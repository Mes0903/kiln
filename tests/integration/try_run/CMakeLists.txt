cmake_minimum_required(VERSION 3.15)
project(TryRunTest)

# Test 1: Basic try_run with return code
try_run(RUN_RESULT COMPILE_RESULT
    SOURCE_FROM_CONTENT test.cpp "int main() { return 42; }"
    COMPILE_OUTPUT_VARIABLE COMPILE_OUTPUT
    RUN_OUTPUT_VARIABLE RUN_OUTPUT
)

message(STATUS "Test 1 - Basic try_run:")
message(STATUS "  Compile result: ${COMPILE_RESULT}")
message(STATUS "  Run result: ${RUN_RESULT}")

if(NOT COMPILE_RESULT)
    message(FATAL_ERROR "Test 1 failed: compilation failed")
endif()

if(NOT RUN_RESULT EQUAL 42)
    message(FATAL_ERROR "Test 1 failed: expected exit code 42, got ${RUN_RESULT}")
endif()

# Test 2: try_run with output capture
try_run(RUN_RESULT2 COMPILE_RESULT2
    SOURCE_FROM_CONTENT test2.cpp "#include <iostream>\nint main() { std::cout << \"Hello World\"; return 0; }"
    RUN_OUTPUT_VARIABLE OUTPUT2
    CXX_STANDARD 17
)

message(STATUS "Test 2 - Output capture:")
message(STATUS "  Compile result: ${COMPILE_RESULT2}")
message(STATUS "  Run result: ${RUN_RESULT2}")
message(STATUS "  Output: ${OUTPUT2}")

if(NOT COMPILE_RESULT2)
    message(FATAL_ERROR "Test 2 failed: compilation failed")
endif()

if(NOT RUN_RESULT2 EQUAL 0)
    message(FATAL_ERROR "Test 2 failed: expected exit code 0, got ${RUN_RESULT2}")
endif()

if(NOT OUTPUT2 MATCHES "Hello World")
    message(FATAL_ERROR "Test 2 failed: expected 'Hello World' in output, got '${OUTPUT2}'")
endif()

# Test 3: try_run with arguments
try_run(RUN_RESULT3 COMPILE_RESULT3
    SOURCE_FROM_CONTENT test3.cpp "#include <iostream>\nint main(int argc, char** argv) { if(argc > 1) std::cout << argv[1]; return argc - 1; }"
    ARGS "test_arg"
    RUN_OUTPUT_VARIABLE OUTPUT3
)

message(STATUS "Test 3 - With arguments:")
message(STATUS "  Compile result: ${COMPILE_RESULT3}")
message(STATUS "  Run result: ${RUN_RESULT3}")
message(STATUS "  Output: ${OUTPUT3}")

if(NOT COMPILE_RESULT3)
    message(FATAL_ERROR "Test 3 failed: compilation failed")
endif()

if(NOT RUN_RESULT3 EQUAL 1)
    message(FATAL_ERROR "Test 3 failed: expected exit code 1, got ${RUN_RESULT3}")
endif()

if(NOT OUTPUT3 MATCHES "test_arg")
    message(FATAL_ERROR "Test 3 failed: expected 'test_arg' in output, got '${OUTPUT3}'")
endif()

# Test 4: try_run with compile definitions
try_run(RUN_RESULT4 COMPILE_RESULT4
    SOURCE_FROM_CONTENT test4.cpp "#include <iostream>\nint main() { \n#ifdef MY_DEFINE\nstd::cout << \"Defined\";\nreturn 0;\n#else\nreturn 1;\n#endif\n}"
    COMPILE_DEFINITIONS MY_DEFINE
    RUN_OUTPUT_VARIABLE OUTPUT4
)

message(STATUS "Test 4 - With compile definitions:")
message(STATUS "  Compile result: ${COMPILE_RESULT4}")
message(STATUS "  Run result: ${RUN_RESULT4}")
message(STATUS "  Output: ${OUTPUT4}")

if(NOT COMPILE_RESULT4)
    message(FATAL_ERROR "Test 4 failed: compilation failed")
endif()

if(NOT RUN_RESULT4 EQUAL 0)
    message(FATAL_ERROR "Test 4 failed: expected exit code 0, got ${RUN_RESULT4}")
endif()

if(NOT OUTPUT4 MATCHES "Defined")
    message(FATAL_ERROR "Test 4 failed: expected 'Defined' in output, got '${OUTPUT4}'")
endif()

# Test 5: try_run that fails to compile
try_run(RUN_RESULT5 COMPILE_RESULT5
    SOURCE_FROM_CONTENT test5.cpp "int main() { this_is_invalid_syntax"
    COMPILE_OUTPUT_VARIABLE COMPILE_OUTPUT5
)

message(STATUS "Test 5 - Compile failure:")
message(STATUS "  Compile result: ${COMPILE_RESULT5}")
message(STATUS "  Run result: ${RUN_RESULT5}")

if(COMPILE_RESULT5)
    message(FATAL_ERROR "Test 5 failed: expected compilation to fail")
endif()

if(NOT RUN_RESULT5 STREQUAL "FAILED_TO_RUN")
    message(FATAL_ERROR "Test 5 failed: expected FAILED_TO_RUN, got ${RUN_RESULT5}")
endif()

message(STATUS "All try_run tests passed!")
