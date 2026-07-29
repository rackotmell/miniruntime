#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <variant>

namespace miniruntime {

    template<typename T>
    struct FutureState {
        std::mutex mutex;
        std::condition_variable cv;
        std::variant<std::monostate, T, std::exception_ptr> result;
        std::atomic<bool> ready{false};
        std::optional<uint64_t> id;
    };

    template<>
    struct FutureState<void> {
        std::mutex mutex;
        std::condition_variable cv;
        std::optional<std::exception_ptr> result;
        std::atomic<bool> ready{false};
        std::optional<uint64_t> id;
    };


    template<typename T>
    class Promise;

    template<typename T>
    class Future {
    public:
        Future(Future&&) = default;
        Future& operator=(Future&&) = default; 

        T get() {
            std::unique_lock<std::mutex> lock(m_state->mutex);
            m_state->cv.wait(lock, [this]{
                return m_state->ready.load();
            });
            
            auto& result = m_state->result;
            if (std::holds_alternative<std::exception_ptr>(result))
                std::rethrow_exception(std::get<std::exception_ptr>(result));

            return std::get<T>(result);
        }

        bool isReady() {
            return m_state->ready.load();
        }

        std::optional<uint64_t> getId() {
            std::lock_guard<std::mutex> lock(m_state->mutex);
            return m_state->id;
        }
        
    private:
        friend class Promise<T>;
        explicit Future(std::shared_ptr<FutureState<T>> state)
            : m_state(state) {}

        std::shared_ptr<FutureState<T>> m_state;
    };

    template<>
    class Future<void> {
    public:
        Future(Future&&) = default;
        Future& operator=(Future&&) = default; 

        void get() {
            std::unique_lock<std::mutex> lock(m_state->mutex);
            m_state->cv.wait(lock, [this]{
                return m_state->ready.load();
            });
            
            auto& result = m_state->result;
            if (result)
                std::rethrow_exception(*result);
        }

        bool isReady() {
            return m_state->ready.load();
        }

        std::optional<uint64_t> getId() {
            std::lock_guard<std::mutex> lock(m_state->mutex);
            return m_state->id;
        }

    private:
        friend class Promise<void>;
        explicit Future(std::shared_ptr<FutureState<void>> state)
            : m_state(state) {}

        std::shared_ptr<FutureState<void>> m_state;
    };


    template<typename T>
    class Promise {
    public:
        Promise() : m_state(std::make_shared<FutureState<T>>()) {}

        void setValue(T value) {
            std::lock_guard<std::mutex> lock(m_state->mutex);
            m_state->result.template emplace<T>(std::move(value));
            m_state->ready.store(true);
            m_state->cv.notify_all();
        }

        void setException(std::exception_ptr ptr) {
            std::lock_guard<std::mutex> lock(m_state->mutex);
            m_state->result.template emplace<std::exception_ptr>(std::move(ptr));
            m_state->ready.store(true);
            m_state->cv.notify_all();
        }

        Future<T> getFuture() {
            return Future<T>(m_state);
        }

        void setId(uint64_t id) {
            std::lock_guard<std::mutex> lock(m_state->mutex);
            m_state->id = id;
        }

    private:
        std::shared_ptr<FutureState<T>> m_state;
    };

    template<>
    class Promise<void> {
    public:
        Promise() : m_state(std::make_shared<FutureState<void>>()) {}

        void setValue() {
            std::lock_guard<std::mutex> lock(m_state->mutex);
            m_state->ready.store(true);
            m_state->cv.notify_all();
        }

        void setException(std::exception_ptr ptr) {
            std::lock_guard<std::mutex> lock(m_state->mutex);
            m_state->result = std::move(ptr);
            m_state->ready.store(true);
            m_state->cv.notify_all();
        }

        Future<void> getFuture() {
            return Future<void>(m_state);
        }

        void setId(uint64_t id) {
            std::lock_guard<std::mutex> lock(m_state->mutex);
            m_state->id = id;
        }

    private:
        std::shared_ptr<FutureState<void>> m_state;
    };

}