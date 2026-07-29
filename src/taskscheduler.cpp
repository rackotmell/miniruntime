#include "taskscheduler.h"
#include "handle.h"
#include "logger.h"

#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <thread>

namespace miniruntime {

    TaskScheduler::TaskScheduler(size_t minParallelTasks, size_t maxParallelTasks)
        : m_pool(minParallelTasks, maxParallelTasks)
        , m_nextId(1)
    {}

    TaskScheduler::~TaskScheduler()
    {
        m_loopThread.join();
    }

    void TaskScheduler::init()
    {
        LOG_INFO("TaskScheduler init started");
        m_loopThread = std::jthread([this] { m_loop.run(); });

        m_timerCleaner = std::make_unique<IntervalHandle>(m_loop.createInterval(
            std::chrono::seconds(5),
            [this] {
                {
                    std::lock_guard lock(m_mutex);
                    auto expiredTimersCount = m_timers.size();

                    std::erase_if(m_timers, [](auto& pair) {
                        return pair.second.fired() || !pair.second.valid();
                    });
                    expiredTimersCount -= m_timers.size();
                    LOG_INFO("TaskScheduler clean up expired timers, removed={}", expiredTimersCount);
                }
            }
        ));
        LOG_INFO("TaskScheduler init finished");
    }

    void TaskScheduler::stop()
    {
        m_loop.stop();
    }

    bool TaskScheduler::cancel(std::optional<uint64_t> id)
    {
        if (!id)
            return false;

        auto idValue = id.value();
        std::lock_guard lock(m_mutex);

        auto timerIt = m_timers.find(idValue);
        if (timerIt != m_timers.end()) {
            LOG_DEBUG("TaskScheduler::cancel manualy cancel timer with id={}", idValue);
            m_timers.erase(timerIt);
            return true;
        }
        auto intervalIt = m_intervals.find(idValue);
        if (intervalIt != m_intervals.end()) {
            LOG_DEBUG("TaskScheduler::cancel manualy cancel interval with id={}", idValue);
            m_intervals.erase(intervalIt);
            return true;
        }
        return false;
    }

}