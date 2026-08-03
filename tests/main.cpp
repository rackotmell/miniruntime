#include "logger.h"
#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    miniruntime::Logger::getInstance().setMinLevel(miniruntime::LogLevel::Error);
    return RUN_ALL_TESTS();
}