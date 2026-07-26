#include "eventloop.h"

#include <sys/types.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <utility>


namespace miniruntime {

    HandleBase::HandleBase(HandleBase&& handle)
        : m_loop(std::exchange(handle.m_loop, nullptr))
        , m_fd(std::exchange(handle.m_fd, -1))
        , m_ownFd(std::exchange(handle.m_ownFd, false))
    {}

    HandleBase& HandleBase::operator=(HandleBase&& handle)
    {
        m_loop = std::exchange(handle.m_loop, nullptr);
        m_fd = std::exchange(handle.m_fd, -1);
        m_ownFd = std::exchange(handle.m_ownFd, false);

        return *this;
    }

    HandleBase::HandleBase(EventLoop* loop, int fd)
        : m_loop(loop), m_fd(fd), m_ownFd(false)
    {}

    HandleBase::~HandleBase()
    {
        if (valid()) {
            m_loop->unregisterEvent(m_fd);
            if (m_ownFd)
                close(m_fd);
        }
    }

    bool HandleBase::valid() const
    {
        return m_loop && m_fd >= 0;
    }

    EventHandle::EventHandle(EventLoop* loop, int fd) : HandleBase(loop, fd)
    { }

    TriggerHandle::TriggerHandle(EventLoop* loop, int fd) : HandleBase(loop, fd)
    {
        m_ownFd = true;
    }

    void TriggerHandle::trigger() const
    {
        uint64_t one = 1;
        write(m_fd, &one, sizeof(one));
    }

    TimerHandle::TimerHandle(EventLoop* loop, int fd) : HandleBase(loop, fd)
    {
        m_ownFd = true;
    }

    void TimerHandle::resetInterval(std::chrono::milliseconds interval)
    {
        if (!valid())
            return;

        m_interval = interval;
        struct itimerspec spec{};

        spec.it_interval.tv_sec = interval.count() / MILLI_DIVIDER;
        spec.it_interval.tv_nsec = (interval.count() % MILLI_DIVIDER) / NANO_DIVIDER;

        timerfd_settime(m_fd, 0, &spec, nullptr);
    }

    void TimerHandle::cancel()
    {
        if (!valid())
            return;

        m_loop->unregisterEvent(m_fd);
    }
}