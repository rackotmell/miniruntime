#include "miniruntime/logger/logger.h"
#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    miniruntime::logger::Logger::getInstance().setMinLevel(miniruntime::logger::LogLevel::Error);
    return RUN_ALL_TESTS();
}