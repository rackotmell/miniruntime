#include "logger.h"

#include <atomic>
#include <chrono>
#include <format>
#include <iostream>
#include <memory>

#include "boundedblockingqueue.h"

namespace miniruntime
{

// A single queued log record: metadata plus the pre-formatted message.
struct LogMessage {
    std::chrono::system_clock::time_point timestamp;
    LogLevel level;
    std::string message;
    std::source_location source;
};

struct Logger::Impl {
    std::atomic<LogLevel> minLevel{LogLevel::Debug};
    std::atomic<std::ostream*> output{&std::cout};
    BoundedBlockingQueue<LogMessage> queue{1000};
    std::thread thread{[this] { worker(); }};

    void worker()
    {
        while (auto message = queue.pop()) {
            writeMessage(message.value());
        }
    }

    void writeMessage(const LogMessage& message)
    {
        // Snapshot the output under acquire; the stream must outlive the logger.
        auto currentOutput = output.load(std::memory_order_acquire);
        *currentOutput << std::format("[{0:%F} {0:%T}] [{1:}] [{2:}:{3:}] {4:}\n",
                                      message.timestamp,
                                      levelToString(message.level),
                                      message.source.file_name(),
                                      message.source.line(),
                                      message.message)
                       << std::flush;
    }

    std::string_view levelToString(LogLevel level)
    {
        using namespace std::string_view_literals;

        switch (level) {
        case LogLevel::Debug:
            return "DEBUG"sv;
        case LogLevel::Info:
            return "INFO"sv;
        case LogLevel::Warning:
            return "WARNING"sv;
        case LogLevel::Error:
            return "ERROR"sv;
        default:
            return "UNKNOWN"sv;
        }
    }
};

Logger::Logger() : m_impl(std::make_unique<Impl>()) {}

// Destructor flushes pending messages and joins the writer thread.
Logger::~Logger() { shutdown(); }

void Logger::setMinLevel(LogLevel level)
{
    m_impl->minLevel.store(level, std::memory_order_release);
}

void Logger::setOutput(std::ostream& os) { m_impl->output.store(&os, std::memory_order_release); }

// Simple heuristic drain: give the writer a moment to empty the queue.
void Logger::flush() { std::this_thread::sleep_for(std::chrono::milliseconds(100)); }

void Logger::shutdown()
{
    // Close makes pop() return nullopt so the worker loop exits.
    m_impl->queue.close();
    if (m_impl->thread.joinable()) m_impl->thread.join();
}

LogLevel Logger::minLevel() { return m_impl->minLevel.load(std::memory_order_acquire); }

void Logger::enqueue(LogLevel level, std::source_location location, std::string message)
{
    m_impl->queue.push(LogMessage{
        .timestamp = std::chrono::system_clock::now(),
        .level = level,
        .message = message,
        .source = location,
    });
}

} // namespace miniruntime