#!/bin/bash
set -e

DMAKE=$1

# Test Debug build
echo "Testing Debug build..."
$DMAKE --config debug
./build/debug/test_exe | grep "Debug"

# Test Release build
echo "Testing Release build..."
$DMAKE --config release
./build/release/test_exe | grep "Release"

# Verify compile_commands.json contains the right include path
if ! grep -q "include.*CMAKE_CURRENT_SOURCE_DIR" build/debug/compile_commands.json; then
    echo "Error: BUILD_INTERFACE not expanded correctly"
    exit 1
fi

echo "All genex_basic tests passed!"
