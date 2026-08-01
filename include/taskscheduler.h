#pragma once

#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <chrono>

#include "future.h"
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

            enqueue([
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

            uint64_t id = createTimer(delay, 
                [promise, func = std::forward<F>(f),
                ...args = std::forward<Args>(args)] {
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
                },
                [promise] {
                    promise->close();
                }
            );

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

            uint64_t id = createInterval(interval,
                [valuePtr = value, func = std::forward<F>(f),
                ...args = std::forward<Args>(args)] {
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
                }, 
                [value] {
                    value->close();
                }
            );
            value->setId(id);
            LOG_DEBUG("TaskScheduler::scheduleInterval interval task scheduled, id={}, interval={}", id, interval);

            return value;
        }

        void init();
        void stop();
        bool cancel(std::optional<uint64_t> id);

    private:
        void enqueue(std::function<void()> task);
        uint64_t createTimer(std::chrono::milliseconds &delay,
            std::function<void()> task, std::function<void()> onCancel);
        uint64_t createInterval(std::chrono::milliseconds &interval,
            std::function<void()> task, std::function<void()> onCancel);

        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };

}