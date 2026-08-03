set(LIB_NAME miniruntime)

add_library(${LIB_NAME} STATIC 
    eventloop.cpp
    handle.cpp
    dynamicthreadpool.cpp
    taskscheduler.cpp
    logger.cpp
)

target_include_directories(${LIB_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include/miniruntime>
)

target_compile_definitions(${LIB_NAME} PRIVATE cxx_std_20)
target_compile_options(${LIB_NAME} PRIVATE -Wall -Wextra -Wpedantic -Werror -std=gnu++20)

find_package(Threads REQUIRED)
target_link_libraries(${LIB_NAME} PUBLIC Threads::Threads)
target_link_libraries(${LIB_NAME} PRIVATE sanitizers_options)

include(CMakePackageConfigHelpers)

install(
    TARGETS ${LIB_NAME} sanitizers_options
    EXPORT miniruntimeTargets 
    ARCHIVE DESTINATION lib/miniruntime
    INCLUDES DESTINATION include/miniruntime
)

install(
    DIRECTORY ${CMAKE_SOURCE_DIR}/include/
    DESTINATION include/miniruntime
)

install(
    EXPORT miniruntimeTargets
    FILE miniruntimeTargets.cmake
    NAMESPACE ${LIB_NAME}::
    DESTINATION ${LIB_NAME}/cmake/
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/miniruntimeConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/miniruntimeConfig.cmake
    INSTALL_DESTINATION ${LIB_NAME}/cmake
)

install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/miniruntimeConfig.cmake
    DESTINATION ${LIB_NAME}/cmake
)