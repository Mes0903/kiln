cmake_minimum_required(VERSION 3.16)
project(test)

# Simulate what folly_add_library does
function(test_func)
  set(_obj_target "my_obj_target")
  file(WRITE "${CMAKE_BINARY_DIR}/dummy.cpp" "int dummy() { return 0; }")
  add_library(${_obj_target} OBJECT "${CMAKE_BINARY_DIR}/dummy.cpp")
  message(STATUS "Inside function: TARGET exists = $<TARGET_EXISTS:${_obj_target}>")
endfunction()

test_func()

# Check if target is visible outside the function
if(TARGET my_obj_target)
  message(STATUS "Target visible outside function")
else()
  message(STATUS "Target NOT visible outside function")
endif()
