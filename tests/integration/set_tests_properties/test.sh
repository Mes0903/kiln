#!/bin/bash
set -e

KILN=$1

echo "Testing set_tests_properties with SKIP_RETURN_CODE..."

# Build all test executables
$KILN || exit 1

# Run test that should pass
echo "Running test_pass..."
$KILN test test_pass > /tmp/test_output.txt 2>&1
if ! grep -q "PASSED" /tmp/test_output.txt; then
    echo "Error: test_pass should have passed"
    cat /tmp/test_output.txt
    exit 1
fi

# Run test that should be skipped
echo "Running test_skip..."
$KILN test test_skip > /tmp/test_output.txt 2>&1
if ! grep -q "SKIPPED" /tmp/test_output.txt; then
    echo "Error: test_skip should have been skipped"
    cat /tmp/test_output.txt
    exit 1
fi

# Run test with multiple properties
echo "Running test_multi..."
$KILN test test_multi > /tmp/test_output.txt 2>&1
if ! grep -q "PASSED" /tmp/test_output.txt; then
    echo "Error: test_multi should have passed"
    cat /tmp/test_output.txt
    exit 1
fi

# Run multiple tests with same properties
echo "Running test_a and test_b..."
$KILN test "test_[ab]" > /tmp/test_output.txt 2>&1
passed_count=$(grep -c "PASSED" /tmp/test_output.txt || true)
if [ "$passed_count" != "2" ]; then
    echo "Error: Both test_a and test_b should have passed"
    cat /tmp/test_output.txt
    exit 1
fi

echo "All set_tests_properties tests passed!"
exit 0
