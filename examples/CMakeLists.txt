cmake_minimum_required(VERSION 3.20)

project(miniruntime_examples LANGUAGES CXX)

set(EXAMPLES
    taskschedulerexample
    schedulersocketexample
    schedulertriggersexample
    eventlooppoolexample
    eventloopqueueexample
)

foreach(example IN LISTS EXAMPLES)
    add_executable(${example} src/${example}.cpp)
    target_compile_features(${example} PRIVATE cxx_std_20)
    target_compile_options(${example} PRIVATE -std=gnu++20)
    target_link_libraries(${example} PRIVATE miniruntime)
endforeach()
