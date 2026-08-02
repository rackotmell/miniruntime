#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <thread>

namespace miniruntime
{

/**
 * @brief Thread pool with automatic size adjustment.
 *
 * DynamicThreadPool runs tasks from a bounded blocking queue on a set of
 * worker threads. The pool starts with minPoolSize workers, grows while
 * the queue is overloaded (up to maxPoolSize), and shrinks when a worker
 * sits idle for idleTimeout.
 */
class DynamicThreadPool
{
public:
    using Task = std::function<void()>;

    /**
     * @brief Constructs the pool and spawns the initial workers.
     * @param minPoolSize Number of workers kept alive regardless of load.
     * @param maxPoolSize Upper bound for automatic scaling up.
     * @param idleTimeout How long a worker idles before retiring itself.
     * @param taskQueueSize Capacity of the internal task queue.
     */
    explicit DynamicThreadPool(size_t minPoolSize = std::thread::hardware_concurrency(),
                               size_t maxPoolSize = std::thread::hardware_concurrency() * 2,
                               std::chrono::milliseconds idleTimeout = std::chrono::seconds(30),
                               size_t taskQueueSize = 1000);
    ~DynamicThreadPool();

    /**
     * @brief Enqueue a task for execution; may block if the queue is full.
     * @param task Callable to run on one of the pool threads.
     */
    void enqueue(Task task);

private:
    struct Impl;
    std::unique_ptr<Impl> m_iml;
};

} // namespace miniruntime