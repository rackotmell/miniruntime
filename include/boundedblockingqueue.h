#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <deque>
#include <optional>

namespace miniruntime {

    template<typename T>
    class BoundedBlockingQueue {
        public:
            BoundedBlockingQueue(size_t maxSize) : m_dequeMaxSize(maxSize) {}

            void push(T value) {
                {
                    std::unique_lock<std::mutex> lock(m_mutex);
                    m_notFullCv.wait(lock, [this]{
                        return m_deque.size() < m_dequeMaxSize;
                    });
                    m_deque.push_back(value);
                }
                m_notEmptyCv.notify_one();
            }

            std::optional<T> pop() {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_notEmptyCv.wait(lock, [this]{
                    return !m_deque.empty() || m_closed;
                });

                if (m_closed && m_deque.empty()) {
                    return std::nullopt;
                }
                T value = std::move(m_deque.front());
                m_deque.pop_front();

                m_notFullCv.notify_one();

                return value;
            }

            template<typename Rep, typename Period>
            std::optional<T> timeoutPop(std::chrono::duration<Rep, Period> duration) {
                std::unique_lock<std::mutex> lock(m_mutex);
                if (!m_notEmptyCv.wait_for(lock, duration, [this]{
                        return !m_deque.empty() || m_closed;
                    })) {
                    return std::nullopt;
                }

                if (m_deque.empty())
                    return std::nullopt;

                T value = std::move(m_deque.front());
                m_deque.pop_front();
                
                m_notFullCv.notify_one();;

                return value;
            }

            template<typename ...Args>
            bool emplace(Args&& ...args) {
                {
                    std::unique_lock<std::mutex> lock(m_mutex);
                    m_notFullCv.wait(lock, [this]{
                        return m_deque.size() < m_dequeMaxSize;
                    });
                    m_deque.emplace_back(std::forward<Args>(args)...);
                }
                m_notEmptyCv.notify_one();
            }
            
            void close() {
                m_closed = true;
                m_notEmptyCv.notify_all();
                m_notFullCv.notify_all();
            }

            size_t size()
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                return m_deque.size();
            } 

        private:
            std::mutex m_mutex;
            std::condition_variable m_notEmptyCv;
            std::condition_variable m_notFullCv;

            std::deque<T> m_deque;
            size_t m_dequeMaxSize;
            std::atomic<bool> m_closed = false;
    };

}