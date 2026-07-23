# pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_map>

namespace miniruntime {

    enum class EventType {
        SOCKET,
        TRIGGER
    };

    class EventLoop;

    class HandleBase {
        public:
            HandleBase(const HandleBase&) = delete;
            HandleBase& operator=(const HandleBase&) = delete;
            HandleBase(HandleBase&& handle);
            HandleBase& operator=(HandleBase&& handle);

            virtual ~HandleBase();

            bool valid() const;

        protected:
            EventLoop* m_loop;
            int m_fd;

            HandleBase(EventLoop* loop, int fd);
    };

    class EventHandle : public HandleBase {
        friend class EventLoop;

        public:
            using HandleBase::HandleBase;

        private:
            EventHandle(EventLoop* loop, int fd);
    };

    class TriggerHandle : public HandleBase {
        friend class EventLoop;

        public:
            using HandleBase::HandleBase;
            void trigger() const;

        private:
            TriggerHandle(EventLoop* loop, int fd);
    };

    class EventLoop {
        using EventCallback = std::function<void(int)>;
        using TriggerCallback = std::function<void()>;

        public:
            EventLoop();
            ~EventLoop();

            [[nodiscard]] EventHandle createEvent(int fd, uint32_t epollFlags, EventType type, EventCallback callback);
            [[nodiscard]] TriggerHandle createTrigger(TriggerCallback callback);

            void run();
            void stop();

        private:
            int m_epollFd;
            std::atomic<bool> m_stop;
            std::mutex m_registerMutex;

            struct Event {
                int fd;
                uint32_t epollFlags;
                EventType type;
                EventCallback callback;
            };
            std::unordered_map<int, Event> m_events;

            friend class HandleBase;
            friend class EventHandle;
            friend class TriggerHandle;
            void unregisterEvent(int fd);
            void registerEvent(Event& event);
    };

}