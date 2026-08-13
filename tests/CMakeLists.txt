include(FetchContent)
FetchContent_Declare(
    googletests
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.17.0
)
FetchContent_MakeAvailable(googletests)

enable_testing()
include(GoogleTest)

set(TEST_MULTI_COMPONENT
    loadtest
    templatecompatibilitytest
)

set(TESTS
    task/boundblockingqueuetest
    task/dynamicthreadpooltest
    task/michaelscottqueuetest
    event/eventlooptest
    asyncresult/futuretest
    asyncresult/sharedvaluetest
    logger/loggertest
    scheduler/taskschedulertest
)

foreach(test IN LISTS TESTS)
    get_filename_component(test_name ${test} NAME)
    add_executable(${test_name} ${test}.cpp main.cpp)
    target_compile_features(${test_name} PRIVATE cxx_std_20)
    target_compile_options(${test_name} PRIVATE -std=gnu++20 -Wall -Wextra)
    target_link_libraries(${test_name} PRIVATE 
        miniruntime
        GTest::gtest
        sanitizers_options
    )
    gtest_discover_tests(${test_name})
endforeach()

foreach(test IN LISTS TEST_MULTI_COMPONENT)
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
