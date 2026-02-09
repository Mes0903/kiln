cmake_minimum_required(VERSION 3.15)
project(glob_wildcard_dirs)

# Absolute glob with wildcard directory component
file(GLOB found_abs "libs/*/CMakeLists.txt")
message(STATUS "ABS: ${found_abs}")

# Relative glob with wildcard directory component
file(GLOB found_rel RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "libs/*/CMakeLists.txt")
message(STATUS "REL: ${found_rel}")
