add_executable(${PROJECT_NAME} 
    main.cpp
    eventloop.cpp
    handle.cpp
    dynamicthreadpool.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)