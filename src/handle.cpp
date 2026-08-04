#include "handle.h"

#include <atomic>
#include <cstring>
#include <memory>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>

#include "eventloop.h"
#include "logger.h"


namespace miniruntime
{

// Per-handle state: the owning loop, the wrapped fd, whether the fd should
// be closed on release, and an optional shared fired flag (timers only).
struct HandleBase::Impl {
    EventLoop* loop;
    int fd;
    bool ownFd;
    std::shared_ptr<std::atomic<bool>> fired;
};

HandleBase::HandleBase(HandleBase&& handle) noexcept : m_impl(std::exchange(handle.m_impl, nullptr))
{
}

HandleBase& HandleBase::operator=(HandleBase&& handle) noexcept
{
    if (this != &handle) {
        release();
        m_impl = std::exchange(handle.m_impl, nullptr);
    }
    return *this;
}

HandleBase::HandleBase(EventLoop* loop, int fd, bool ownFd)
    : m_impl(std::make_unique<Impl>(Impl{loop, fd, ownFd, nullptr}))
{
}

HandleBase::~HandleBase() { release(); }

bool HandleBase::valid() const { return m_impl && m_impl->loop && m_impl->fd >= 0; }

void HandleBase::release()
{
    if (!valid()) return;

    m_impl->loop->unregisterEvent(m_impl->fd);
    if (m_impl->ownFd) close(m_impl->fd);
    m_impl->fd = -1;
}

// EventHandle never owns the fd: the caller keeps ownership.
EventHandle::EventHandle(EventLoop* loop, int fd) : HandleBase(loop, fd, false) {}

// TriggerHandle owns its eventfd.
TriggerHandle::TriggerHandle(EventLoop* loop, int fd) : HandleBase(loop, fd, true) {}

void TriggerHandle::trigger() const
{
    if (!valid()) {
        LOG_WARNING("TriggerHandle::trigger on invalid handle");
        return;
    }
    LOG_DEBUG("TriggerHandle::trigger on fd={}", m_impl->fd);

    uint64_t one = 1;
    // Writing 1 to the eventfd makes the loop's epoll_wait wake.
    const ssize_t written = write(m_impl->fd, &one, sizeof(one));
    if (written != static_cast<ssize_t>(sizeof(one))) {
        LOG_WARNING(
            "TriggerHandle::trigger write failed on fd={}: {}", m_impl->fd, std::strerror(errno));
    }
}

TimerHandle::TimerHandle(EventLoop* loop, int fd) : HandleBase(loop, fd, true)
{
    // The fired flag is shared with the loop so it can be set
    // from event-loop.
    m_impl->fired = std::make_shared<std::atomic<bool>>(false);
}

void TimerHandle::cancel()
{
    if (valid())
        LOG_DEBUG("TimerHandle::cancel on fd={}", m_impl->fd);
    else
        LOG_WARNING("TimerHandle::cancel on invalid handle");

    release();
}

bool TimerHandle::fired() const { return m_impl->fired->load(std::memory_order_acquire); }

std::shared_ptr<std::atomic<bool>> TimerHandle::firedAccess() { return m_impl->fired; }

// IntervalHandle owns its timerfd.
IntervalHandle::IntervalHandle(EventLoop* loop, int fd) : HandleBase(loop, fd, true) {}

void IntervalHandle::cancel()
{
    if (valid())
        LOG_DEBUG("IntervalHandle::cancel on fd={}", m_impl->fd);
    else
        LOG_WARNING("IntervalHandle::cancel on invalid handle");

    release();
}

void IntervalHandle::resetInterval(std::chrono::milliseconds interval)
{
    if (!valid()) {
        LOG_WARNING("IntervalHandle::resetInterval on invalid handle");
        return;
    }

    // Re-arm the timerfd with the new period on the loop.
    m_impl->loop->resetTimerInterval(m_impl->fd, interval);
}
} // namespace miniruntime