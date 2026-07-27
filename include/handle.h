# pragma once

#include <chrono>

namespace miniruntime {

    class EventLoop;
    class HandleBase {
    public:
        HandleBase(const HandleBase&) = delete;
        HandleBase& operator=(const HandleBase&) = delete;
        HandleBase(HandleBase&& handle) noexcept;
        HandleBase& operator=(HandleBase&& handle) noexcept;

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
        void cancel();

    protected:
        TimerHandle(EventLoop* loop, int fd);
    };


    class IntervalHandle : public TimerHandle {
        friend class EventLoop;

    public:
        using TimerHandle::TimerHandle;
        void resetInterval(std::chrono::milliseconds interval);
    
    private:
        IntervalHandle(EventLoop* loop, int fd);

    };

}