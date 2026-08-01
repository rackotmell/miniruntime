#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>

namespace miniruntime
{

/**
 * @brief Thread-safe bounded blocking queue.
 *
 * A fixed-capacity deque. push() blocks if the capacity is reached;
 * pop() blocks if the queue is empty. Both wake up immediately on close().
 *
 * @tparam T Element type; must be movable.
 */
template <typename T> class BoundedBlockingQueue
{
public:
    /**
     * @brief Constructs the queue with the given capacity.
     * @param maxSize Maximum number of elements the queue may hold.
     */
    BoundedBlockingQueue(size_t maxSize) : m_dequeMaxSize(maxSize) {}

    /**
     * @brief Inserts a value at the back; blocks if the queue is full.
     * @param value Element to enqueue.
     * @return true on success, false if the queue was closed while waiting.
     */
    bool push(T value)
    {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_notFullCv.wait(lock, [this] { return m_deque.size() < m_dequeMaxSize || m_closed; });
            if (m_closed) return false;
            m_deque.push_back(value);
        }
        m_notEmptyCv.notify_one();
        return true;
    }

    /**
     * @brief Removes and returns the front element; blocks if the queue is empty.
     * @return The element, or std::nullopt if the queue is closed and drained.
     */
    std::optional<T> pop()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_notEmptyCv.wait(lock, [this] { return !m_deque.empty() || m_closed; });

        if (m_closed && m_deque.empty()) {
            return std::nullopt;
        }
        T value = std::move(m_deque.front());
        m_deque.pop_front();

        m_notFullCv.notify_one();

        return value;
    }

    /**
     * @brief Like pop(), but returns std::nullopt after the given timeout.
     * @param duration Maximum time to wait for an element.
     * @return The element, or std::nullopt on timeout or queue closure.
     */
    template <typename Rep, typename Period>
    std::optional<T> timeoutPop(std::chrono::duration<Rep, Period> duration)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (!m_notEmptyCv.wait_for(
                lock, duration, [this] { return !m_deque.empty() || m_closed; })) {
            return std::nullopt;
        }

        if (m_deque.empty()) return std::nullopt;

        T value = std::move(m_deque.front());
        m_deque.pop_front();

        m_notFullCv.notify_one();

        return value;
    }

    /**
     * @brief In-place push; blocks if the queue is full.
     * @param args Constructor arguments for T forwarded to emplace_back().
     * @return true on success, false if the queue was closed while waiting.
     */
    template <typename... Args> bool emplace(Args&&... args)
    {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_notFullCv.wait(lock, [this] { return m_deque.size() < m_dequeMaxSize || m_closed; });
            if (m_closed) return false;
            m_deque.emplace_back(std::forward<Args>(args)...);
        }
        m_notEmptyCv.notify_one();
        return true;
    }

    /**
     * @brief Marks the queue as closed; unblocks all waiting threads.
     */
    void close()
    {
        m_closed.store(true);
        m_notEmptyCv.notify_all();
        m_notFullCv.notify_all();
    }

    /**
     * @brief Current number of elements in the queue (thread-safe snapshot).
     */
    size_t size() const
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_deque.size();
    }

private:
    mutable std::mutex m_mutex;
    // Signalled when at least one element becomes available.
    std::condition_variable m_notEmptyCv;
    // Signalled when at least one free slot becomes available.
    std::condition_variable m_notFullCv;

    std::deque<T> m_deque;
    size_t m_dequeMaxSize;
    std::atomic<bool> m_closed = false;
};

} // namespace miniruntime