#pragma once

#include "miniruntime/asyncresult/detail/future.h"

namespace miniruntime::asyncresult
{

template <typename T> class Promise;

/**
 * @brief Receiving side of a Promise/Future pair.
 *
 * Future shares the result state owned by a Promise and lets the consumer
 * wait for or poll the task outcome. get() blocks until a value or an
 * exception is published, and re-throws any captured exception. The
 * underlying state is shared via shared_ptr, so futures are copyable.
 */
template <typename T> class Future : public detail::FutureBase<detail::FutureState<T>>
{
    friend class Promise<T>;

public:
    using detail::FutureBase<detail::FutureState<T>>::FutureBase;

    /**
     * @brief Blocks until the result is available.
     * @return The task result, or std::nullopt if the future was closed.
     * @throws The task exception, if the promise captured one.
     */
    std::optional<T> get()
    {
        this->m_state->status.wait(detail::FutureStatus::Pending, std::memory_order_acquire);

        if (this->m_state->status.load(std::memory_order_acquire) == detail::FutureStatus::Closed)
            return std::nullopt;

        auto& result = this->m_state->result;
        if (std::holds_alternative<std::exception_ptr>(result))
            std::rethrow_exception(std::get<std::exception_ptr>(result));

        return std::get<T>(result);
    }
};

/**
 * @brief void specialization of Future: get() returns nothing.
 */
template <> class Future<void> : public detail::FutureBase<detail::FutureState<void>>
{
    friend class Promise<void>;

public:
    using detail::FutureBase<detail::FutureState<void>>::FutureBase;

    /**
     * @brief Blocks until the task completes; re-throws the task exception if any.
     */
    void get()
    {
        this->m_state->status.wait(detail::FutureStatus::Pending, std::memory_order_acquire);

        if (this->m_state->status.load(std::memory_order_acquire) == detail::FutureStatus::Closed)
            return;

        auto& result = this->m_state->result;
        if (std::holds_alternative<std::exception_ptr>(result))
            std::rethrow_exception(std::get<std::exception_ptr>(result));
    }
};

/**
 * @brief Producing side of a Promise/Future pair.
 *
 * Promise lets a task publish its result or exception; the consumer gets
 * access through getFuture(). setValue()/setException() are one-shot and
 * wake up every thread blocked on the matching Future::get().
 */
template <typename T> class Promise : public detail::PromiseBase<detail::FutureState<T>>
{
public:
    /**
     * @brief Publishes the result and unblocks all waiting futures.
     * @param value Result to deliver to the consumer.
     */
    void setValue(T value)
    {
        // One-shot flag: ignore duplicate deliveries.
        if (!this->claim(this->m_state->blocked)) {
            LOG_WARNING("Promise<T> value already setted up");
            return;
        }
        this->m_state->result.template emplace<T>(std::move(value));
        this->m_state->status.store(detail::FutureStatus::Ready, std::memory_order_release);
        this->m_state->status.notify_all();
    }

    /**
     * @brief Creates a Future sharing this promise's result state.
     * @return Future linked to this promise.
     */
    Future<T> getFuture() { return Future<T>(this->m_state); }
};

/**
 * @brief void specialization of Promise: publishes completion only.
 */
template <> class Promise<void> : public detail::PromiseBase<detail::FutureState<void>>
{
public:
    /**
     * @brief Marks the task as completed and unblocks all waiting futures.
     */
    void setValue()
    {
        if (!this->claim(this->m_state->blocked)) {
            LOG_WARNING("Promise<void> value already setted up");
            return;
        }
        this->m_state->status.store(detail::FutureStatus::Ready, std::memory_order_release);
        this->m_state->status.notify_all();
    }

    /**
     * @brief Creates a Future sharing this promise's result state.
     * @return Future linked to this promise.
     */
    Future<void> getFuture() { return Future<void>(this->m_state); }
};

} // namespace miniruntime::asyncresult