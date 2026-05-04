project(PerSubdirCompiler CXX)
message(STATUS "Top-level CXX compiler: ${CMAKE_CXX_COMPILER}")

# Top-level target uses default (gcc/g++).
add_executable(host_app main.cpp)

# Subdirectory swaps the compiler for its own targets.
add_subdirectory(sub)

message(STATUS "After subdir, top-level CXX compiler: ${CMAKE_CXX_COMPILER}")
