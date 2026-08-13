#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <mutex>
#include <optional>
#include <utility>

namespace miniruntime::asyncresult
{

/**
 * @brief Reusable value holder for repeating tasks (scheduleInterval).
 *
 * Unlike Future (one-shot), SharedValue keeps the latest published value
 * for the lifetime of the interval. Each set() overwrites the previous
 * value; wait() blocks until a new value appears, returns it and clears
 * the slot. SharedValue is thread-safe.
 */
template <typename T> class SharedValue
{
public:
    /**
     * @brief Publishes a new value and wakes up all waiting consumers.
     * @param value New value to deliver.
     */
    void set(T value)
    {
        {
            std::lock_guard lock(m_mutex);
            m_value = std::move(value);
        }
        m_cv.notify_all();
    }

    /**
     * @brief Publishes an exception instead of a value.
     * @param ptr Exception captured by the producing task.
     */
    void setException(std::exception_ptr ptr)
    {
        {
            std::lock_guard lock(m_mutex);
            m_exception = std::move(ptr);
        }
        m_cv.notify_all();
    }

    /**
     * @brief Blocks until a new value is published or the holder is closed.
     * @return The published value, or std::nullopt if the holder was closed.
     * @throws The published exception, if one was set.
     */
    std::optional<T> wait()
    {
        std::unique_lock lock(m_mutex);
        m_cv.wait(lock, [this] { return m_value.has_value() || m_exception || m_closed; });
        if (m_closed) return std::nullopt;
        if (m_exception) std::rethrow_exception(m_exception);

        const auto result = std::move(m_value);
        m_value.reset();

        return result;
    }

    /**
     * @brief Marks the holder as closed; wait() returns std::nullopt from now on.
     */
    void close()
    {
        m_closed.store(true);
        m_cv.notify_all();
    }

    /**
     * @brief Associates the scheduler timer id with this holder.
     * @param id Identifier from TaskScheduler::scheduleInterval().
     */
    void setId(uint64_t id)
    {
        std::lock_guard lock(m_mutex);
        m_id = id;
    }

    /**
     * @brief Returns the scheduler timer id, if one was assigned.
     * @return The stored id, or std::nullopt.
     */
    std::optional<uint64_t> getId()
    {
        std::lock_guard lock(m_mutex);
        return m_id;
    }

private:
    std::optional<T> m_value;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::exception_ptr m_exception;
    std::atomic<bool> m_closed;
    std::optional<uint64_t> m_id;
};

/**
 * @brief void specialization of SharedValue: publishes completion only.
 *
 * Reuses SharedValue<bool>; set() signals completion and wait() merely
 * reports whether the next tick arrived.
 */
template <> class SharedValue<void> : SharedValue<bool>
{
    using Base = SharedValue<bool>;

public:
    using Base::close;
    using Base::getId;
    using Base::setException;
    using Base::setId;

    /**
     * @brief Marks the current tick as completed and wakes up wait().
     */
    void set() { Base::set(true); }
    /**
     * @brief Blocks until the next tick or closure.
     * @return true if a new tick arrived, false if the holder was closed.
     */
    bool wait() { return Base::wait().has_value(); }
};

} // namespace miniruntime::asyncresult