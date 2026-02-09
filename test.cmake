# Quick memory check
cmake_minimum_required(VERSION 3.16)
project(test)

function(test_cmake_parse_arguments)
  cmake_parse_arguments(
    FOO
    "FLAG1;FLAG2"
    "NAME;VALUE"
    "SRCS;DEPS"
    ${ARGN}
  )
endfunction()

# Call it 1000 times
foreach(i RANGE 1 1000)
  test_cmake_parse_arguments(NAME "test_${i}" SRCS a.cpp b.cpp c.cpp DEPS dep1 dep2)
endforeach()
message(STATUS "Done with 1000 calls")
