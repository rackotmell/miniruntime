cmake_minimum_required(VERSION 3.20)

project(miniruntime VERSION 0.1.0 LANGUAGES CXX DESCRIPTION "Mini async task runtime for C++20")

option(ENABLE_ASAN_UBSAN "Build with ASAN+UBSAN sanitizers" OFF)
option(ENABLE_TSAN "Build with TSAN sanitizer" OFF)
option(BUILD_EXAMPLES "Build with examples" ON)

add_library(sanitizers_options INTERFACE)

if (ENABLE_ASAN_UBSAN)
    if (ENABLE_TSAN)
        message(FATAL_ERROR "ASAN+UBSAN and TSAN can not be combined")
    endif()
    target_compile_options(sanitizers_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(sanitizers_options INTERFACE -fsanitize=address,undefined)
elseif(ENABLE_TSAN)
    target_compile_options(sanitizers_options INTERFACE -fsanitize=thread -fno-omit-frame-pointer)
    target_link_options(sanitizers_options INTERFACE -fsanitize=thread)
endif()

add_subdirectory(src)

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()


