cmake_minimum_required(VERSION 3.15)
project(FunctionListDirTest)

# Include file that defines a function
include(subdir/helpers.cmake)

# Call the function - it should report its definition location (subdir), not caller location
message("Main dir: ${CMAKE_CURRENT_LIST_DIR}")
test_function_from_subdir()
