cmake_minimum_required(VERSION 3.15)
project(AsmSupport C)

# Only test assembly on x86_64
if(NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
    message(STATUS "Skipping ASM test on ${CMAKE_SYSTEM_PROCESSOR}")
    return()
endif()

enable_language(ASM)
add_executable(asm_test main.c add_numbers.S)
