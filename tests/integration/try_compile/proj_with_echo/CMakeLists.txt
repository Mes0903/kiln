cmake_minimum_required(VERSION 3.15)
project(proj_with_echo CXX)

# Build a tiny library, then run a custom target that echoes the library path
# wrapped in ### markers. Mirrors what qtbase's QtSeparateDebugInfo.cmake does
# when it tries to discover the linker output path via try_compile + regex.
add_library(echoed_lib SHARED echoed.cpp)

add_custom_target(print_lib_path ALL
    COMMAND ${CMAKE_COMMAND} -E echo "###$<TARGET_FILE:echoed_lib>###"
    DEPENDS echoed_lib
)
