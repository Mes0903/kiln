cmake_minimum_required(VERSION 3.10)
project(TestProject CXX)

add_library(my_lib SHARED my_lib.cpp)

add_executable(test_project main.cpp)

target_link_libraries(test_project PRIVATE my_lib)
