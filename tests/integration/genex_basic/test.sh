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

# Verify compile_commands.json contains the expanded include path (not the unexpanded variable)
# BUILD_INTERFACE should expand ${CMAKE_CURRENT_SOURCE_DIR}/include to an actual path ending in /include
if ! grep -q '"-I[^"]*/include"' build/debug/compile_commands.json && \
   ! grep -q -- '-I[^ ]*/include ' build/debug/compile_commands.json; then
    echo "Error: BUILD_INTERFACE not expanded correctly - /include path not found"
    cat build/debug/compile_commands.json
    exit 1
fi

# Verify the unexpanded variable is NOT in the output (it should be expanded)
if grep -q 'CMAKE_CURRENT_SOURCE_DIR' build/debug/compile_commands.json; then
    echo "Error: CMAKE_CURRENT_SOURCE_DIR was not expanded"
    exit 1
fi

echo "All genex_basic tests passed!"
