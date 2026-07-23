#include "threadpool.h"

#include <cstddef>

namespace miniruntime {

    ThreadPool::ThreadPool(size_t poolSize, size_t taskQueueSize)
        : m_taskQueue(taskQueueSize)
    {
        m_threads.reserve(poolSize);
        for (int i = 0; i < poolSize; ++i) {
            m_threads.emplace_back(&ThreadPool::worker, this);
        }
    }

    ThreadPool::~ThreadPool()
    {
        m_taskQueue.close();
    }

    void ThreadPool::enqueue(Task task)
    {
        m_taskQueue.push(task);
    }

    void ThreadPool::worker()
    {
        while(auto task = m_taskQueue.pop()) {
            if (!(*task)) 
                break;

            try {
                (*task)();
            } catch (...) { }
        }
    }

}