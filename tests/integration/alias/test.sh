#!/bin/bash
set -e

DMAKE=$1
TEST_DIR=$(dirname "$0")

echo "=== Testing ALIAS support ==="

# Clean first
cd "$TEST_DIR"
$DMAKE clean

# Build the project (should handle aliases correctly)
$DMAKE

# Verify outputs exist
if [ ! -f "build/debug/myapp" ]; then
    echo "ERROR: myapp executable not found"
    exit 1
fi

if [ ! -f "build/debug/libmylib.a" ]; then
    echo "ERROR: libmylib.a not found"
    exit 1
fi

if [ ! -f "build/debug/libutils.a" ]; then
    echo "ERROR: libutils.a not found"
    exit 1
fi

# Run the executable
./build/debug/myapp
if [ $? -ne 0 ]; then
    echo "ERROR: myapp failed to run"
    exit 1
fi

# Test building with alias name (should resolve to real target)
$DMAKE MyNamespace::App
if [ $? -ne 0 ]; then
    echo "ERROR: Failed to build using alias name"
    exit 1
fi

# Clean and test building library alias
$DMAKE clean
$DMAKE MyNamespace::MyLib
if [ ! -f "build/debug/libmylib.a" ]; then
    echo "ERROR: Failed to build library using alias name"
    exit 1
fi

echo "SUCCESS: All alias tests passed!"
exit 0
