cmake_minimum_required(VERSION 3.15)
project(set_property_clear_inherited)

# Add include directories at the top level
include_directories(/fake/parent/inc1 /fake/parent/inc2)

get_directory_property(_parent_incs INCLUDE_DIRECTORIES)
message(STATUS "Parent INCLUDE_DIRECTORIES: ${_parent_incs}")

add_subdirectory(child1)
add_subdirectory(child2)
