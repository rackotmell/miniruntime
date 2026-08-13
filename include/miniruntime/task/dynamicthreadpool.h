#pragma once

#include <chrono>
#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <thread>

#include "miniruntime/task/boundedblockingqueue.h"

namespace miniruntime::task
{

// Queue type concept
template <template <typename> class Q, typename T>
concept Queue = requires(Q<T> queue, T value) {
    { queue.push(value) } -> std::same_as<bool>;
    { queue.pop() } -> std::same_as<std::optional<T>>;
    { queue.timeoutPop(std::chrono::seconds(1)) } -> std::same_as<std::optional<T>>;
    { queue.close() } -> std::same_as<void>;
    { queue.size() } -> std::same_as<size_t>;
};

/**
 * @brief Thread pool with automatic size adjustment.
 *
 * DynamicThreadPool runs tasks from a configurable queue on a set of
 * worker threads. The pool starts with minPoolSize workers, grows while
 * the queue is overloaded (up to maxPoolSize), and shrinks when a worker
 * sits idle for idleTimeout.
 *
 * @tparam QueueType A queue template (BoundedBlockingQueue or MichaelScottQueue).
 *   The task type (std::function<void()>) is fixed internally.
 */
template <template <typename> class QueueType = BoundedBlockingQueue>
    requires Queue<QueueType, std::function<void()>>
class DynamicThreadPool
{
public:
    using Task = std::function<void()>;

    /**
     * @brief Constructs the pool and spawns the initial workers.
     * @param minPoolSize Number of workers kept alive regardless of load.
     * @param maxPoolSize Upper bound for automatic scaling up.
     * @param idleTimeout How long a worker idles before retiring itself.
     * @param taskQueueSize Capacity of the internal task queue;
     * ignored if used unbounded MichaelScottQueue.
     * @throws std::system_error if the initial pool threads cannot be spawned.
     */
    explicit DynamicThreadPool(size_t minPoolSize = std::thread::hardware_concurrency(),
                               size_t maxPoolSize = std::thread::hardware_concurrency() * 2,
                               std::chrono::milliseconds idleTimeout = std::chrono::seconds(30),
                               size_t taskQueueSize = 1000);
    ~DynamicThreadPool();

    /**
     * @brief Enqueue a task for execution; may block if the queue is full.
     * @param task Callable to run on one of the pool threads.
     * @throws std::bad_alloc if the task cannot be moved into the queue.
     */
    void enqueue(Task task);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace miniruntime::task
