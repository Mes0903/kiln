cmake_minimum_required(VERSION 3.10)
project(tcc-c-only C)

add_library(util STATIC util.c)
add_executable(hello hello.c)
target_link_libraries(hello util)
