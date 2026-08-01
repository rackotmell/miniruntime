# pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>

#include "handle.h"

namespace miniruntime {

    enum class EventType {
        SOCKET,
        TRIGGER,
        TIMER,
    };

    class EventLoop {
    public:
        using EventCallback = std::function<void(int)>;
        using TriggerCallback = std::function<void()>;
        using TimerCallback = std::function<void()>;

        EventLoop();
        ~EventLoop();

        EventLoop(const EventLoop&) = delete;
        EventLoop& operator=(const EventLoop&) = delete;
        EventLoop(EventLoop&&) = delete;
        EventLoop& operator=(EventLoop&&) = delete;

        [[nodiscard]] EventHandle createEvent(
            int fd,
            uint32_t epollFlags,
            EventType type,
            EventCallback callback
        );
        [[nodiscard]] TriggerHandle createTrigger(TriggerCallback callback);
        [[nodiscard]] TimerHandle createTimer(
            std::chrono::milliseconds timeout, 
            TimerCallback callback
        );
        [[nodiscard]] IntervalHandle createInterval(
            std::chrono::milliseconds interval, 
            TimerCallback callback
        );

        void run();
        void stop();

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;

        friend class HandleBase;
        friend class EventHandle;
        friend class TriggerHandle;
        friend class TimerHandle;
        friend class IntervalHandle;
        void unregisterEvent(int fd);
        void resetTimerInterval(int fd, std::chrono::milliseconds interval);
    };

}