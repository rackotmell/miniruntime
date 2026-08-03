#pragma once

// =============================================================
// Internal Future/Promise implementation. NOT a public API.
// Do not include directly — use "future.h".
// =============================================================


#include <atomic>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <type_traits>
#include <variant>

#include "../logger.h"

namespace miniruntime::detail
{

// Result state: pending until a value/exception is set,
// closed when the producer gives up without a result.
enum class FutureStatus { Pending, Ready, Closed };

// Shared state common to all result types.
struct FutureStateBase {
    std::atomic<FutureStatus> status{FutureStatus::Pending};
    std::atomic<uint64_t> id;
    std::atomic<bool> idSetted{false};
    std::atomic<bool> blocked{false};
};

// Adds the result storage; T specialization stores a value or an exception.
template <typename T> struct FutureState : FutureStateBase {
    std::variant<std::monostate, T, std::exception_ptr> result;
};

// void has no value alternative.
template <> struct FutureState<void> : FutureStateBase {
    std::variant<std::monostate, std::exception_ptr> result;
};

// Explicit declare concept
// FutureState must have result field with std::variant type
template <typename T> struct isVariant : std::false_type {
};

template <typename... Args> struct isVariant<std::variant<Args...>> : std::true_type {
};

template <typename T>
concept Variant = isVariant<std::remove_cvref_t<T>>::value;

// Any state that exposes a std::variant member named "result".
template <typename T>
concept HasVariantResult =
    requires(T& obj) { obj.result; } && Variant<decltype(std::declval<T>().result)>;

// Base of reader side of the shared state. Copy/move-safe by design.
template <HasVariantResult State> class FutureBase
{
public:
    FutureBase(FutureBase&&) = default;
    FutureBase& operator=(FutureBase&&) = default;

    /// Non-blocking readiness check.
    bool isReady()
    {
        return m_state->status.load(std::memory_order_acquire) == FutureStatus::Ready;
    }

    /// Returns the scheduler timer id, if one was assigned.
    std::optional<uint64_t> getId()
    {
        if (m_state->idSetted.load(std::memory_order_acquire)) {
            return m_state->id.load(std::memory_order_acquire);
        }
        return std::nullopt;
    }

    bool isClosed()
    {
        return m_state->status.load(std::memory_order_acquire) == FutureStatus::Closed;
    }

protected:
    // Futures are only created from the matching promise.
    explicit FutureBase(std::shared_ptr<State> state) : m_state(state) {}

    std::shared_ptr<State> m_state;
};

// Base of writer side of the shared state.
template <HasVariantResult State> class PromiseBase
{
public:
    PromiseBase() : m_state(std::make_shared<State>()) {}

    void setException(std::exception_ptr ptr)
    {
        if (!claim(m_state->blocked)) {
            LOG_WARNING("Promise exception allready setted up");
            return;
        }
        m_state->result.template emplace<std::exception_ptr>(std::move(ptr));
        m_state->status.store(FutureStatus::Ready, std::memory_order_release);
        m_state->status.notify_all();
    }

    // Associates the scheduler timer id with this result state.
    void setId(uint64_t id)
    {
        claim(m_state->idSetted);
        m_state->id.store(id, std::memory_order_release);
    }

    void close()
    {
        m_state->status.store(FutureStatus::Closed, std::memory_order_release);
        m_state->status.notify_all();
    }

protected:
    // Atomically takes the one-shot flag; false if it was already taken.
    bool claim(std::atomic<bool>& target)
    {
        bool expected = false;
        return target.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel, std::memory_order_acquire);
    }

    std::shared_ptr<State> m_state;
};

} // namespace miniruntime::detail