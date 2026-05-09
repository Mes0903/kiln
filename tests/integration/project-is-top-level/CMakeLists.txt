cmake_minimum_required(VERSION 3.21)
project(TopProj)

# project() in the top-level source dir → both must be TRUE.
if(NOT PROJECT_IS_TOP_LEVEL)
    message(FATAL_ERROR "PROJECT_IS_TOP_LEVEL=${PROJECT_IS_TOP_LEVEL} for top-level project")
endif()
if(NOT TopProj_IS_TOP_LEVEL)
    message(FATAL_ERROR "TopProj_IS_TOP_LEVEL=${TopProj_IS_TOP_LEVEL}")
endif()

add_subdirectory(sub)

# After the subdirectory's project() ran, our own PROJECT_IS_TOP_LEVEL must
# still be TRUE in this scope (the variable is per-scope).
if(NOT PROJECT_IS_TOP_LEVEL)
    message(FATAL_ERROR "PROJECT_IS_TOP_LEVEL clobbered after add_subdirectory")
endif()

message(STATUS "project-is-top-level: OK")
