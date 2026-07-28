#pragma once

#include <condition_variable>
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
            std::lock_guard lock(m_mutex);
            m_closed = true;
            m_cv.notify_all();
        }
        
    private:
        std::optional<T> m_value;
        std::mutex m_mutex;
        std::condition_variable m_cv;
        std::exception_ptr m_exception;
        bool m_closed;
    };

}