#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>

#include "miniruntime/event/handle.h"

namespace miniruntime::event
{

/// Describes an event is registered for.
enum class EventType { SOCKET, TRIGGER, TIMER, USER };

/**
 * @brief Epoll-driven event reactor.
 *
 * EventLoop owns a single epoll descriptor and multiplexes three kinds
 * of fds: raw fd (from user, createEvent), eventfd triggers (createTrigger)
 * and timerfd timers (createTimer/createInterval). Each registration is
 * exposed as an RAII handle that unregisters its fd on destruction.
 *
 * run() blocks the calling thread and dispatches callbacks until stop()
 * is called from another thread. create* and stop() are thread-safe;
 * run() must be used by a single thread.
 */
class EventLoop
{
public:
    using EventCallback = std::function<void(int)>;
    using TriggerCallback = std::function<void()>;
    using TimerCallback = std::function<void()>;

    /**
     * @brief Creates the event loop and the underlying epoll descriptor.
     * @throws std::runtime_error if the epoll descriptor cannot be created.
     */
    EventLoop();
    ~EventLoop();

    // Immovable: live fd handles keep an EventLoop* pointing to
    // this object to be able unregister themself.
    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;
    EventLoop(EventLoop&&) = delete;
    EventLoop& operator=(EventLoop&&) = delete;

    /**
     * @brief Registers a raw fd to be watched for the given epoll events.
     * @param fd Descriptor to register with epoll.
     * @param epollFlags Event mask, e.g. EPOLLIN | EPOLLET.
     * @param type How the descriptor should be interpreted.
     * @param callback Called from run() when the fd becomes ready.
     * @return Handle owning the registration.
     * @throws std::runtime_error if the fd cannot be registered with epoll.
     */
    [[nodiscard]] EventHandle
    createEvent(int fd, uint32_t epollFlags, EventType type, EventCallback callback);

    /**
     * @brief Creates a manual signal using eventfd.
     * @param callback Called from run() after the trigger fires.
     * @return Handle; trigger() wakes up the event loop.
     * @throws std::runtime_error if the eventfd cannot be created or registered.
     */
    [[nodiscard]] TriggerHandle createTrigger(TriggerCallback callback);

    /**
     * @brief Creates a one-shot timer.
     * @param timeout  Delay before the callback fires.
     * @param callback Called from run() once the timer expires.
     * @return Handle; fired() reports whether the timer has expired.
     * @throws std::runtime_error if the timerfd cannot be created or registered.
     */
    [[nodiscard]] TimerHandle createTimer(std::chrono::milliseconds timeout,
                                          TimerCallback callback);

    /**
     * @brief Creates an interval timer.
     * @param interval Period between consecutive firings.
     * @param callback Called from run() on every tick.
     * @return Handle; resetInterval() can change the period later.
     * @throws std::runtime_error if the timerfd cannot be created or registered.
     */
    [[nodiscard]] IntervalHandle createInterval(std::chrono::milliseconds interval,
                                                TimerCallback callback);

    /**
     * @brief Blocks the calling thread to dispatch callbacks until stop().
     * @throws std::runtime_error if epoll_wait fails (other than EINTR).
     */
    void run();

    /**
     * @brief Requests the loop to stop; wakes run() from epoll_wait. Thread-safe.
     */
    void stop();

private:
    // Handles need these to unregister/reset their fd on destruction.
    friend class HandleBase;
    friend class EventHandle;
    friend class TriggerHandle;
    friend class TimerHandle;
    friend class IntervalHandle;

    /// Remove an fd from epoll and the registry.
    void unregisterEvent(int fd);
    /// Re-arm an interval timer with a new period.
    void resetTimerInterval(int fd, std::chrono::milliseconds interval);

    // Pimpl: hides all runtime internals
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace miniruntime::event
