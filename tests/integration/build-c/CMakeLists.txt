cmake_minimum_required(VERSION 3.10)

project(build-c)

add_library(lib STATIC lib.c)
add_executable(main main.cpp)
target_link_libraries(main lib)
