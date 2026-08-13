#pragma once

#include "miniruntime/task/hazardpointers.h"
#include <atomic>
#include <chrono>
#include <cstddef>
#include <optional>
#include <thread>

namespace miniruntime::task
{

/**
 * @brief Unbounded lock-free FIFO queue (Michael & Scott, 1996).
 *
 * A classic two-pointer queue: a dummy head node separates the producer
 * (m_tail) and consumer (m_head) fronts so that push and pop never contend.
 * The algorithm itself is lock-free; memory reclamation is delegated to
 * HazardPointers, which guarantees a node is never deleted while another
 * thread still dereferences it.
 *
 * Poping from an empty queue spins (yields) until an element appears or the
 * queue is closed. No strict ordering of concurrent pushes is guaranteed.
 *
 * @tparam T Element type; must be movable and trivially destroyable.
 */
template <typename T> class MichaelScottQueue
{
public:
    /**
     * @brief Constructs the queue with a single dummy head/tail node.
     */
    MichaelScottQueue() : m_size(0), m_closed(false)
    {
        auto* dummy = new Node();
        m_head.store(dummy, std::memory_order_relaxed);
        m_tail.store(dummy, std::memory_order_relaxed);
    }

    /**
     * @brief Releases all remaining nodes (must be called without concurrent users).
     */
    ~MichaelScottQueue()
    {
        s_hazardPointers.drain();

        Node* current = m_head.load(std::memory_order_relaxed);
        while (current != nullptr) {
            Node* next = current->next.load(std::memory_order_relaxed);
            delete current;
            current = next;
        }
    }

    /**
     * @brief Moves/copies a value to the back of the queue.
     * @param value Element to enqueue (perfectly forwarded).
     * @return false if the queue is already closed, true otherwise.
     * @throws std::bad_alloc or an exception from T's constructor if the new
     * node cannot be allocated.
     */
    template <typename U> bool push(U&& value) { return emplace(std::forward<U>(value)); }

    /**
     * @brief Removes and returns the front element; blocks (spins) until available.
     * @return The element, or std::nullopt if the queue is closed and drained.
     * @throws An exception from T's move constructor when extracting the element.
     */
    std::optional<T> pop()
    {
        std::optional<T> result;

        while (true) {
            auto state = tryPop(result);
            if (state == TryState::Popped) return result;
            if (state == TryState::Empty && m_closed.load(std::memory_order_acquire))
                return std::nullopt;

            std::this_thread::yield();
        }
    }

    /**
     * @brief Like pop(), but gives up after the given timeout.
     * @param duration Maximum time to wait for an element.
     * @return The element, or std::nullopt on timeout or queue closure.
     * @throws An exception from T's move constructor when extracting the element.
     */
    template <typename Rep, typename Period>
    std::optional<T> timeoutPop(std::chrono::duration<Rep, Period> duration)
    {
        auto deadline = std::chrono::steady_clock::now() + duration;
        std::optional<T> result;

        while (true) {
            if (std::chrono::steady_clock::now() >= deadline) return std::nullopt;

            auto state = tryPop(result);
            if (state == TryState::Popped) return result;
            if (state == TryState::Empty && m_closed.load(std::memory_order_acquire))
                return std::nullopt;

            std::this_thread::yield();
        }
    }

    /**
     * @brief In-place push; constructs the element at the back of the queue.
     * @param args Constructor arguments for T.
     * @return false if the queue is already closed, true otherwise.
     * @throws std::bad_alloc or an exception from T's constructor if the new
     * node cannot be allocated.
     */
    template <typename... Args> bool emplace(Args&&... args)
    {
        if (m_closed.load(std::memory_order_relaxed)) return false;

        Node* newNode = s_hazardPointers.allocate();
        if (newNode) {
            newNode->next.store(nullptr, std::memory_order_relaxed);
            newNode->value.emplace(std::forward<Args>(args)...);
        } else {
            newNode = new Node(std::forward<Args>(args)...);
        }

        while (true) {
            Node* t = m_tail.load(std::memory_order_acquire);
            size_t slot = s_hazardPointers.protect(t);
            if (m_tail.load(std::memory_order_acquire) != t) {
                s_hazardPointers.unprotect(slot);
                continue;
            }

            // Another producer already appended a node; help move the tail forward.
            Node* next = t->next.load(std::memory_order_acquire);
            if (next != nullptr) {
                m_tail.compare_exchange_weak(
                    t, next, std::memory_order_release, std::memory_order_relaxed);
                s_hazardPointers.unprotect(slot);
                continue;
            }

            Node* expected = nullptr;
            if (t->next.compare_exchange_weak(
                    expected, newNode, std::memory_order_release, std::memory_order_relaxed)) {

                s_hazardPointers.unprotect(slot);

                // Best-effort advance of the tail; the loser retries in the next iteration.
                m_tail.compare_exchange_weak(
                    t, newNode, std::memory_order_release, std::memory_order_relaxed);

                m_size.fetch_add(1, std::memory_order_relaxed);
                return true;
            }

            s_hazardPointers.unprotect(slot);
        }
    }

    /**
     * @brief Marks the queue as closed; unblocks waiters and rejects new pushes.
     */
    void close() { m_closed.store(true, std::memory_order_relaxed); }

    /**
     * @brief Approximate current number of elements (relaxed snapshot).
     */
    size_t size() const { return m_size.load(std::memory_order_relaxed); }

private:
    enum class TryState { Retry, Empty, Popped };

    /**
     * @brief One non-blocking attempt to pop an element.
     * @param result Receives the moved-out value on success.
     * @return Popped on success, Empty if the queue is empty, Retry on contention.
     */
    TryState tryPop(std::optional<T>& result)
    {
        Node* h = m_head.load(std::memory_order_acquire);
        size_t hSlot = s_hazardPointers.protect(h);
        if (m_head.load(std::memory_order_acquire) != h) {
            s_hazardPointers.unprotect(hSlot);
            return TryState::Retry;
        }

        Node* headNext = h->next.load(std::memory_order_acquire);
        if (headNext == nullptr) {
            s_hazardPointers.unprotect(hSlot);
            return TryState::Empty;
        }

        size_t nSlot = s_hazardPointers.protect(headNext);
        if (m_head.load(std::memory_order_acquire) != h) {
            s_hazardPointers.unprotect(hSlot);
            s_hazardPointers.unprotect(nSlot);
            return TryState::Retry;
        }

        // Queue has a single (dummy) node left; another consumer drains it.
        Node* t = m_tail.load(std::memory_order_acquire);
        if (h == t) {
            m_tail.compare_exchange_weak(
                t, headNext, std::memory_order_release, std::memory_order_relaxed);
            s_hazardPointers.unprotect(hSlot);
            s_hazardPointers.unprotect(nSlot);
            return TryState::Retry;
        }

        // Detach the current head; the old dummy becomes retired garbage.
        Node* expected = h;
        if (m_head.compare_exchange_weak(
                expected, headNext, std::memory_order_release, std::memory_order_relaxed)) {
            result = std::move(headNext->value.value());
            m_size.fetch_sub(1, std::memory_order_relaxed);
            s_hazardPointers.unprotect(hSlot);
            s_hazardPointers.unprotect(nSlot);
            s_hazardPointers.retire(h);
            return TryState::Popped;
        }
        s_hazardPointers.unprotect(hSlot);
        s_hazardPointers.unprotect(nSlot);
        return TryState::Retry;
    }

    // A queue node; the first node is a dummy that does not hold a value.
    struct Node {
        std::optional<T> value;
        std::atomic<Node*> next;

        Node() : value(std::nullopt), next(nullptr) {}
        template <typename... Args>
        explicit Node(Args&&... args)
            : value(std::in_place, std::forward<Args>(args)...), next(nullptr)
        {
        }
    };

    std::atomic<Node*> m_head;
    std::atomic<Node*> m_tail;
    std::atomic<size_t> m_size;
    std::atomic<bool> m_closed;

    // Reclaimer shared by all queues of this type.
    inline static HazardPointers<Node> s_hazardPointers;
};

} // namespace miniruntime::task