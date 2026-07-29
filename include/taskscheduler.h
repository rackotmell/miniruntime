#pragma once

#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "dynamicthreadpool.h"
#include "eventloop.h"
#include "future.h"
#include "handle.h"
#include "sharedvalue.h"
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

            return promise->getFuture();
        }

        template<typename F, typename... Args>
        auto schedule(std::chrono::milliseconds delay, F&& f, Args&&... args)
            -> Future<std::invoke_result_t<F, Args...>>
        {
            using ResultType = std::invoke_result_t<F, Args...>;
            auto promise = std::make_shared<Promise<ResultType>>();

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
            uint64_t id;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                id = m_nextId++;
                m_timers.try_emplace(id, std::move(handle));
            }
            promise->setId(id);
            LOG_DEBUG("TaskScheduler:schedule task scheduled, id={}, delay={}", id, delay);

            return promise->getFuture();
        }

        template<typename F, typename... Args>
        auto scheduleInterval(std::chrono::milliseconds interval, F&& f, Args&&... args)
            -> std::shared_ptr<SharedValue<std::invoke_result_t<F, Args...>>>
        {
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
            uint64_t id;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                id = m_nextId++;
                m_intervals.try_emplace(id, std::move(handle));
            }
            value->setId(id);
            LOG_DEBUG("TaskScheduler::scheduleInterval interval task scheduled, id={}, interval={}", id, interval);

            return value;
        }

        void init();
        void stop();
        bool cancel(std::optional<uint64_t> id);

    private:
        DynamicThreadPool m_pool;
        EventLoop m_loop;
        std::jthread m_loopThread;
        std::unique_ptr<IntervalHandle> m_timerCleaner;
        
        std::mutex m_mutex;
        std::unordered_map<uint64_t, TimerHandle> m_timers;
        std::unordered_map<uint64_t, IntervalHandle> m_intervals;

        uint64_t m_nextId;
    };

}