#pragma once

#include <atomic>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <type_traits>
#include <variant>

#include "logger.h"

namespace miniruntime {

    enum class FutureStatus {
        Pending,
        Ready,
        Closed
    };

    struct FutureStateBase {
        std::atomic<FutureStatus> status{FutureStatus::Pending};
        std::atomic<uint64_t> id;
        std::atomic<bool> idSetted{false};
        std::atomic<bool> blocked{false};
    };

    template<typename T>
    struct FutureState : FutureStateBase {
        std::variant<std::monostate, T, std::exception_ptr> result;
    };

    template<>
    struct FutureState<void> : FutureStateBase {
        std::variant<std::monostate, std::exception_ptr> result;
    };

    // Explicit declare concept
    // FutureState must have result field with std::variant type 
    template<typename T>
    struct isVariant : std::false_type {};

    template<typename... Args>
    struct isVariant<std::variant<Args...>> : std::true_type {};

    template<typename T>
    concept Variant = isVariant<std::remove_cvref_t<T>>::value;

    template<typename T>
    concept HasVariantResult = requires (T& obj) {
        obj.result;
    } && Variant<decltype(std::declval<T>().result)>;


    template<typename T>
    class Promise;

    template<HasVariantResult State>
    class FutureBase {
    public:
        FutureBase(FutureBase&&) = default;
        FutureBase& operator=(FutureBase&&) = default;

        bool isReady() {
            return m_state->status.load(std::memory_order_acquire) == FutureStatus::Ready;
        }

        std::optional<uint64_t> getId() {
            if (m_state->idSetted.load(std::memory_order_acquire)) {
                return m_state->id.load(std::memory_order_acquire);
            }
            return std::nullopt;
        }

        bool isClosed() {
            return m_state->status.load(std::memory_order_acquire) == FutureStatus::Closed;
        }

    protected:
        explicit FutureBase(std::shared_ptr<State> state)
            : m_state(state) {}

        std::shared_ptr<State> m_state;
    };

    template<typename T>
    class Future : public FutureBase<FutureState<T>> {
        friend class Promise<T>;
    public:
        using FutureBase<FutureState<T>>::FutureBase;

        std::optional<T> get() {
            this->m_state->status.wait(FutureStatus::Pending, std::memory_order_acquire);

            if (this->m_state->status.load(std::memory_order_acquire) == FutureStatus::Closed)
                return std::nullopt;
            
            auto& result = this->m_state->result;
            if (std::holds_alternative<std::exception_ptr>(result))
                std::rethrow_exception(std::get<std::exception_ptr>(result));

            return std::get<T>(result);
        }
    };

    template<>
    class Future<void> : public FutureBase<FutureState<void>> {
        friend class Promise<void>;
    public:
        using FutureBase<FutureState<void>>::FutureBase;

        void get() {
            this->m_state->status.wait(FutureStatus::Pending, std::memory_order_acquire);

            if (this->m_state->status.load(std::memory_order_acquire) == FutureStatus::Closed)
                return;
            
            auto& result = this->m_state->result;
            if (std::holds_alternative<std::exception_ptr>(result))
                std::rethrow_exception(std::get<std::exception_ptr>(result));
        }
    };


    template<HasVariantResult State>
    class PromiseBase {
    public:
        PromiseBase() : m_state(std::make_shared<State>()) {}

        void setException(std::exception_ptr ptr) {
            if (!claim(m_state->blocked)) {
                LOG_WARNING("Promise exception allready setted up");
                return;
            }
            m_state->result.template emplace<std::exception_ptr>(std::move(ptr));
            m_state->status.store(FutureStatus::Ready, std::memory_order_release);
            m_state->status.notify_all();
        }

        void setId(uint64_t id) {
            claim(m_state->idSetted);
            m_state->id.store(id, std::memory_order_release);
        }

        void close() {
            m_state->status.store(FutureStatus::Closed, std::memory_order_release);
            m_state->status.notify_all();
        }
        
    protected:
        bool claim(std::atomic<bool>& target) {
            bool expected = false;
            return target.compare_exchange_strong(expected, true,
                std::memory_order_acq_rel, std::memory_order_acquire);
        }

        std::shared_ptr<State> m_state;
    };

    template<typename T>
    class Promise : public PromiseBase<FutureState<T>> {
    public:
        void setValue(T value) {
            if (!this->claim(this->m_state->blocked)) {
                    LOG_WARNING("Promise<T> value already setted up");
                    return;
            }
            this->m_state->result.template emplace<T>(std::move(value));
            this->m_state->status.store(FutureStatus::Ready, std::memory_order_release);
            this->m_state->status.notify_all();
        }

        Future<T> getFuture() {
            return Future<T>(this->m_state);
        }
    };

    template<>
    class Promise<void> : public PromiseBase<FutureState<void>> {
    public:
        void setValue() {
            if (!this->claim(this->m_state->blocked)) {
                    LOG_WARNING("Promise<void> value already setted up");
                    return;
            }
            this->m_state->status.store(FutureStatus::Ready, std::memory_order_release);
            this->m_state->status.notify_all();
        }

        Future<void> getFuture() {
            return Future<void>(this->m_state);
        }
    };

}