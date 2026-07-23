#include "eventloop.h"

#include <array>
#include <cerrno>
#include <iostream>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/eventfd.h>
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
        Event event;
        event.fd = fd;
        event.epollFlags = epollFlags;
        event.type = type;
        event.callback = callback;

        registerEvent(event);

        return EventHandle{this, fd};
    }

    TriggerHandle EventLoop::createTrigger(TriggerCallback callback)
    {
        const int fd = eventfd(0, EFD_NONBLOCK);
        if (fd < 0)
            throw std::runtime_error("eventfd error");

        Event event;
        event.fd = fd;
        event.epollFlags = EPOLLIN;
        event.type = EventType::TRIGGER;
        event.callback = [cb = std::move(callback)](int fd){
            uint64_t val;
            read(fd, &val, sizeof(val));
            if (cb) cb();
        };

        registerEvent(event);

        return TriggerHandle{this, fd};
    }

    void EventLoop::run()
    {
        std::array<epoll_event, 64> events;

        while (!m_stop) {
            int n = epoll_wait(m_epollFd, events.data(), events.size(), -1);
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw std::runtime_error("epoll wait error");
            } 
            for (const auto& epollEvent : events) {
                const auto it = m_events.find(epollEvent.data.fd);
                if (it != m_events.end()) {
                    const auto& event = it->second;
                    event.callback(epollEvent.data.fd);
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
        std::lock_guard<std::mutex> lock(m_registerMutex);
        epoll_ctl(m_epollFd, EPOLL_CTL_DEL, fd, nullptr);
    }

    void EventLoop::registerEvent(Event& event)
    {
        const auto fd = event.fd;

        struct epoll_event ev = {0};
        ev.data.fd = event.fd;
        ev.events = event.epollFlags;

        std::lock_guard<std::mutex> lock(m_registerMutex);
        epoll_ctl(m_epollFd, EPOLL_CTL_ADD, fd, &ev);
        m_events[fd] = std::move(event);
    }

}