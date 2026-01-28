#!/usr/bin/env bash
set -e

DMAKE=$1

# Build all targets
$DMAKE test_basic test_modules test_lib

# Run the basic test
./build/debug/test_basic

# Run the modules test
./build/debug/test_modules

# Verify library was built
if [ ! -f build/debug/libtest_lib.a ]; then
    echo "ERROR: Library not built"
    exit 1
fi

echo "All tests passed!"
