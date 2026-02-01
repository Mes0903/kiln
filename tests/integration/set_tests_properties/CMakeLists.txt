cmake_minimum_required(VERSION 3.15)
project(SetTestsPropertiesTest)

enable_testing()

# Test 1: Normal passing test
add_executable(test_pass test_pass.cpp)
add_test(NAME test_pass COMMAND test_pass)

# Test 2: Test with skip return code
add_executable(test_skip test_skip.cpp)
add_test(NAME test_skip COMMAND test_skip)
set_tests_properties(test_skip PROPERTIES SKIP_RETURN_CODE 77)

# Test 3: Multiple properties on one test
add_executable(test_multi test_multi.cpp)
add_test(NAME test_multi COMMAND test_multi)
set_tests_properties(test_multi PROPERTIES
    TIMEOUT 10
    SKIP_RETURN_CODE 77
)

# Test 4: Multiple tests with same properties
add_executable(test_a test_a.cpp)
add_executable(test_b test_b.cpp)
add_test(NAME test_a COMMAND test_a)
add_test(NAME test_b COMMAND test_b)
set_tests_properties(test_a test_b PROPERTIES SKIP_RETURN_CODE 99)
