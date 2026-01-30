cmake_minimum_required(VERSION 3.15)
project(ConfigureFileTest)

# Test variables with various values
set(COMPILATION_FLAGS "-Wall -Wextra -Werror")
set(CXX_STANDARD 17)
set(CXX_STANDARD_NUM 20)
set(EMPTY_VAR "")
# UNDEFINED_VAR intentionally not set

configure_file(
    "config.h.in"
    "config.h"
    @ONLY
)

# Create a simple executable that includes the generated header
add_executable(test_config main.cpp)
target_include_directories(test_config PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
