#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <deque>

namespace miniruntime {

    template<typename T>
    class BlockingQueue {
        public:

            void push(T value) {
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_deque.push_back(value);
                }
                m_cv.notify_one();
            }

            T pop() {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait(lock, [this]{
                    return !m_deque.empty() || m_closed;
                });
                if (m_closed && m_deque.empty()) {
                    throw std::runtime_error("queue closed");
                }
                T value = std::move(m_deque.front());
                m_deque.pop_front();

                return value;
            }

            template<typename ...Args>
            void emplace(Args&& ...args) {
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_deque.emplace_back(std::forward<Args>(args)...);
                }
                m_cv.notify_one();
            }
            
            void close() {
                m_closed = true;
                m_cv.notify_all();
            }

        private:
            std::mutex m_mutex;
            std::condition_variable m_cv;

            std::deque<T> m_deque;
            std::atomic<bool> m_closed = false;
    };

}