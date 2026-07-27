#pragma once

#include <atomic>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <variant>

namespace miniruntime {

    template<typename T>
    struct SharedState {
        std::mutex mutex;
        std::condition_variable cv;
        std::variant<std::monostate, T, std::exception_ptr> result;
        std::atomic<bool> ready;
    };

    template<typename T>
    class Promise;

    template<typename T>
    class Future {
    public:
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
        
    private:
        friend class Promise<T>;
        explicit Future(std::shared_ptr<SharedState<T>> state)
            : m_state(state) {}


        std::shared_ptr<SharedState<T>> m_state;
    };

    template<typename T>
    class Promise {
    public:
        Promise() : m_state(std::make_shared<SharedState<T>>()) {}

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

    private:
        std::shared_ptr<SharedState<T>> m_state;
    };

}