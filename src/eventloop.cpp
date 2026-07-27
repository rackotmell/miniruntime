#include "eventloop.h"
#include "logger.h"

#include <array>
#include <cerrno>
#include <ctime>
#include <iostream>
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
        if (m_epollFd < 0)
            std::cout << "[Error] Can not create epoll";
    }

    EventLoop::~EventLoop() 
    {
        for (auto& [fd, event] : m_events) {
            epoll_ctl(m_epollFd, EPOLL_CTL_DEL, fd, nullptr);
        }
    }

    EventHandle EventLoop::createEvent(int fd, uint32_t epollFlags, EventType type, EventCallback callback)
    {
        Event event = prepareEvent(fd, epollFlags, type, std::move(callback));
        registerEvent(event);

        return EventHandle{this, fd};
    }

    TriggerHandle EventLoop::createTrigger(TriggerCallback callback)
    {
        const int fd = eventfd(0, EFD_NONBLOCK);
        if (fd < 0)
            throw std::runtime_error("eventfd error");

        Event event = prepareEvent(fd, EPOLLIN, EventType::TRIGGER,
            [cb = std::move(callback)](int fd) {
                uint64_t val;
                read(fd, &val, sizeof(val));
                if (cb) cb();
        });
        registerEvent(event);

        return TriggerHandle{this, fd};
    }

    TimerHandle EventLoop::createTimer(std::chrono::milliseconds timeout, TimerCallback callback)
    {
        const int fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
        if (fd < 0)
            throw std::runtime_error("timerfd error");

        struct itimerspec spec{};
        spec.it_value.tv_sec = timeout.count() / MILLI_DIVIDER;
        spec.it_value.tv_nsec = (timeout.count() % MILLI_DIVIDER) / NANO_DIVIDER;

        timerfd_settime(fd, 0, &spec, nullptr);

        Event event = prepareEvent(fd, EPOLLIN, EventType::TIMER,
            [cb = std::move(callback)](int fd) {
                uint64_t val;
                read(fd, &val, sizeof(val));
                if (cb) cb();
        });
        registerEvent(event);

        return TimerHandle(this, fd);
    }

    TimerHandle EventLoop::createInterval(std::chrono::milliseconds interval, TimerCallback callback)
    {
        const int fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
        if (fd < 0)
            throw std::runtime_error("timerfd error");

        struct itimerspec spec{};
        spec.it_value.tv_sec = interval.count() / MILLI_DIVIDER;
        spec.it_value.tv_nsec = (interval.count() % MILLI_DIVIDER) / NANO_DIVIDER;
        spec.it_interval.tv_sec = interval.count() / MILLI_DIVIDER;
        spec.it_interval.tv_nsec = (interval.count() % MILLI_DIVIDER) / NANO_DIVIDER;

        timerfd_settime(fd, 0, &spec, nullptr);

        Event event = prepareEvent(fd, EPOLLIN, EventType::TIMER,
            [cb = std::move(callback)](int fd) {
                uint64_t val;
                read(fd, &val, sizeof(val));
                if (cb) cb();
        });
        registerEvent(event);

        return TimerHandle(this, fd);
    }

    void EventLoop::run()
    {
        constexpr int EPOLL_TIMEOUT = 100;
        std::array<epoll_event, 64> events;

        while (!m_stop) {
            int n = epoll_wait(m_epollFd, events.data(), events.size(), EPOLL_TIMEOUT);
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw std::runtime_error("epoll wait error");
            } 
            for (int i = 0; i < n; ++i) {
                const auto fd = events[i].data.fd;
                const auto it = m_events.find(fd);
                if (it != m_events.end()) {
                    const auto& event = it->second;
                    event.callback(fd);
                }
            }
        }
    }

    void EventLoop::stop() 
    {
        m_stop = true;
    }

    void EventLoop::unregisterEvent(int fd)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        epoll_ctl(m_epollFd, EPOLL_CTL_DEL, fd, nullptr);
    }

    void EventLoop::registerEvent(Event& event)
    {
        const auto fd = event.fd;

        struct epoll_event ev = {0};
        ev.data.fd = event.fd;
        ev.events = event.epollFlags;

        std::lock_guard<std::mutex> lock(m_mutex);
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

}