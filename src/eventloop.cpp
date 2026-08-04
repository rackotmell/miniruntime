#include "eventloop.h"
#include "handle.h"
#include "logger.h"

#include <array>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <unistd.h>

namespace miniruntime
{

// itimerspec conversion helpers.
constexpr int MILLI_DIVIDER = 1000;
constexpr int NANO_DIVIDER = 1000000;

// Per-fd state kept in the event registry.
struct Event {
    int fd;
    uint32_t epollFlags;
    EventType type;
    EventLoop::EventCallback callback;
};

// Pimpl implementation of EventLoop.
struct EventLoop::Impl {
    int epollFd{-1};
    std::atomic<bool> stop{false};
    std::mutex mutex;
    std::unordered_map<int, Event> events;

    // Add the fd to epoll, or refresh its flags if already registered.
    void registerEvent(Event& event)
    {
        const auto fd = event.fd;

        struct epoll_event ev = {};
        ev.data.fd = event.fd;
        ev.events = event.epollFlags;

        if (epoll_ctl(epollFd, EPOLL_CTL_ADD, fd, &ev) == -1) {
            if (errno == EEXIST) {
                // fd already registered (e.g. re-created handle) -> MOD.
                if (epoll_ctl(epollFd, EPOLL_CTL_MOD, fd, &ev) == -1) {
                    LOG_ERROR("epol_ctl mod failed, fd={}", fd);
                    throw std::runtime_error("epol_ctl mod failed");
                }
                LOG_DEBUG("EventLoop::registerEvent event re-registered, fd={}", fd);
            } else {
                LOG_ERROR("epoll_ctl failed: {}", strerror(errno));
                throw std::runtime_error("epoll_ctl failed");
            }
        }
        events[fd] = std::move(event);
    }

    Event prepareEvent(const int fd, uint32_t epollFlags, EventType type, EventCallback callback)
    {
        return {.fd = fd, .epollFlags = epollFlags, .type = type, .callback = std::move(callback)};
    }

    void prepareIntervalSpec(itimerspec& spec, int64_t ms)
    {
        spec.it_value.tv_sec = ms / MILLI_DIVIDER;
        spec.it_value.tv_nsec = (ms % MILLI_DIVIDER) * NANO_DIVIDER;
        spec.it_interval.tv_sec = ms / MILLI_DIVIDER;
        spec.it_interval.tv_nsec = (ms % MILLI_DIVIDER) * NANO_DIVIDER;
    }
};

EventLoop::EventLoop() : m_impl(std::make_unique<Impl>())
{
    m_impl->epollFd = epoll_create1(0);
    if (m_impl->epollFd < 0) {
        LOG_ERROR("Can not create epoll");
        throw std::runtime_error("Can not create epoll");
    }
}

EventLoop::~EventLoop()
{
    stop();
    // Deregister remaining fds.
    for (auto& [fd, event] : m_impl->events) {
        if (epoll_ctl(m_impl->epollFd, EPOLL_CTL_DEL, fd, nullptr) == -1) {
            LOG_WARNING("EventLoop::~EventLoop epoll_ctl DEL failed for fd={}: {}",
                        fd, std::strerror(errno));
        }
    }
    if (m_impl->epollFd >= 0) {
        close(m_impl->epollFd);
        m_impl->epollFd = -1;
    }
}

EventHandle
EventLoop::createEvent(int fd, uint32_t epollFlags, EventType type, EventCallback callback)
{
    LOG_DEBUG("EventLoop::createEvent: fd={}, flags={:#x}", fd, epollFlags);

    Event event = m_impl->prepareEvent(fd, epollFlags, type, std::move(callback));

    std::lock_guard lock(m_impl->mutex);
    m_impl->registerEvent(event);

    return EventHandle{this, fd};
}

TriggerHandle EventLoop::createTrigger(TriggerCallback callback)
{
    const int fd = eventfd(0, EFD_NONBLOCK);
    LOG_DEBUG("EventLoop::createTrigger: fd={}", fd);

    if (fd < 0) throw std::runtime_error("eventfd error");

    // Read the eventfd so the trigger can fire again on the EventLoop iteration.
    Event event =
        m_impl->prepareEvent(fd, EPOLLIN, EventType::TRIGGER, [cb = std::move(callback)](int fd) {
            uint64_t val;
            read(fd, &val, sizeof(val));
            if (cb) cb();
        });

    std::lock_guard lock(m_impl->mutex);
    m_impl->registerEvent(event);

    return TriggerHandle{this, fd};
}

TimerHandle EventLoop::createTimer(std::chrono::milliseconds timeout, TimerCallback callback)
{
    const int fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    const auto timeoutMs = timeout.count();
    LOG_DEBUG("EventLoop::createTimer: fd={}, timeout={}ms", fd, timeoutMs);

    if (fd < 0) throw std::runtime_error("timerfd error");

    // One-shot: only it_value is armed, no it_interval.
    struct itimerspec spec{};
    spec.it_value.tv_sec = timeoutMs / MILLI_DIVIDER;
    spec.it_value.tv_nsec = (timeoutMs % MILLI_DIVIDER) * NANO_DIVIDER;

    TimerHandle timer{this, fd};

    // Flag the handle as fired.
    Event event =
        m_impl->prepareEvent(fd,
                             EPOLLIN,
                             EventType::TIMER,
                             [fired = timer.firedAccess(), cb = std::move(callback)](int fd) {
                                 uint64_t val;
                                 read(fd, &val, sizeof(val));
                                 if (cb) cb();
                                 fired->store(true, std::memory_order_release);
                             });

    std::lock_guard lock(m_impl->mutex);
    timerfd_settime(fd, 0, &spec, nullptr);
    m_impl->registerEvent(event);

    return timer;
}

IntervalHandle EventLoop::createInterval(std::chrono::milliseconds interval, TimerCallback callback)
{
    const int fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    const auto intervalMs = interval.count();
    LOG_DEBUG("EventLoop::createInterval: fd={}, timeout={}ms", fd, intervalMs);

    if (fd < 0) throw std::runtime_error("timerfd error");

    // Interval: it_value and it_interval are armed.
    struct itimerspec spec{};
    m_impl->prepareIntervalSpec(spec, intervalMs);

    Event event =
        m_impl->prepareEvent(fd, EPOLLIN, EventType::TIMER, [cb = std::move(callback)](int fd) {
            uint64_t val;
            read(fd, &val, sizeof(val));
            if (cb) cb();
        });

    std::lock_guard lock(m_impl->mutex);
    timerfd_settime(fd, 0, &spec, nullptr);
    m_impl->registerEvent(event);

    return IntervalHandle(this, fd);
}

void EventLoop::run()
{
    LOG_INFO("EventLoop started");

    // Timeout keeps the loop responsive to stop() without incoming events.
    constexpr int EPOLL_TIMEOUT = 100;
    std::array<epoll_event, 64> events;

    while (!m_impl->stop.load(std::memory_order_acquire)) {
        int n = epoll_wait(m_impl->epollFd, events.data(), events.size(), EPOLL_TIMEOUT);
        if (n < 0) {
            LOG_ERROR("epoll_wait failed: {}", strerror(errno));

            // Signal interrupted the wait; just retry.
            if (errno == EINTR) {
                continue;
            }
            throw std::runtime_error("epoll wait error");
        }

        for (int i = 0; i < n; ++i) {
            const auto fd = events[i].data.fd;
            EventCallback cb;
            {
                // Copy the callback under the lock, invoke it outside it,
                // so long callbacks do not block registrations.
                std::lock_guard<std::mutex> lock(m_impl->mutex);
                auto it = m_impl->events.find(fd);
                if (it == m_impl->events.end()) continue;
                cb = it->second.callback;
            }
            cb(fd);
        }
    }

    LOG_INFO("EventLoop stopped");
}

void EventLoop::stop()
{
    LOG_DEBUG("EventLoop::stop called");
    m_impl->stop.store(true, std::memory_order_release);
}

// Delegating wrapper for HandleBase and the handle classes.
void EventLoop::unregisterEvent(int fd)
{
    LOG_DEBUG("EventLoop::unregisterEvent: fd={}", fd);

    std::lock_guard lock(m_impl->mutex);
    if (epoll_ctl(m_impl->epollFd, EPOLL_CTL_DEL, fd, nullptr) == -1) {
        // ENOENT means the fd was already gone; not an error.
        if (errno != ENOENT) {
            LOG_WARNING("epoll_ctl del failed, fd={}", fd);
        }
    }
    m_impl->events.erase(fd);
}

// Delegating wrapper for IntervalHandle::resetInterval.
void EventLoop::resetTimerInterval(int fd, std::chrono::milliseconds interval)
{
    const auto intervalMs = interval.count();
    LOG_DEBUG("Reset timer interval: fd={}, interval={}ms", fd, intervalMs);

    struct itimerspec spec{};
    m_impl->prepareIntervalSpec(spec, intervalMs);

    std::lock_guard lock(m_impl->mutex);
    timerfd_settime(fd, 0, &spec, nullptr);
}

} // namespace miniruntime
