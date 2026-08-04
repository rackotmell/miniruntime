#include "taskscheduler.h"

#include <mutex>
#include <thread>

#include "dynamicthreadpool.h"
#include "eventloop.h"
#include "handle.h"

namespace miniruntime
{

// Registry entry for a one-shot timer: the event-loop handle plus a
// callback to run when the task is cancelled by the user.
struct TimerEntry {
    TimerHandle handle;
    std::function<void()> onCancel;
};

// Registry entry for a recurring interval, same layout as TimerEntry.
struct IntervalEntry {
    IntervalHandle handle;
    std::function<void()> onCancel;
};

// Pimpl implementation of TaskScheduler.
struct TaskScheduler::Impl {
    // Constructs the thread pool with the given size bounds.
    Impl(size_t minParallelTasks, size_t maxParallelTasks)
        : pool(minParallelTasks, maxParallelTasks)
    {
    }

    DynamicThreadPool pool;
    EventLoop loop;
    std::jthread loopThread;
    std::unique_ptr<IntervalHandle> timerCleaner;

    std::mutex mutex;
    std::unordered_map<uint64_t, TimerEntry> timers;
    std::unordered_map<uint64_t, IntervalEntry> intervals;

    uint64_t nextId{1};
    bool initialized{false};
};

TaskScheduler::TaskScheduler(size_t minParallelTasks, size_t maxParallelTasks)
    : m_impl(std::make_unique<Impl>(minParallelTasks, maxParallelTasks))
{
}

TaskScheduler::~TaskScheduler()
{
    // Stop the event loop and joins its thread to
    // guarantees correct destruction order.
    m_impl->loop.stop();
    if (m_impl->loopThread.joinable()) m_impl->loopThread.join();
}

void TaskScheduler::init()
{
    if (m_impl->initialized) {
        LOG_WARNING("TaskScheduler already initialized");
        return;
    }

    LOG_INFO("TaskScheduler init started");
    // Start the event loop on a dedicated thread.
    m_impl->loopThread = std::jthread([this] { m_impl->loop.run(); });

    // Install a periodic cleanup that clean already-fired or invalidated timers.
    m_impl->timerCleaner = std::make_unique<IntervalHandle>(
        m_impl->loop.createInterval(std::chrono::seconds(5), [this] {
            std::lock_guard lock(m_impl->mutex);
            auto expiredTimersCount = m_impl->timers.size();

            // Remove timers that have already fired or were invalidated.
            std::erase_if(m_impl->timers, [](auto& pair) {
                return pair.second.handle.fired() || !pair.second.handle.valid();
            });
            expiredTimersCount -= m_impl->timers.size();
            LOG_INFO("TaskScheduler clean up expired timers, removed={}", expiredTimersCount);
        }));

    m_impl->initialized = true;
    LOG_INFO("TaskScheduler init finished");
}

void TaskScheduler::stop() { m_impl->loop.stop(); }

bool TaskScheduler::cancel(std::optional<uint64_t> id)
{
    if (!id) return false;

    std::function<void()> onCancel;
    bool found = false;
    {
        std::lock_guard lock(m_impl->mutex);
        auto idValue = id.value();
        if (auto it = m_impl->timers.find(idValue); it != m_impl->timers.end()) {
            LOG_DEBUG("TaskScheduler::cancel manualy cancel timer with id={}", idValue);
            onCancel = std::move(it->second.onCancel);
            m_impl->timers.erase(it);
            found = true;
        } else if (auto it = m_impl->intervals.find(idValue); it != m_impl->intervals.end()) {
            LOG_DEBUG("TaskScheduler::cancel manualy cancel interval with id={}", idValue);
            onCancel = std::move(it->second.onCancel);
            m_impl->intervals.erase(it);
            found = true;
        }
    }
    if (found && onCancel) onCancel();
    return found;
}

void TaskScheduler::enqueue(std::function<void()> task) { m_impl->pool.enqueue(std::move(task)); }

uint64_t TaskScheduler::createTimer(std::chrono::milliseconds& delay,
                                    std::function<void()> task,
                                    std::function<void()> onCancel)
{
    TimerHandle handle = m_impl->loop.createTimer(
        delay,
        // When the timer fires, dispatch the task to the pool.
        [this, tpTask = std::move(task)] { m_impl->pool.enqueue(std::move(tpTask)); });

    uint64_t id;
    {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        id = m_impl->nextId++;
        m_impl->timers.try_emplace(id, TimerEntry{std::move(handle), std::move(onCancel)});
    }
    return id;
}

// Bridge: same as createTimer but for a recurring interval.
uint64_t TaskScheduler::createInterval(std::chrono::milliseconds& interval,
                                       std::function<void()> task,
                                       std::function<void()> onCancel)
{
    IntervalHandle handle = m_impl->loop.createInterval(
        interval,
        // On every tick, re-dispatch the task to the pool.
        [this, tpTask = std::move(task)] { m_impl->pool.enqueue(std::move(tpTask)); });

    uint64_t id;
    {
        std::lock_guard lock(m_impl->mutex);
        id = m_impl->nextId++;
        m_impl->intervals.try_emplace(id, IntervalEntry{std::move(handle), std::move(onCancel)});
    }
    return id;
}

} // namespace miniruntime
