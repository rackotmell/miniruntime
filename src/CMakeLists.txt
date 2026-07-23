add_executable(${PROJECT_NAME} 
    main.cpp
    eventloop.cpp
    handle.cpp

)

target_include_directories(${PROJECT_NAME} PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)