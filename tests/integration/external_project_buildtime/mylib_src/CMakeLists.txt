cmake_minimum_required(VERSION 3.15)
project(mylib)

# Simple library that the parent project depends on
add_library(mylib STATIC mylib.cpp)
target_include_directories(mylib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Dump target info at build time for debugging
dmake_dump_target_info(mylib AT_BUILD)

# Install the library
install(TARGETS mylib ARCHIVE DESTINATION lib)
install(FILES mylib.h DESTINATION include)
