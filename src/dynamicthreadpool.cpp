#include "dynamicthreadpool.h"
#include "logger.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <mutex>
#include <stop_token>
#include <thread>
#include <utility>

namespace miniruntime {

    DynamicThreadPool::DynamicThreadPool(
        size_t minPoolSize,
        size_t maxPoolSize,
        std::chrono::milliseconds idleTimeout,
        size_t taskQueueSize
    ) : m_taskQueue(taskQueueSize)
      , m_idleTimeout(idleTimeout)
      , m_minPoolSize(minPoolSize)
      , m_maxPoolSize(maxPoolSize)
    {
        const auto idleMs = idleTimeout.count();
        LOG_DEBUG("DynamicThreadPool created: min={}, max={}, idleTimeout={}ms, queueSize={}",
              minPoolSize, maxPoolSize, idleMs, taskQueueSize);


        m_threads.reserve(m_maxPoolSize);
        createNThreads(m_minPoolSize);
    }

    DynamicThreadPool::~DynamicThreadPool()
    {
        LOG_DEBUG("DynamicThreadPool destroyed");
        m_taskQueue.close();
    }

    void DynamicThreadPool::enqueue(Task task)
    {
        adjustSize();
        m_taskQueue.push(std::move(task));
    }

    void DynamicThreadPool::worker(std::stop_token stopToken)
    {
        const auto threadId = std::this_thread::get_id();
        LOG_DEBUG("DynamicThreadPool::worker started (id={})", threadId);

        while(!stopToken.stop_requested()) {
            auto task = m_taskQueue.timeoutPop(m_idleTimeout);
            if (task) { 
                try {
                    (*task)();
                } catch (...) {
                    LOG_WARNING("Task failed in worker (id={})", threadId);
                }
                continue;
            }

            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_threads.size() <= m_minPoolSize)
                continue;

            const auto& it = std::find_if(m_threads.begin(), m_threads.end(),
                [id = std::this_thread::get_id()](auto& thread){
                return id == thread.get_id();
            });

            if (it == m_threads.end())
                continue;

            m_zombie = std::move(*it);
            m_threads.erase(it);

            LOG_DEBUG("DynamicThreadPool::worker exiting due to idle timeout (id={})", threadId);

            return;
        }
        LOG_DEBUG("DynamicThreadPool::worker stopped by stop_token (id={})", threadId);
    }

    void DynamicThreadPool::createNThreads(size_t n)
    {
        for (size_t i = 0; i < n; ++i) {
            m_threads.emplace_back([this](std::stop_token st){
                worker(std::move(st));
            });
        }
    }

    void DynamicThreadPool::adjustSize()
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        const auto threadCount = m_threads.size();
        const auto taskCount = m_taskQueue.size();

        if (taskCount > 2 * threadCount && threadCount < m_maxPoolSize) {
            const auto newThreadsCount = threadCount + 1;
            LOG_INFO("DynamicThreadPool scaling up: new threads count={} (previous={})", 
                newThreadsCount, threadCount);

            createNThreads(1);
        }
    }

}