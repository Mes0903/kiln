cmake_minimum_required(VERSION 3.15)
project(TryCompileIntegration)

# Test 1: Basic success with SOURCES
file(WRITE ${CMAKE_BINARY_DIR}/test_success.cpp "int main() { return 0; }")
try_compile(RESULT1 ${CMAKE_BINARY_DIR}
    SOURCES ${CMAKE_BINARY_DIR}/test_success.cpp
    CXX_STANDARD 17
)
if(NOT RESULT1)
    message(FATAL_ERROR "Test 1 failed: basic compilation should succeed")
endif()

# Test 2: Expected failure (invalid code)
file(WRITE ${CMAKE_BINARY_DIR}/test_failure.cpp "int main() { COMPILE_ERROR }")
try_compile(RESULT2 ${CMAKE_BINARY_DIR}
    SOURCES ${CMAKE_BINARY_DIR}/test_failure.cpp
)
if(RESULT2)
    message(FATAL_ERROR "Test 2 failed: should have failed to compile")
endif()

# Test 3: SOURCE_FROM_CONTENT
try_compile(RESULT3 ${CMAKE_BINARY_DIR}
    SOURCE_FROM_CONTENT test3.cpp "int main() { return 0; }"
    CXX_STANDARD 20
)
if(NOT RESULT3)
    message(FATAL_ERROR "Test 3 failed: SOURCE_FROM_CONTENT should succeed")
endif()

# Test 4: SOURCE_FROM_VAR
set(MY_SOURCE "int main() { return 42; }")
try_compile(RESULT4 ${CMAKE_BINARY_DIR}
    SOURCE_FROM_VAR test4.cpp MY_SOURCE
    CXX_STANDARD 17
)
if(NOT RESULT4)
    message(FATAL_ERROR "Test 4 failed: SOURCE_FROM_VAR should succeed")
endif()

# Test 5: With compile definitions
try_compile(RESULT5 ${CMAKE_BINARY_DIR}
    SOURCE_FROM_CONTENT test5.cpp "#ifdef TEST_DEFINE\nint main() { return 0; }\n#else\n#error Missing define\n#endif"
    COMPILE_DEFINITIONS TEST_DEFINE
)
if(NOT RESULT5)
    message(FATAL_ERROR "Test 5 failed: compilation with definitions should succeed")
endif()

# Test 6: Cache hit (run twice with same source)
try_compile(RESULT6_A ${CMAKE_BINARY_DIR}
    SOURCE_FROM_CONTENT test6.cpp "int main() { return 0; }"
    CXX_STANDARD 17
)
try_compile(RESULT6_B ${CMAKE_BINARY_DIR}
    SOURCE_FROM_CONTENT test6.cpp "int main() { return 0; }"
    CXX_STANDARD 17
)
if(NOT RESULT6_A OR NOT RESULT6_B)
    message(FATAL_ERROR "Test 6 failed: cache hit test")
endif()

# Test 7: OUTPUT_VARIABLE
try_compile(RESULT7 ${CMAKE_BINARY_DIR}
    SOURCE_FROM_CONTENT test7.cpp "INVALID CODE"
    OUTPUT_VARIABLE COMPILE_OUTPUT
)
if(RESULT7)
    message(FATAL_ERROR "Test 7 failed: should have failed")
endif()
if("${COMPILE_OUTPUT}" STREQUAL "")
    message(FATAL_ERROR "Test 7 failed: OUTPUT_VARIABLE should contain error output")
endif()

message(STATUS "All try_compile integration tests passed!")
