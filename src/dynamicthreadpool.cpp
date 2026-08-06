#include "dynamicthreadpool.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <stop_token>
#include <utility>
#include <vector>

#include "logger.h"
#include "michaelscottqueue.h"

namespace miniruntime
{

template <template <typename> class QueueType>
    requires Queue<QueueType, std::function<void()>>
struct DynamicThreadPool<QueueType>::Impl {
    std::vector<std::jthread> threads;
    QueueType<Task> taskQueue;
    std::jthread zombie;
    std::timed_mutex mutex;

    // Pool tuning parameters; const, fixed for the pool lifetime.
    const std::chrono::milliseconds idleTimeout;
    const size_t minPoolSize;
    const size_t maxPoolSize;

    Impl(size_t minPoolSize,
         size_t maxPoolSize,
         std::chrono::milliseconds idleTimeout,
         size_t taskQueueSize)
        : taskQueue(createQueue(taskQueueSize)), idleTimeout(idleTimeout), minPoolSize(minPoolSize),
          maxPoolSize(maxPoolSize)
    {
    }

    // Factory method for creation queue from the template parameter
    static QueueType<Task> createQueue(size_t taskQueueSize)
    {
        if constexpr (std::is_constructible_v<QueueType<Task>, size_t>) {
            return QueueType<Task>(taskQueueSize);
        } else {
            return QueueType<Task>();
        }
    }

    // Main worker loop: take tasks until stopped or idle for too long.
    void worker(std::stop_token stopToken)
    {
        const auto threadId = std::this_thread::get_id();
        LOG_DEBUG("DynamicThreadPool::worker started (id={})", threadId);

        while (!stopToken.stop_requested()) {
            auto task = taskQueue.timeoutPop(idleTimeout);
            if (task) {
                try {
                    (*task)();
                } catch (...) {
                    LOG_WARNING("Task failed in worker (id={})", threadId);
                }
                continue;
            }

            // No task within idleTimeout: consider retiring the worker.
            // Lock with timeout to avoid deadlock with destructor
            std::unique_lock lock(mutex, std::defer_lock);
            constexpr auto lockTimeout = std::chrono::milliseconds(50);

            if (!lock.try_lock_for(lockTimeout)) continue;

            // Keep the pool floor: never shrink below minPoolSize.
            if (threads.size() <= minPoolSize) continue;

            // Find our own thread object before erasing it.
            const auto& it = std::find_if(
                threads.begin(), threads.end(), [id = std::this_thread::get_id()](auto& thread) {
                    return id == thread.get_id();
                });

            if (it == threads.end()) continue;

            // Move the retiring thread into the zombie slot so it is
            // joined elsewhere; this thread returns and ends itself.
            zombie = std::move(*it);
            threads.erase(it);

            LOG_DEBUG("DynamicThreadPool::worker exiting due to idle timeout (id={})", threadId);

            return;
        }
        LOG_DEBUG("DynamicThreadPool::worker stopped by stop_token (id={})", threadId);
    }

    void createNThreads(size_t n)
    {
        for (size_t i = 0; i < n; ++i) {
            threads.emplace_back([this](std::stop_token st) { worker(std::move(st)); });
        }
    }

    // Heuristic scaling: add one worker when the queue has more than
    // two tasks per thread and the pool has not reached its cap.
    void adjustSize()
    {
        std::lock_guard lock(mutex);

        const auto threadCount = threads.size();
        const auto taskCount = taskQueue.size();

        if (taskCount > 2 * threadCount && threadCount < maxPoolSize) {
            LOG_INFO("DynamicThreadPool scaling up: new threads count={} (previous={})",
                     threadCount + 1,
                     threadCount);

            createNThreads(1);
        }
    }
};

template <template <typename> class QueueType>
    requires Queue<QueueType, std::function<void()>>
DynamicThreadPool<QueueType>::DynamicThreadPool(size_t minPoolSize,
                                                size_t maxPoolSize,
                                                std::chrono::milliseconds idleTimeout,
                                                size_t taskQueueSize)
    : m_impl(std::make_unique<Impl>(minPoolSize, maxPoolSize, idleTimeout, taskQueueSize))
{
    LOG_DEBUG("DynamicThreadPool created: min={}, max={}, idleTimeout={}ms, queueSize={}",
              minPoolSize,
              maxPoolSize,
              idleTimeout.count(),
              taskQueueSize);

    m_impl->threads.reserve(maxPoolSize);
    m_impl->createNThreads(minPoolSize);
}

template <template <typename> class QueueType>
    requires Queue<QueueType, std::function<void()>>
DynamicThreadPool<QueueType>::~DynamicThreadPool()
{
    {
        std::lock_guard lock(m_impl->mutex);
        for (auto& thread : m_impl->threads) {
            thread.request_stop();
        }
    }
    // Wake up all workers blocked on the empty queue.
    m_impl->taskQueue.close();
    {
        std::lock_guard lock(m_impl->mutex);
        for (auto& thread : m_impl->threads) {
            if (thread.joinable()) thread.join();
        }
    }

    LOG_DEBUG("DynamicThreadPool destroyed");
}

template <template <typename> class QueueType>
    requires Queue<QueueType, std::function<void()>>
void DynamicThreadPool<QueueType>::enqueue(Task task)
{
    // Scale up if overloaded, then hand the task to the queue.
    m_impl->adjustSize();
    m_impl->taskQueue.push(std::move(task));
}

template class DynamicThreadPool<BoundedBlockingQueue>;
template class DynamicThreadPool<MichaelScottQueue>;

} // namespace miniruntime
