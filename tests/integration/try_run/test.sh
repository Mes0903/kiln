#!/bin/bash
set -e

KILN=$1

cd "$(dirname "$0")"

# Run kiln (which will execute the CMakeLists.txt and run all tests)
OUTPUT=$($KILN 2>&1)

if echo "$OUTPUT" | grep -q "All try_run tests passed!"; then
    echo "✓ try_run test passed"
    exit 0
else
    echo "✗ try_run test failed"
    echo "$OUTPUT"
    exit 1
fi
