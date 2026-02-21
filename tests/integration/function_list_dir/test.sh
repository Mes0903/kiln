#!/bin/bash
set -e

KILN=$1
cd "$(dirname "$0")"

# Run kiln - it will fail if any messages are FATAL_ERROR
output=$($KILN 2>&1)

# Check that function name is reported correctly
echo "$output" | grep -q "Function name: test_function_from_subdir" || {
    echo "FAILED: Function name not reported correctly"
    echo "Output: $output"
    exit 1
}

# Check that function dir ends with "subdir" (not main dir)
echo "$output" | grep "Function dir:" | grep -q "subdir" || {
    echo "FAILED: Function directory should end with 'subdir'"
    echo "Output: $output"
    exit 1
}

# Check success messages
echo "$output" | grep -q "SUCCESS: Can access files" || {
    echo "FAILED: Function could not access files relative to definition"
    echo "Output: $output"
    exit 1
}

echo "$output" | grep -q "SUCCESS: CMAKE_CURRENT_FUNCTION_LIST_DIR is correct" || {
    echo "FAILED: CMAKE_CURRENT_FUNCTION_LIST_DIR verification failed"
    echo "Output: $output"
    exit 1
}

echo "Test passed"
