#pragma once

#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "dynamicthreadpool.h"
#include "eventloop.h"
#include "future.h"
#include "handle.h"

namespace miniruntime {

    class TaskScheduler {
    public:
        TaskScheduler(size_t minParallelTasks, size_t maxParallelTasks);

        template<typename F, typename... Args>
        auto execute(F&& f, Args&&... args) 
            -> Future<std::invoke_result_t<F, Args...>>
        {
            using ResultType = std::invoke_result_t<F, Args...>;
            std::shared_ptr<Promise<ResultType>> promise;
            Future<ResultType> future = promise->getFuture();

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
            using ResultType = std::invoke_result_t<F, Args...>;
            std::shared_ptr<Promise<ResultType>> promise;
            Future<ResultType> future = promise->getFuture();

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

            TimerHandle timer = m_loop.createTimer(
                delay,
                [this, task = std::move(threadPoolTask)] {
                    m_pool.enqueue(std::move(task));
                }
            );

            std::lock_guard<std::mutex> lock(m_timerMutex);
            m_timers.push_back(std::move(timer));
        }

    private:
        DynamicThreadPool m_pool;
        EventLoop m_loop;
        std::jthread m_loopThread;
        
        std::mutex m_timerMutex;
        std::vector<TimerHandle> m_timers;

        std::mutex m_intervalMutex;
        std::vector<IntervalHandle> m_intervals;
    };

}