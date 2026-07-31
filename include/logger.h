#pragma once

#include <chrono>
#include <format>
#include <ostream>
#include <iostream>
#include <source_location>
#include <string_view>
#include <thread>

#include "boundedblockingqueue.h"

template<>
struct std::formatter<std::thread::id, char> : std::formatter<unsigned long long, char> {
    auto format(std::thread::id id, format_context& ctx) const {
        return std::formatter<unsigned long long, char>::format(
            std::hash<std::thread::id>{}(id), ctx);
    }
};

namespace miniruntime {

    enum class LogLevel {
        Debug,
        Info,
        Warning,
        Error
    };

    class Logger {
    public:
        static Logger& getInstance() {
            static Logger instance;
            return instance;
        }

        ~Logger() {
            shutdown();
        }

        template<typename ...Args>
        void log(
            LogLevel level,
            std::source_location location,
            std::string_view format,
            Args&& ...args
        ) {
            if (level < m_minLevel)
                return;

            std::string formattedMessage;
            try {
                formattedMessage = std::vformat(format, std::make_format_args(std::forward<Args>(args)...));
            } catch (std::format_error& e) {
                formattedMessage = std::string("[Format error] ") + e.what();
            }

            m_queue.push(LogMessage{
                .timestamp = std::chrono::system_clock::now(),
                .level = level,
                .message = formattedMessage,
                .source = location,
            });
        }

        void setMinLevel(LogLevel level) { m_minLevel = level; }
        void setOutput(std::ostream& os) { m_output = &os; }
        void flush() {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        void shutdown() {
            m_queue.close();
            if (m_thread.joinable())
                m_thread.join();
        }

    private:
        struct LogMessage {
            std::chrono::system_clock::time_point timestamp;
            LogLevel level;
            std::string message;
            std::source_location source;
        };

        Logger() : m_minLevel(LogLevel::Debug)
            , m_output(&std::cout)
            , m_queue(1000)
            , m_thread([this](){
                worker();
            })
        {}

        void worker() {
            while (auto message = m_queue.pop()) {
                writeMessage(message.value());
            }
        }

        void writeMessage(const LogMessage& message) {
            *m_output << std::format(
                "[{0:%F} {0:%T}] [{1:}] [{2:}:{3:}] {4:}\n",
                message.timestamp,
                levelToString(message.level),
                message.source.file_name(),
                message.source.line(),
                message.message
            ) << std::flush;
        }

        std::string_view levelToString(LogLevel level) {
            using namespace std::string_view_literals;

            switch (level) {
            case LogLevel::Debug: return "DEBUG"sv;
            case LogLevel::Info: return "INFO"sv;
            case LogLevel::Warning: return "WARNING"sv;
            case LogLevel::Error: return "ERROR"sv;
            default: return "UNKNOWN"sv;
            }
        } 

        LogLevel m_minLevel;
        std::ostream* m_output;
        BoundedBlockingQueue<LogMessage> m_queue;
        std::thread m_thread;
    };

    #define LOG_DEBUG(format, ...) \
        miniruntime::Logger::getInstance().log( \
            miniruntime::LogLevel::Debug, std::source_location::current(), format, ##__VA_ARGS__)

    #define LOG_INFO(format, ...) \
        miniruntime::Logger::getInstance().log( \
            miniruntime::LogLevel::Info, std::source_location::current(), format, ##__VA_ARGS__)

    #define LOG_WARNING(format, ...) \
        miniruntime::Logger::getInstance().log( \
            miniruntime::LogLevel::Warning, std::source_location::current(), format, ##__VA_ARGS__)

    #define LOG_ERROR(format, ...) \
        miniruntime::Logger::getInstance().log( \
            miniruntime::LogLevel::Error, std::source_location::current(), format, ##__VA_ARGS__)
}