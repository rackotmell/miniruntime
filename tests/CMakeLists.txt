include(FetchContent)
FetchContent_Declare(
    googletests
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.17.0
)
FetchContent_MakeAvailable(googletests)

enable_testing()
include(GoogleTest)

set(TESTS
    boundblockingqueuetest
    dynamicthreadpooltest
    eventlooptest
    futuretest
    loadtest
    loggertest
    sharedvaluetest
    taskschedulertest
    templatecompatibilitytest
)

foreach(test IN LISTS TESTS)
    add_executable(${test} ${test}.cpp main.cpp)
    target_compile_features(${test} PRIVATE cxx_std_20)
    target_compile_options(${test} PRIVATE -std=gnu++20 -Wall -Wextra)
    target_link_libraries(${test} PRIVATE 
        miniruntime
        GTest::gtest
        sanitizers_options
    )
    gtest_discover_tests(${test})
endforeach()
