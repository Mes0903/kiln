#!/bin/bash
set -e

# Run kiln (first argument is the kiln binary path)
"$1"

# Verify the executable was built
if [ ! -f build/debug/find_test ]; then
    echo "Executable 'find_test' not found"
    exit 1
fi

# Run the executable to verify it works and the math library was linked
./build/debug/find_test | grep -q "sqrt(16) = 4"

echo "Find commands integration test passed!"
