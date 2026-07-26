# pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace miniruntime {

    enum class EventType {
        SOCKET,
        TRIGGER,
        TIMER,
    };

    class EventLoop;
    class HandleBase {
    public:
        HandleBase(const HandleBase&) = delete;
        HandleBase& operator=(const HandleBase&) = delete;
        HandleBase(HandleBase&& handle);
        HandleBase& operator=(HandleBase&& handle);

        virtual ~HandleBase();

        bool valid() const;

    protected:
        EventLoop* m_loop;
        int m_fd;
        bool m_ownFd;

        HandleBase(EventLoop* loop, int fd);
    };


    class EventHandle : public HandleBase {
        friend class EventLoop;

    public:
        using HandleBase::HandleBase;

    private:
        EventHandle(EventLoop* loop, int fd);
    };


    class TriggerHandle : public HandleBase {
        friend class EventLoop;

    public:
        using HandleBase::HandleBase;
        void trigger() const;

    private:
        TriggerHandle(EventLoop* loop, int fd);
    };


    class TimerHandle : public HandleBase {
        friend class EventLoop;

    public:
        using HandleBase::HandleBase;
        void resetInterval(std::chrono::milliseconds interval);
        void cancel();

    private:
        TimerHandle(EventLoop* loop, int fd);

        std::chrono::milliseconds m_interval;
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
        std::unique_ptr<TriggerHandle> m_closeTrigger;

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
    };

}