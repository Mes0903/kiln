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

# Test 8: CMAKE_FLAGS support
try_compile(RESULT8 ${CMAKE_BINARY_DIR}
    SOURCE_FROM_CONTENT test8.cpp "#ifdef MY_FLAG\nint main() { return 0; }\n#else\n#error MY_FLAG not defined\n#endif"
    CMAKE_FLAGS -DCOMPILE_DEFINITIONS:STRING=-DMY_FLAG
)
if(NOT RESULT8)
    message(FATAL_ERROR "Test 8 failed: CMAKE_FLAGS should work")
endif()

# Test 9: CMAKE_FLAGS with multiple flags
try_compile(RESULT9 ${CMAKE_BINARY_DIR}
    SOURCE_FROM_CONTENT test9.cpp "#if defined(FLAG_A) && defined(FLAG_B)\nint main() { return 0; }\n#else\n#error Flags not defined\n#endif"
    CMAKE_FLAGS -DCOMPILE_DEFINITIONS:STRING=-DFLAG_A -DCOMPILE_DEFINITIONS:STRING=-DFLAG_B
)
if(NOT RESULT9)
    message(FATAL_ERROR "Test 9 failed: CMAKE_FLAGS with multiple entries should work")
endif()

# Test 10: project-mode try_compile must capture command stdout into
# OUTPUT_VARIABLE. qtbase's QtSeparateDebugInfo.cmake relies on this to fish a
# built library path out of build_output via regex; without capture, the
# regex extracts nothing and Qt's configure fails.
try_compile(RESULT10
    ${CMAKE_BINARY_DIR}/proj_with_echo_build
    ${CMAKE_CURRENT_SOURCE_DIR}/proj_with_echo
    proj_with_echo
    OUTPUT_VARIABLE PROJ_BUILD_OUTPUT
)
if(NOT RESULT10)
    message(FATAL_ERROR "Test 10 failed: project-mode try_compile should succeed.\nOutput was:\n${PROJ_BUILD_OUTPUT}")
endif()
string(REGEX REPLACE ".+###(.+)###.+" "\\1" TEST10_EXTRACTED "${PROJ_BUILD_OUTPUT}")
if("${TEST10_EXTRACTED}" STREQUAL "${PROJ_BUILD_OUTPUT}")
    message(FATAL_ERROR "Test 10 failed: ###...### markers not found in OUTPUT_VARIABLE.\nOutput was:\n${PROJ_BUILD_OUTPUT}")
endif()
if(NOT EXISTS "${TEST10_EXTRACTED}")
    message(FATAL_ERROR "Test 10 failed: extracted path does not exist: '${TEST10_EXTRACTED}'")
endif()
