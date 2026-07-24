#include "dynamicthreadpool.h"

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
        m_threads.reserve(m_maxPoolSize);
        createNThreads(m_minPoolSize);
    }

    DynamicThreadPool::~DynamicThreadPool()
    {
        m_taskQueue.close();
    }

    void DynamicThreadPool::enqueue(Task task)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            const auto threadCount = m_threads.size();
            const auto taskCount = m_taskQueue.size();

            if (taskCount > 2 * threadCount && threadCount < m_maxPoolSize) {
                createNThreads(m_maxPoolSize - threadCount);
            }
        }

        m_taskQueue.push(task);
    }

    void DynamicThreadPool::worker(std::stop_token stopToken)
    {
        while(!stopToken.stop_requested()) {
            auto task = m_taskQueue.timeoutPop(m_idleTimeout);
            if (task) { 
                try {
                    (*task)();
                } catch (...) { }
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

            m_zombies.push_back(std::move(*it));
            m_threads.erase(it);

            return;
        }
    }

    void DynamicThreadPool::createNThreads(int n)
    {
        for (int i = 0; i < n; ++i) {
            m_threads.emplace_back([this](std::stop_token st){
                worker(std::move(st));
            });
        }
    }

}