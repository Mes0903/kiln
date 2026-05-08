cmake_minimum_required(VERSION 3.15)
project(modules_basic CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Create executable with module sources in the same target
# This tests modules within a single target (simpler case)
add_executable(modules_basic_app main.cpp math.cppm)
