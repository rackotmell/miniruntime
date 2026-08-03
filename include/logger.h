#pragma once

#include <format>
#include <memory>
#include <source_location>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>

// Makes std::thread::id printable via std::format, e.g. "Thread-123".
template <> struct std::formatter<std::thread::id> : std::formatter<std::string_view> {
    auto format(const std::thread::id& id, std::format_context& ctx) const
    {
        std::ostringstream os;
        os << "Thread-" << id;
        return std::formatter<std::string_view>::format(os.str(), ctx);
    }
};

namespace miniruntime
{

/// Severity of a log message; lower values are more verbose.
enum class LogLevel { Debug, Info, Warning, Error };

/**
 * @brief Asynchronous logger singleton.
 *
 * Logger accepts messages from any thread, formats them on the caller and
 * offloads the actual writing to a dedicated worker thread through a bounded
 * queue. This keeps logging latency off the hot path. Access is via the
 * getInstance() singleton; the LOG_* macros capture the source location
 * automatically.
 */
class Logger
{
public:
    /**
     * @brief Returns the logger instance.
     * @return Reference to the singleton logger.
     */
    static Logger& getInstance()
    {
        static Logger instance;
        return instance;
    }
    ~Logger();

    /**
     * @brief Formats and enqueues a log message.
     * @param level    Log level of the message.
     * @param location Call-site source location (filled by the macros).
     * @param format   format string for std::format.
     * @param args     Format arguments.
     */
    template <typename... Args>
    void log(LogLevel level, std::source_location location, std::string_view format, Args... args)
    {
        if (level < minLevel()) return;

        std::string formattedMessage;
        try {
            // Args by value: make_format_args requires lvalues, so rvalues
            // like literals or temporaries must be copied into the frame first.
            formattedMessage = std::vformat(format, std::make_format_args(args...));
        } catch (std::format_error& e) {
            // Never let a bad format string break the caller.
            formattedMessage = std::string("[Format error] ") + e.what();
        }
        enqueue(level, location, std::move(formattedMessage));
    }

    /**
     * @brief Sets the minimum level; messages below it are dropped.
     * @param level New minimum level.
     */
    void setMinLevel(LogLevel level);

    /**
     * @brief Redirects all output to the given stream.
     * @param os Target stream (must outlive the logger).
     */
    void setOutput(std::ostream& os);

    /**
     * @brief Blocks briefly to let pending messages drain.
     */
    void flush();

    /**
     * @brief Closes the queue and joins the worker thread.
     */
    void shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    // Private: only getInstance() may construct the singleton.
    Logger();

    void enqueue(LogLevel level, std::source_location location, std::string message);
    LogLevel minLevel();
};

// Convenience macros: capture the source location and route to the singleton.
#define LOG_DEBUG(format, ...)                                                                     \
    miniruntime::Logger::getInstance().log(                                                        \
        miniruntime::LogLevel::Debug, std::source_location::current(), format, ##__VA_ARGS__)

#define LOG_INFO(format, ...)                                                                      \
    miniruntime::Logger::getInstance().log(                                                        \
        miniruntime::LogLevel::Info, std::source_location::current(), format, ##__VA_ARGS__)

#define LOG_WARNING(format, ...)                                                                   \
    miniruntime::Logger::getInstance().log(                                                        \
        miniruntime::LogLevel::Warning, std::source_location::current(), format, ##__VA_ARGS__)

#define LOG_ERROR(format, ...)                                                                     \
    miniruntime::Logger::getInstance().log(                                                        \
        miniruntime::LogLevel::Error, std::source_location::current(), format, ##__VA_ARGS__)
} // namespace miniruntime