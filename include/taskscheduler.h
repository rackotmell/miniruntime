#pragma once

#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <list>

#include "dynamicthreadpool.h"
#include "eventloop.h"
#include "future.h"
#include "handle.h"
#include "sharedstate.h"
#include "logger.h"

namespace miniruntime {

    class TaskScheduler {
    public:
        TaskScheduler(
            size_t minParallelTasks = std::thread::hardware_concurrency(),
            size_t maxParallelTasks = 2 * std::thread::hardware_concurrency()
        );
        ~TaskScheduler();

        template<typename F, typename... Args>
        auto execute(F&& f, Args&&... args) 
            -> Future<std::invoke_result_t<F, Args...>>
        {
            LOG_DEBUG("TaskScheduler::excecute called");

            using ResultType = std::invoke_result_t<F, Args...>;
            auto promise = std::make_shared<Promise<ResultType>>();
            auto future = promise->getFuture();

            m_pool.enqueue([
                promise,
                func = std::forward<F>(f),
                ...args = std::forward<Args>(args)
            ] {
                try {
                    if constexpr (std::is_void_v<ResultType>) {
                        func(args...);
                        promise->setValue();
                    } else {
                        promise->setValue(func(args...));
                    }
                } catch (...) {
                    promise->setException(std::current_exception());
                }
            });

            return future;
        }

        template<typename F, typename... Args>
        auto schedule(std::chrono::milliseconds delay, F&& f, Args&&... args)
            -> Future<std::invoke_result_t<F, Args...>>
        {
            LOG_DEBUG("TaskScheduler:schedule called with delay={}", delay);

            using ResultType = std::invoke_result_t<F, Args...>;
            auto promise = std::make_shared<Promise<ResultType>>();
            auto future = promise->getFuture();

            DynamicThreadPool::Task threadPoolTask = [
                promise,
                func = std::forward<F>(f),
                ...args = std::forward<Args>(args)
            ] {
                try {
                    if constexpr (std::is_void_v<ResultType>) {
                        func(args...);
                        promise->setValue();
                    } else {
                        promise->setValue(func(args...));
                    }
                } catch (...) {
                    promise->setException(std::current_exception());
                }
            };

            TimerHandle handle = m_loop.createTimer(
                delay,
                [this, task = std::move(threadPoolTask)] {
                    m_pool.enqueue(std::move(task));
                }
            );

            std::lock_guard<std::mutex> lock(m_timerMutex);
            m_timers.push_back(std::move(handle));

            return future;
        }

        template<typename F, typename... Args>
        auto scheduleInterval(std::chrono::milliseconds interval, F&& f, Args&&... args)
            -> std::shared_ptr<SharedValue<std::invoke_result_t<F, Args...>>>
        {
            LOG_DEBUG("TaskScheduler::scheduleInterval called with interval={}", interval);

            using ResultType = std::invoke_result_t<F, Args...>;
            auto value = std::make_shared<SharedValue<ResultType>>();

            DynamicThreadPool::Task threadPoolTask = [
                valuePtr = value,
                func = std::forward<F>(f),
                ...args = std::forward<Args>(args)
            ] {
                try {
                    if constexpr (std::is_void_v<ResultType>) {
                        func(args...);
                        valuePtr->set();
                    } else {
                        valuePtr->set(func(args...));
                    }
                } catch (...) {
                    valuePtr->setException(std::current_exception());
                }
            };

            IntervalHandle handle = m_loop.createInterval(
                interval,
                [this, task = std::move(threadPoolTask)] {
                    m_pool.enqueue(std::move(task));
                }
            );

            std::lock_guard<std::mutex> lock(m_timerMutex);
            m_intervals.push_back(std::move(handle));

            return value;
        }

        void init();
        void stop();

        

    private:
        DynamicThreadPool m_pool;
        EventLoop m_loop;
        std::jthread m_loopThread;
        std::unique_ptr<IntervalHandle> m_timerCleaner;
        
        std::mutex m_timerMutex;
        std::list<TimerHandle> m_timers;

        std::mutex m_intervalMutex;
        std::list<IntervalHandle> m_intervals;
    };

}