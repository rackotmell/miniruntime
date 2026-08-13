#pragma once

#include <atomic>
#include <chrono>
#include <memory>

namespace miniruntime::event
{

class EventLoop;

/**
 * @brief RAII owner of a single event-loop registration.
 *
 * HandleBase is the common base of all handle types. It wraps an fd
 * registered in an EventLoop and guarantees that the fd is unregistered
 * (and optionally closed) when the handle is destroyed, moved or released.
 * Instances are move-only.
 */
class HandleBase
{
public:
    HandleBase(const HandleBase&) = delete;
    HandleBase& operator=(const HandleBase&) = delete;
    HandleBase(HandleBase&& handle) noexcept;
    HandleBase& operator=(HandleBase&& handle) noexcept;

    virtual ~HandleBase();

    /**
     * @brief Reports whether the handle still owns a live registration.
     * @return true if the handle is valid and its fd is registered in the loop.
     */
    bool valid() const;

protected:
    // Subclasses (via EventLoop friendship) pass the loop, the fd to own
    // and whether the fd should be closed on release.
    HandleBase(EventLoop* loop, int fd, bool ownFd);

    /// Unregister the fd from the loop and mark the handle invalid.
    void release();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

/**
 * @brief Handle for a raw user fd (EventLoop::createEvent).
 *
 * The fd is owned by the caller, so the handle only manages the
 * epoll registration and never closes the fd.
 */
class EventHandle : public HandleBase
{
    friend class EventLoop;

private:
    // Only EventLoop is allowed to construct it.
    EventHandle(EventLoop* loop, int fd);
};

/**
 * @brief Handle for a manual signal (EventLoop::createTrigger).
 *
 * trigger() writes to the underlying eventfd, waking up the event loop
 * from another thread.
 */
class TriggerHandle : public HandleBase
{
    friend class EventLoop;

public:
    /**
     * @brief Fires the trigger, waking up the event loop. Thread-safe.
     */
    void trigger() const;

private:
    // Only EventLoop is allowed to construct it.
    TriggerHandle(EventLoop* loop, int fd);
};

/**
 * @brief Handle for a one-shot timer (EventLoop::createTimer).
 *
 * fired() reports whether the timer has expired; cancel() lets the user
 * abort it before it fires.
 */
class TimerHandle : public HandleBase
{
    friend class EventLoop;

public:
    /**
     * @brief Aborts the pending timer and releases the registration.
     */
    void cancel();

    /**
     * @brief Reports whether the timer has fired. Thread-safe.
     * @return true if the timer fired.
     */
    bool fired() const;

private:
    // Only EventLoop is allowed to construct it.
    TimerHandle(EventLoop* loop, int fd);

    // Exposes the shared fired flag to EventLoop so it can flip it on fire.
    std::shared_ptr<std::atomic<bool>> firedAccess();
};

/**
 * @brief Handle for a recurring timer (EventLoop::createInterval).
 *
 * cancel() stops the interval; resetInterval() changes its period on the fly.
 */
class IntervalHandle : public HandleBase
{
    friend class EventLoop;

public:
    /**
     * @brief Stops the interval and releases the registration.
     */
    void cancel();

    /**
     * @brief Changes the interval period. The new period applies immediately.
     * @param interval New period.
     */
    void resetInterval(std::chrono::milliseconds interval);

private:
    // Only EventLoop is allowed to construct it.
    IntervalHandle(EventLoop* loop, int fd);
};

} // namespace miniruntime::event