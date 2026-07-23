#pragma once

#include <cstddef>
#include <functional>
#include <thread>
#include <vector>

#include "boundedblockingqueue.h"

namespace miniruntime {

    class ThreadPool {
        using Task = std::function<void()>;

        public:
            explicit ThreadPool(
                size_t poolSize = std::thread::hardware_concurrency(),
                size_t taskQueueSize = 1000
            );
            ~ThreadPool();
            void enqueue(Task task);

        private:
            std::vector<std::jthread> m_threads;
            BoundedBlockingQueue<Task> m_taskQueue;

            void worker();
    };

}