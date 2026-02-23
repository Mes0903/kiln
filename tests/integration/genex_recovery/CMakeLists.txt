cmake_minimum_required(VERSION 3.15)
project(genex_recovery_test)

add_executable(dummy main.cpp)
set_target_properties(dummy PROPERTIES COMPILE_DEFINITIONS "FOO;BAR")

# Test 1: Normal ANGLE-R / SEMICOLON / COMMA evaluation
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/test1.txt"
    CONTENT "a$<SEMICOLON>b$<COMMA>c$<ANGLE-R>d")

# Test 2: ANGLE-R inside conditional
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/test2.txt"
    CONTENT "$<$<BOOL:1>:a$<ANGLE-R>b>")

# Test 3: JOIN with COMMA separator
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/test3.txt"
    CONTENT "$<JOIN:a;b;c,$<COMMA>>")

# Test 4: Literal boolean conditionals
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/test4.txt"
    CONTENT "$<1:included>$<0:excluded>")
