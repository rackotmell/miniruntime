#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <mutex>
#include <stop_token>
#include <thread>
#include <vector>

#include "boundedblockingqueue.h"

namespace miniruntime {

    class DynamicThreadPool {
        using Task = std::function<void()>;

        public:
            explicit DynamicThreadPool(
                size_t minPoolSize = std::thread::hardware_concurrency(),
                size_t maxPoolSize = std::thread::hardware_concurrency() * 2,
                std::chrono::milliseconds idleTimeout = std::chrono::seconds(30),
                size_t taskQueueSize = 1000
            );
            ~DynamicThreadPool();
            void enqueue(Task task);

        private:
            std::vector<std::jthread> m_threads;
            std::vector<std::jthread> m_zombies;
            BoundedBlockingQueue<Task> m_taskQueue;

            const std::chrono::milliseconds m_idleTimeout;
            const size_t m_minPoolSize;
            const size_t m_maxPoolSize;

            std::mutex m_mutex;

            void worker(std::stop_token stopToken);
            void createNThreads(int n);
    };

}