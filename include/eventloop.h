# pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_map>

#include "handle.h"

namespace miniruntime {

    enum class EventType {
        SOCKET,
        TRIGGER,
        TIMER,
    };

    constexpr int MILLI_DIVIDER = 1000;
    constexpr int NANO_DIVIDER = 1000000;

    class EventLoop {
        using EventCallback = std::function<void(int)>;
        using TriggerCallback = std::function<void()>;
        using TimerCallback = std::function<void()>;

    public:
        EventLoop();
        ~EventLoop();

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
        [[nodiscard]] TimerHandle createInterval(
            std::chrono::milliseconds interval, 
            TimerCallback callback
        );

        void run();
        void stop();

    private:
        int m_epollFd;
        std::atomic<bool> m_stop;
        std::mutex m_mutex;

        struct Event {
            int fd;
            uint32_t epollFlags;
            EventType type;
            EventCallback callback;
        };
        std::unordered_map<int, Event> m_events;

        friend class HandleBase;
        friend class EventHandle;
        friend class TriggerHandle;
        friend class TimerHandle;
        void unregisterEvent(int fd);
        void registerEvent(Event& event);
        Event prepareEvent(
            const int fd,
            uint32_t epollFlags,
            EventType type,
            EventCallback callback
        );
        void resetTimerInterval(int fd, std::chrono::milliseconds interval);
    };

}