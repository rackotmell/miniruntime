#pragma once

#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace miniruntime::task
{

/**
 * @brief Thread-safe garbage collector for lock-free structures (Hazard Pointers).
 *
 * Lock-free algorithms may access a pointer that another thread is about to
 * delete (ABA / use-after-free). Hazard pointers solve this by "protecting"
 * a pointer before dereferencing it: a reader records the address in a
 * per-thread slot that the reclaimer must check before actually free'ing.
 *
 * Contract:
 *  - protect() before each dereference of a shared node pointer;
 *  - unprotect() as soon as the pointer is no longer needed;
 *  - retire() hands a logically-removed node to the reclaimer, which frees
 *    it only when no slot still points at it.
 *
 * Internally each thread owns a thread-local announcement record (slots) and
 * a list of retired nodes. When the list grows too large, scan() walks every
 * thread's slot and deallocates everything that is no longer protected.
 * Freed nodes are kept in a shared node pool (pool()) and reused by allocate()
 * to avoid repeated allocation of the queue's dummy nodes.
 *
 * @tparam T Type of the protected nodes; must be deallocateable with delete.
 */
template <typename T> class HazardPointers
{
public:
    // Number of pointers one thread may protect simultaneously.
    static constexpr size_t slotsCount = 4;
    // Retired nodes per thread that trigger a scan().
    static constexpr size_t retireThreshold = 1024;

    HazardPointers() = default;
    HazardPointers(const HazardPointers&) = delete;
    HazardPointers& operator=(const HazardPointers&) = delete;

    ~HazardPointers() { drainPool(); }

    /**
     * @brief Marks ptr as currently in use by the calling thread.
     * @param ptr Address that must not be freed while protected.
     * @return Slot index to pass to unprotect() later.
     * @throws std::runtime_error if all slots are occupied.
     */
    size_t protect(T* ptr)
    {
        initRecord();
        for (size_t i = 0; i < slotsCount; ++i) {
            T* expected = nullptr;
            if (s_threadRecord->slots[i].compare_exchange_weak(
                    expected, ptr, std::memory_order_release, std::memory_order_relaxed))
                return i;
        }
        throw std::runtime_error("Stored pointers overflow");
    }

    /**
     * @brief Releases the slot previously returned by protect().
     * @param slot Index obtained from protect().
     */
    void unprotect(size_t slot)
    {
        s_threadRecord->slots[slot].store(nullptr, std::memory_order_release);
    }

    /**
     * @brief Registers a logically-removed node for deferred deallocation.
     *
     * The node becomes a candidate for free; it is actually deallocated
     * (or moved to the pool) only when no other thread protects it.
     */
    void retire(T* ptr)
    {
        s_threadRetired.list.push_back(ptr);
        if (s_threadRetired.list.size() > retireThreshold) scan();
    }

    /**
     * @brief Reclaims as many retired nodes as possible (see retire()).
     * @throws std::bad_alloc if the survivor list cannot be allocated.
     */
    void scan()
    {
        auto& localList = s_threadRetired.list;
        std::vector<T*> survivors;
        survivors.reserve(localList.size());

        for (auto* retiredPtr : localList) {
            if (isProtected(retiredPtr))
                survivors.push_back(retiredPtr);
            else
                deallocate(retiredPtr);
        }
        localList = std::move(survivors);

        scanOrphaned();
    }

    /**
     * @brief Forcibly frees all retired nodes, ignoring outstanding protection.
     *
     * Called by the queue destructor, which guarantees no other thread is
     * still using the nodes, so it is safe to release them unconditionally.
     */
    void drain()
    {
        auto& localList = s_threadRetired.list;
        for (auto* ptr : localList)
            deallocate(ptr);
        localList.clear();

        scanOrphaned();
    }

    /**
     * @brief Returns a reusable node from the internal pool, if any.
     * @return A pooled pointer, or nullptr if the pool is empty.
     * @throws std::bad_alloc if the pool lock or internal storage cannot be allocated.
     */
    T* allocate()
    {
        lockPool();
        T* ptr = nullptr;
        if (!pool().empty()) {
            ptr = pool().back();
            pool().pop_back();
        }
        unlockPool();
        return ptr;
    }

    /**
     * @brief Returns a node to the internal pool (the pool owns deallocation).
     * @throws std::bad_alloc if the pool storage cannot be allocated.
     */
    void deallocate(T* ptr)
    {
        lockPool();
        pool().push_back(ptr);
        unlockPool();
    }

private:
    // Per-thread announcement record: up to slotsCount protected pointers.
    struct AnouncementRecord {
        std::atomic<T*> slots[slotsCount];
        std::atomic<AnouncementRecord*> next;

        AnouncementRecord() : slots{nullptr}, next{nullptr} {}
    };

    // Retired-but-not-yet-reclaimed nodes owned by the calling thread.
    // On thread exit, remaining nodes are moved to s_orphanedRetired so they
    // can be reclaimed later by scanOrphaned().
    struct ThreadRetiredList {
        std::vector<T*> list;
        ~ThreadRetiredList()
        {
            if (!list.empty()) {
                int expected = 0;
                while (!m_orphanedLock.compare_exchange_strong(
                    expected, 1, std::memory_order_acquire, std::memory_order_relaxed)) {
                    m_orphanedLock.wait(1, std::memory_order_relaxed);
                    expected = 0;
                }
                s_orphanedRetired.insert(
                    s_orphanedRetired.end(), list.begin(), list.end());
                m_orphanedLock.store(0, std::memory_order_release);
                m_orphanedLock.notify_one();
            }
        }
    };

    // Simple spin-lock guarding the shared node pool.
    void lockPool()
    {
        int expected = 0;
        while (!m_poolLock.compare_exchange_strong(
            expected, 1, std::memory_order_acquire, std::memory_order_relaxed)) {
            m_poolLock.wait(1, std::memory_order_relaxed);
            expected = 0;
        }
    }

    void unlockPool()
    {
        m_poolLock.store(0, std::memory_order_release);
        m_poolLock.notify_one();
    }

    // Simple spin-lock guarding the shared orphaned list of retired nodes.
    void lockOrphaned()
    {
        int expected = 0;
        while (!m_orphanedLock.compare_exchange_strong(
            expected, 1, std::memory_order_acquire, std::memory_order_relaxed)) {
            m_orphanedLock.wait(1, std::memory_order_relaxed);
            expected = 0;
        }
    }

    void unlockOrphaned()
    {
        m_orphanedLock.store(0, std::memory_order_release);
        m_orphanedLock.notify_one();
    }

    // Lazily publishes the calling thread's announcement record into the global list.
    void initRecord()
    {
        if (s_threadRecord == nullptr) {
            auto* rec = new AnouncementRecord();
            s_threadRecord = rec;

            auto* expected = s_head.load(std::memory_order_relaxed);
            do {
                rec->next.store(expected, std::memory_order_relaxed);
            } while (!s_head.compare_exchange_weak(
                expected, rec, std::memory_order_release, std::memory_order_relaxed));
        }
    }

    // True if any thread currently protects ptr.
    bool isProtected(T* ptr)
    {
        for (auto* record = s_head.load(std::memory_order_acquire); record != nullptr;
             record = record->next.load(std::memory_order_acquire)) {
            for (size_t i = 0; i < slotsCount; ++i) {
                if (record->slots[i].load(std::memory_order_acquire) == ptr) return true;
            }
        }
        return false;
    }

    // Reclaims nodes retired by threads that have since terminated.
    void scanOrphaned()
    {
        lockOrphaned();
        std::vector<T*> survivors;
        survivors.reserve(s_orphanedRetired.size());

        for (auto* retiredPtr : s_orphanedRetired) {
            if (isProtected(retiredPtr))
                survivors.push_back(retiredPtr);
            else
                deallocate(retiredPtr);
        }
        s_orphanedRetired = std::move(survivors);
        unlockOrphaned();
    }

    // Deletes all nodes in the pool (destructor path).
    void drainPool()
    {
        lockPool();
        for (auto* ptr : pool())
            delete ptr;
        pool().clear();
        unlockPool();
    }

    inline thread_local static AnouncementRecord* s_threadRecord{nullptr};
    inline static std::atomic<AnouncementRecord*> s_head{nullptr};
    inline thread_local static ThreadRetiredList s_threadRetired;
    inline static std::vector<T*> s_orphanedRetired;
    inline static std::atomic<int> m_orphanedLock{0};
    inline static std::atomic<int> m_poolLock{0};

    // Heap-allocated to survive static destruction order fiasco:
    // ~HazardPointers() calls drainPool() which accesses the pool, so the
    // pool must outlive the static HazardPointers instance.
    static std::vector<T*>& pool()
    {
        static auto* instance = new std::vector<T*>();
        return *instance;
    }
};

} // namespace miniruntime::task