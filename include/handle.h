# pragma once

#include <atomic>
#include <chrono>
#include <memory>

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
        HandleBase(EventLoop* loop, int fd, bool ownFd);

        void release();

        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };


    class EventHandle : public HandleBase {
        friend class EventLoop;
    private:
        EventHandle(EventLoop* loop, int fd);
    };


    class TriggerHandle : public HandleBase {
        friend class EventLoop;
    public:
        void trigger() const;

    private:
        TriggerHandle(EventLoop* loop, int fd);
    };


    class TimerHandle : public HandleBase {
        friend class EventLoop;
    public:
        void cancel();
        bool fired() const;
    
    private:
        TimerHandle(EventLoop* loop, int fd);
        std::shared_ptr<std::atomic<bool>> firedAccess();
    };


    class IntervalHandle : public HandleBase {
        friend class EventLoop;
    public:
        void cancel();
        void resetInterval(std::chrono::milliseconds interval);
    
    private:
        IntervalHandle(EventLoop* loop, int fd);
    };

}