cmake_minimum_required(VERSION 3.28)
project(target_sources_test CXX)

set(CMAKE_CXX_STANDARD 20)

# Test 1: Basic target_sources with regular sources
add_executable(test_basic)
target_sources(test_basic
    PRIVATE
        main.cpp
)

# Test 2: target_sources with CXX_MODULES file set
add_executable(test_modules)
target_sources(test_modules
    PRIVATE
        main_modules.cpp
    PRIVATE
        FILE_SET cxx_modules
        TYPE CXX_MODULES
        FILES
            math.cppm
)

# Test 3: target_sources with multiple visibility scopes
add_library(test_lib STATIC)
target_sources(test_lib
    PRIVATE
        lib_impl.cpp
    PUBLIC
        FILE_SET headers
        TYPE HEADERS
        FILES
            lib.hpp
)
