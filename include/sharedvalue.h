#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <mutex>
#include <optional>
#include <utility>

namespace miniruntime {

    template<typename T>
    class SharedValue {
    public:
        void set(T value) {
            {
                std::lock_guard lock(m_mutex);
                m_value = std::move(value);
            }
            m_cv.notify_all();
        }

        void setException(std::exception_ptr ptr) {
            {
                std::lock_guard lock(m_mutex);
                m_exception = std::move(ptr);
            }
            m_cv.notify_all();
        }

        std::optional<T> wait() {
            std::unique_lock lock(m_mutex);
            m_cv.wait(lock, [this] {
                return m_value.has_value() || m_closed;
            });
            if (m_closed)
                return std::nullopt;
            if (m_exception)
                std::rethrow_exception(m_exception);

            const auto result = std::move(m_value);
            m_value.reset();

            return result;
        }

        void close() {
            m_closed.store(true);
            m_cv.notify_all();
        }

        void setId(uint64_t id) {
            std::lock_guard lock(m_mutex);
            m_id = id;
        }

        std::optional<uint64_t> getId() {
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


    template<>
    class SharedValue<void> : SharedValue<bool>
    {
        using Base = SharedValue<bool>;
        public:
            using Base::close;
            using Base::setException;
            using Base::setId;
            using Base::getId;

            void set() {
                Base::set(true);
            }
            bool wait() {
                return Base::wait().has_value();
            }
    };

}