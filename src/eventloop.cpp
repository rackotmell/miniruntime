#include "eventloop.h"
#include "handle.h"
#include "logger.h"

#include <array>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <unistd.h>

namespace miniruntime {

    EventLoop::EventLoop() : m_stop{false}
    {
        m_epollFd = epoll_create1(0);
        if (m_epollFd < 0) {
            LOG_ERROR("Can not create epoll");
            throw std::runtime_error("Can not create epoll");
        }
    }

    EventLoop::~EventLoop() 
    {
        for (auto& [fd, event] : m_events) {
            epoll_ctl(m_epollFd, EPOLL_CTL_DEL, fd, nullptr);
        }
    }

    EventHandle EventLoop::createEvent(int fd, uint32_t epollFlags, EventType type, EventCallback callback)
    {
        LOG_DEBUG("EventLoop::createEvent: fd={}, flags={:#x}", fd, epollFlags);

        Event event = prepareEvent(fd, epollFlags, type, std::move(callback));

        std::lock_guard<std::mutex> lock(m_mutex);
        registerEvent(event);

        return EventHandle{this, fd};
    }

    TriggerHandle EventLoop::createTrigger(TriggerCallback callback)
    {
        const int fd = eventfd(0, EFD_NONBLOCK);
        LOG_DEBUG("EventLoop::createTrigger: fd={}", fd);

        if (fd < 0)
            throw std::runtime_error("eventfd error");

        Event event = prepareEvent(fd, EPOLLIN, EventType::TRIGGER,
            [cb = std::move(callback)](int fd) {
                uint64_t val;
                read(fd, &val, sizeof(val));
                if (cb) cb();
        });

        std::lock_guard<std::mutex> lock(m_mutex);
        registerEvent(event);

        return TriggerHandle{this, fd};
    }

    TimerHandle EventLoop::createTimer(std::chrono::milliseconds timeout, TimerCallback callback)
    {
        const int fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
        const auto timeoutMs = timeout.count();
        LOG_DEBUG("EventLoop::createTimer: fd={}, timeout={}ms", fd, timeoutMs);

        if (fd < 0)
            throw std::runtime_error("timerfd error");

        struct itimerspec spec{};
        spec.it_value.tv_sec = timeoutMs / MILLI_DIVIDER;
        spec.it_value.tv_nsec = (timeoutMs % MILLI_DIVIDER) / NANO_DIVIDER;

        TimerHandle timer{this, fd};

        Event event = prepareEvent(fd, EPOLLIN, EventType::TIMER,
            [fired = timer.m_fired, cb = std::move(callback)](int fd) {
                uint64_t val;
                read(fd, &val, sizeof(val));
                if (cb) cb();
                fired->store(true);
        });

        std::lock_guard<std::mutex> lock(m_mutex);
        timerfd_settime(fd, 0, &spec, nullptr);
        registerEvent(event);

        return TimerHandle(this, fd);
    }

    IntervalHandle EventLoop::createInterval(std::chrono::milliseconds interval, TimerCallback callback)
    {
        const int fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
        const auto intervalMs = interval.count();
        LOG_DEBUG("EventLoop::createInterval: fd={}, timeout={}ms", fd, intervalMs);

        if (fd < 0)
            throw std::runtime_error("timerfd error");

        struct itimerspec spec{};
        spec.it_value.tv_sec = intervalMs / MILLI_DIVIDER;
        spec.it_value.tv_nsec = (intervalMs % MILLI_DIVIDER) / NANO_DIVIDER;
        spec.it_interval.tv_sec = intervalMs / MILLI_DIVIDER;
        spec.it_interval.tv_nsec = (intervalMs % MILLI_DIVIDER) / NANO_DIVIDER;

        Event event = prepareEvent(fd, EPOLLIN, EventType::TIMER,
            [cb = std::move(callback)](int fd) {
                uint64_t val;
                read(fd, &val, sizeof(val));
                if (cb) cb();
        });

        std::lock_guard<std::mutex> lock(m_mutex);
        timerfd_settime(fd, 0, &spec, nullptr);
        registerEvent(event);

        return IntervalHandle(this, fd);
    }

    void EventLoop::run()
    {
        LOG_INFO("EventLoop started");

        constexpr int EPOLL_TIMEOUT = 100;
        std::array<epoll_event, 64> events;

        while (!m_stop) {
            int n = epoll_wait(m_epollFd, events.data(), events.size(), EPOLL_TIMEOUT);
            if (n < 0) {
                const char* errorStr = strerror(errno);
                LOG_ERROR("epoll_wait failed: {}", errorStr);

                if (errno == EINTR) {
                    continue;
                }
                throw std::runtime_error("epoll wait error");
            } 

            LOG_DEBUG("epoll_wait return {} events", n);

            for (int i = 0; i < n; ++i) {
                const auto fd = events[i].data.fd;
                const auto it = m_events.find(fd);
                if (it != m_events.end()) {
                    const auto& event = it->second;
                    event.callback(fd);
                }
            }
        }

        LOG_INFO("EventLoop stopped");
    }

    void EventLoop::stop() 
    {
        LOG_DEBUG("EventLoop::stop called");
        m_stop = true;
    }

    void EventLoop::unregisterEvent(int fd)
    {
        LOG_DEBUG("EventLoop::unregisterEvent: fd={}", fd);

        std::lock_guard<std::mutex> lock(m_mutex);
        epoll_ctl(m_epollFd, EPOLL_CTL_DEL, fd, nullptr);
        m_events.erase(fd);
    }

    void EventLoop::registerEvent(Event& event)
    {
        const auto fd = event.fd;

        struct epoll_event ev = {0};
        ev.data.fd = event.fd;
        ev.events = event.epollFlags;

        epoll_ctl(m_epollFd, EPOLL_CTL_ADD, fd, &ev);
        m_events[fd] = std::move(event);
    }

    EventLoop::Event EventLoop::prepareEvent(
        int fd,
        uint32_t epollFlags,
        EventType type,
        EventCallback callback
    ) {
        return {
            .fd = fd,
            .epollFlags = epollFlags,
            .type = type,
            .callback = std::move(callback)
        };
    }

    void EventLoop::resetTimerInterval(int fd, std::chrono::milliseconds interval)
    {
        const auto intervalMs = interval.count();
        LOG_DEBUG("Reset timer interval: fd={}, interval={}ms", fd, intervalMs);

        struct itimerspec spec{};

        spec.it_value.tv_sec = intervalMs / MILLI_DIVIDER;
        spec.it_value.tv_nsec = (intervalMs % MILLI_DIVIDER) / NANO_DIVIDER;
        spec.it_interval.tv_sec = intervalMs / MILLI_DIVIDER;
        spec.it_interval.tv_nsec = (intervalMs % MILLI_DIVIDER) / NANO_DIVIDER;

        std::lock_guard<std::mutex> lock(m_mutex);
        timerfd_settime(fd, 0, &spec, nullptr);
    }

}