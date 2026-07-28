#include "taskscheduler.h"
#include "handle.h"

#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <thread>

namespace miniruntime {

    TaskScheduler::TaskScheduler(size_t minParallelTasks, size_t maxParallelTasks)
        : m_pool(minParallelTasks, maxParallelTasks)
    {}

    void TaskScheduler::init()
    {
        LOG_INFO("TaskScheduler init started");
        m_loopThread = std::jthread([this] { m_loop.run(); });

        m_timerCleaner = std::make_unique<IntervalHandle>(m_loop.createInterval(
            std::chrono::seconds(5),
            [this] {
                {
                    std::lock_guard lock(m_timerMutex);
                    auto expiredTimersCount = m_timers.size();

                    m_timers.remove_if([](TimerHandle& timer) {
                        return timer.fired();
                    });
                    expiredTimersCount -= m_timers.size();
                    LOG_INFO("TaskScheduler clean up expired timers, removed={}", expiredTimersCount);
                }
                {
                    std::lock_guard lock(m_intervalMutex);
                    auto expiredintervalCount = m_intervals.size();

                    m_intervals.remove_if([](IntervalHandle& interval) {
                        return !interval.valid();
                    });
                    expiredintervalCount -= m_intervals.size();
                    LOG_INFO("TaskScheduler clean up expired intervals, removed={}", expiredintervalCount);

                }
            }
        ));
        LOG_INFO("TaskScheduler init finished");
    }

    void TaskScheduler::stop()
    {
        m_loop.stop();
    }

}