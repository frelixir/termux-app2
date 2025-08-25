cmake_minimum_required(VERSION 3.22.1)
project("display")

include_directories(inc)
include_directories(include)
add_library(${CMAKE_PROJECT_NAME} SHARED
        src/buffer.c
        display.cpp)

target_link_libraries(${CMAKE_PROJECT_NAME}
        # List libraries link to the target library
        android
        log)
