cmake_minimum_required(VERSION 3.20)

project(TaskRunner VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ENABLE_ASAN_UBSAN "Build with ASAN+UBSAN sanitizers" OFF)
option(ENABLE_TSAN "Build with TSAN sanitizer" OFF)

if (ENABLE_ASAN AND ENABLE_TSAN)
    message(FATAL_ERROR "ASAN+UBSAN and TSAN can not be combined")
endif()

set(SAN_FLAGS)
if (ENABLE_ASAN_UBSAN)
    list(APPEND SAN_FLAGS
        -fsanitize=address,undefined
        -fno-omit-frame-pointer)
elseif(ENABLE_TSAN)
    list(APPEND SAN_FLAGS
        -fsanitize=thread
        -fno-omit-frame-pointer)
endif()

if (SAN_FLAGS)
    add_compile_options(${SAN_FLAGS})
    add_link_options(${SAN_FLAGS})
endif()

add_compile_options(-Wall -Wextra -Wpedantic -Werror)

add_subdirectory(src)