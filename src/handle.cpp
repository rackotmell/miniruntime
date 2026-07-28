#include "handle.h"

#include <atomic>
#include <memory>
#include <sys/types.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <utility>

#include "eventloop.h"
#include "logger.h"


namespace miniruntime {

    HandleBase::HandleBase(HandleBase&& handle) noexcept
        : m_loop(std::exchange(handle.m_loop, nullptr))
        , m_fd(std::exchange(handle.m_fd, -1))
        , m_ownFd(std::exchange(handle.m_ownFd, false))
    {}

    HandleBase& HandleBase::operator=(HandleBase&& handle) noexcept
    {
        m_loop = std::exchange(handle.m_loop, nullptr);
        m_fd = std::exchange(handle.m_fd, -1);
        m_ownFd = std::exchange(handle.m_ownFd, false);

        return *this;
    }

    HandleBase::HandleBase(EventLoop* loop, int fd, bool ownFd)
        : m_loop(loop), m_fd(fd), m_ownFd(ownFd)
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

    EventHandle::EventHandle(EventLoop* loop, int fd) : HandleBase(loop, fd, false)
    {}

    TriggerHandle::TriggerHandle(EventLoop* loop, int fd) : HandleBase(loop, fd, true)
    {}

    void TriggerHandle::trigger() const
    {
        LOG_DEBUG("TriggerHandle::trigger on fd={}", m_fd);

        uint64_t one = 1;
        write(m_fd, &one, sizeof(one));
    }

    TimerHandle::TimerHandle(EventLoop* loop, int fd)
        : HandleBase(loop, fd, true)
        , m_fired(std::make_shared<std::atomic<bool>>(false))
    {
        m_ownFd = true;
    }

    void TimerHandle::cancel()
    {
        LOG_DEBUG("TimerHandle::cancel on fd={}", m_fd);
        
        if (!valid())
            return;

        m_loop->unregisterEvent(m_fd);
        close(m_fd);
        m_fd = -1;
    }

    bool TimerHandle::fired()
    {
        return m_fired->load();
    }

    IntervalHandle::IntervalHandle(EventLoop* loop, int fd) : HandleBase(loop, fd, true)
    {}

    void IntervalHandle::cancel()
    {
        LOG_DEBUG("IntervalHandle::cancel on fd={}", m_fd);
        
        if (!valid())
            return;

        m_loop->unregisterEvent(m_fd);
        close(m_fd);
        m_fd = -1;
    }

    void IntervalHandle::resetInterval(std::chrono::milliseconds interval)
    {
        if (!valid())
            return;

        m_loop->resetTimerInterval(m_fd, interval);
    }
}