cmake_minimum_required(VERSION 3.15)
project(quote_handling)

set(MY_VAR "hello")
set(MY_VERSION "v1.0.0")

# Custom command args: embedded quotes must be stripped (shell would strip them)
add_custom_target(test_quotes ALL
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/echo_args.sh -flag="${MY_VAR}" plain_arg >${CMAKE_CURRENT_BINARY_DIR}/quote_output.txt
    COMMENT "Testing quote handling"
)

# Compile definitions: embedded quotes must be preserved (C string literals)
add_executable(test_def test_def.cpp)
target_compile_definitions(test_def PRIVATE MY_VERSION="${MY_VERSION}")
