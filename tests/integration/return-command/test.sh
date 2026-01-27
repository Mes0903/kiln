#!/bin/bash
set -e

DMAKE="$1"
TEST_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "Testing return() command behavior..."

# Test 1 & 2: Function and Macro return
echo ""
echo "Running function and macro tests..."
OUTPUT=$("$DMAKE" -P "$TEST_DIR/CMakeLists.txt" 2>&1)

# Check that function return worked correctly
if ! echo "$OUTPUT" | grep -q "Inside function before return"; then
    echo "FAIL: Function was not called"
    exit 1
fi

if echo "$OUTPUT" | grep -q "Inside function after return"; then
    echo "FAIL: Function did not return correctly"
    exit 1
fi

if ! echo "$OUTPUT" | grep -q "After function call - SHOULD PRINT"; then
    echo "FAIL: Script did not continue after function call"
    exit 1
fi

# Check that macro return worked correctly (exits the calling scope)
if ! echo "$OUTPUT" | grep -q "Inside macro before return"; then
    echo "FAIL: Macro was not called"
    exit 1
fi

if echo "$OUTPUT" | grep -q "Inside macro after return"; then
    echo "FAIL: Macro did not return correctly"
    exit 1
fi

if echo "$OUTPUT" | grep -q "After macro call"; then
    echo "FAIL: Script continued after macro return (should have exited)"
    exit 1
fi

echo "✓ Function and macro tests passed"

# Test 3: Include file return
echo ""
echo "Running include file test..."
OUTPUT=$("$DMAKE" -P "$TEST_DIR/test_include.cmake" 2>&1)

if ! echo "$OUTPUT" | grep -q "Main file: before include"; then
    echo "FAIL: Main file did not execute before include"
    exit 1
fi

if ! echo "$OUTPUT" | grep -q "Included file: start"; then
    echo "FAIL: Included file was not executed"
    exit 1
fi

if echo "$OUTPUT" | grep -q "Included file: after return"; then
    echo "FAIL: Included file did not return correctly"
    exit 1
fi

if ! echo "$OUTPUT" | grep -q "Main file: after include - SHOULD PRINT"; then
    echo "FAIL: Main file did not continue after include"
    exit 1
fi

echo "✓ Include file test passed"

# Test 4: Script body return
echo ""
echo "Running script body return test..."
OUTPUT=$("$DMAKE" -P "$TEST_DIR/test_script_return.cmake" 2>&1)

if ! echo "$OUTPUT" | grep -q "Script: before return"; then
    echo "FAIL: Script did not execute before return"
    exit 1
fi

if echo "$OUTPUT" | grep -q "Script: after return"; then
    echo "FAIL: Script did not return correctly"
    exit 1
fi

echo "✓ Script body return test passed"

echo ""
echo "All return() tests passed!"
exit 0
