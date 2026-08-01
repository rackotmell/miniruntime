#pragma once

#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>

#include "future.h"
#include "logger.h"
#include "sharedvalue.h"

namespace miniruntime {

/**
 * @brief High-level facade over the runtime components.
 *
 * TaskScheduler orchestrates the low-level building blocks
 * (EventLoop, DynamicThreadPool, Handle, Future/Promise, SharedValue)
 * and exposes a simple, synchronous API for asynchronous task execution:
 *   - execute():          run a task immediately on the thread pool;
 *   - schedule():         run a task once after a delay;
 *   - scheduleInterval(): run a task repeatedly at a fixed interval.
 *
 * It is a convenience facade: users are free to bypass it and combine
 * the underlying components directly for finer control.
 */
class TaskScheduler
{
public:
    /**
     * @brief Construct a scheduler with a dynamically resized thread pool.
     * @param minParallelTasks Minimum number of pool threads.
     * @param maxParallelTasks Maximum number of pool threads.
     */
    TaskScheduler(size_t minParallelTasks = std::thread::hardware_concurrency(),
                  size_t maxParallelTasks = 2 * std::thread::hardware_concurrency());

    /**
     * @brief Stops the event loop and joins its thread, then destroys the runtime.
     */
    ~TaskScheduler();

    /**
     * @brief Runs a callable immediately on the thread pool.
     * @return Future holding the result (or the exception) once the task completes.
     */
    template <typename F, typename... Args>
    auto execute(F&& f, Args&&... args) -> Future<std::invoke_result_t<F, Args...>>
    {
        LOG_DEBUG("TaskScheduler::excecute called");

        using ResultType = std::invoke_result_t<F, Args...>;
        auto promise = std::make_shared<Promise<ResultType>>();

        // Wrap the user callable and put it into thread pool.
        enqueue([promise, func = std::forward<F>(f), ... args = std::forward<Args>(args)] {
            try
            {
                if constexpr (std::is_void_v<ResultType>)
                {
                    func(args...);
                    promise->setValue();
                } else
                {
                    promise->setValue(func(args...));
                }
            } catch (...)
            {
                promise->setException(std::current_exception());
            }
        });

        return promise->getFuture();
    }

    /**
     * @brief Schedules a callable to run once after the given delay.
     * @param delay Delay before the task is dispatched to the thread pool.
     * @return Future holding the result (or the exception) once the task completes;
     * can be canceled by id before triggering.
     */
    template <typename F, typename... Args>
    auto schedule(std::chrono::milliseconds delay, F&& f, Args&&... args)
        -> Future<std::invoke_result_t<F, Args...>>
    {
        using ResultType = std::invoke_result_t<F, Args...>;
        auto promise = std::make_shared<Promise<ResultType>>();

        // Register a one-shot timer.
        uint64_t id = createTimer(
            delay,
            [promise, func = std::forward<F>(f), ... args = std::forward<Args>(args)] {
                try
                {
                    if constexpr (std::is_void_v<ResultType>)
                    {
                        func(args...);
                        promise->setValue();
                    } else
                    {
                        promise->setValue(func(args...));
                    }
                } catch (...)
                {
                    promise->setException(std::current_exception());
                }
            },
            [promise] { promise->close(); });

        promise->setId(id);
        LOG_DEBUG("TaskScheduler:schedule task scheduled, id={}, delay={}", id, delay);

        return promise->getFuture();
    }

    /**
     * @brief Schedules a callable to run repeatedly at a fixed interval.
     * @param interval Period between consecutive runs.
     * @return SharedValue holding the latest result; cancel it by id to stop.
     */
    template <typename F, typename... Args>
    auto scheduleInterval(std::chrono::milliseconds interval, F&& f, Args&&... args)
        -> std::shared_ptr<SharedValue<std::invoke_result_t<F, Args...>>>
    {
        using ResultType = std::invoke_result_t<F, Args...>;
        auto value = std::make_shared<SharedValue<ResultType>>();

        // Register an interval timer.
        uint64_t id = createInterval(
            interval,
            [valuePtr = value, func = std::forward<F>(f), ... args = std::forward<Args>(args)] {
                try
                {
                    if constexpr (std::is_void_v<ResultType>)
                    {
                        func(args...);
                        valuePtr->set();
                    } else
                    {
                        valuePtr->set(func(args...));
                    }
                } catch (...)
                {
                    valuePtr->setException(std::current_exception());
                }
            },
            [value] { value->close(); });
        value->setId(id);
        LOG_DEBUG("TaskScheduler::scheduleInterval interval task scheduled, id={}, interval={}",
                  id,
                  interval);

        return value;
    }

    /**
     * @brief Starts the event loop on a dedicated thread. Idempotent.
     */
    void init();

    /**
     * @brief Stops the event loop.
     */
    void stop();

    /**
     * @brief Cancels a pending timer or a recurring interval by its id.
     * @param id Identifier returned by schedule()/scheduleInterval().
     * @return true if a task with the given id was found and cancelled.
     */
    bool cancel(std::optional<uint64_t> id);

private:
    /// Enqueue a type-erased task onto the thread pool.
    void enqueue(std::function<void()> task);

    /// Register a one-shot timer; returns the assigned timer id.
    uint64_t createTimer(std::chrono::milliseconds& delay,
                         std::function<void()> task,
                         std::function<void()> onCancel);

    /// Register an interval timer; returns the assigned timer id.
    uint64_t createInterval(std::chrono::milliseconds& interval,
                            std::function<void()> task,
                            std::function<void()> onCancel);

    // Pimpl: hides all runtime internals
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace miniruntime
