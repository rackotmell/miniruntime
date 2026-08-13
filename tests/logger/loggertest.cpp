#include <gtest/gtest.h>
#include <iostream>
#include <sstream>
#include <string>

#include "miniruntime/logger/logger.h"

using namespace miniruntime::logger;
using namespace std::chrono_literals;

namespace {

// Routes the singleton to a local stream and restores it on destruction.
class LogCapture
{
public:
    LogCapture()
    {
        Logger::getInstance().setOutput(m_stream);
        Logger::getInstance().setMinLevel(LogLevel::Debug);
    }

    ~LogCapture()
    {
        Logger::getInstance().setOutput(std::cout);
        Logger::getInstance().setMinLevel(LogLevel::Error);
    }

    void flush() { Logger::getInstance().flush(); }

    std::string text()
    {
        flush();
        return m_stream.str();
    }

private:
    std::ostringstream m_stream;
};

} // namespace

TEST(LoggerTest, WritesFormattedMessage)
{
    LogCapture capture;

    Logger::getInstance().log(
        LogLevel::Info, std::source_location::current(), "value is {}", 42);
    capture.flush();

    EXPECT_TRUE(capture.text().find("value is 42") != std::string::npos);
    EXPECT_TRUE(capture.text().find("INFO") != std::string::npos);
}

TEST(LoggerTest, FiltersByMinLevel)
{
    LogCapture capture;

    Logger::getInstance().setMinLevel(LogLevel::Error);
    Logger::getInstance().log(
        LogLevel::Debug, std::source_location::current(), "debug message");
    Logger::getInstance().log(
        LogLevel::Info, std::source_location::current(), "info message");
    capture.flush();
    EXPECT_EQ(capture.text(), "");

    Logger::getInstance().log(
        LogLevel::Error, std::source_location::current(), "error message");
    capture.flush();
    EXPECT_TRUE(capture.text().find("error message") != std::string::npos);
}

TEST(LoggerTest, FormatsThreadId)
{
    LogCapture capture;

    Logger::getInstance().log(
        LogLevel::Info, std::source_location::current(), "tid={}", std::this_thread::get_id());
    capture.flush();

    EXPECT_TRUE(capture.text().find("Thread-") != std::string::npos);
}

TEST(LoggerTest, BadFormatDoesNotThrow)
{
    LogCapture capture;

    EXPECT_NO_THROW(Logger::getInstance().log(
        LogLevel::Info, std::source_location::current(), "{} {} missing arg"));
    capture.flush();

    EXPECT_TRUE(capture.text().find("[Format error]") != std::string::npos);
}

TEST(LoggerTest, MacroLogsToCurrentSource)
{
    LogCapture capture;

    LOG_WARNING("warning from macro");
    capture.flush();

    const auto text = capture.text();
    EXPECT_TRUE(text.find("warning from macro") != std::string::npos);
    EXPECT_TRUE(text.find("loggertest.cpp") != std::string::npos);
    EXPECT_TRUE(text.find("WARNING") != std::string::npos);
}

TEST(LoggerTest, RedirectsOutput)
{
    LogCapture capture;
    std::ostringstream first;
    std::ostringstream second;

    Logger::getInstance().setOutput(first);
    Logger::getInstance().log(
        LogLevel::Info, std::source_location::current(), "first stream");
    Logger::getInstance().flush();

    Logger::getInstance().setOutput(second);
    Logger::getInstance().log(
        LogLevel::Info, std::source_location::current(), "second stream");
    Logger::getInstance().flush();

    EXPECT_TRUE(first.str().find("first stream") != std::string::npos);
    EXPECT_TRUE(second.str().find("second stream") != std::string::npos);
}
