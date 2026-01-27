#!/bin/bash
set -e

DMAKE=$1
TEST_DIR=$(cd "$(dirname "$0")" && pwd)

cd "$TEST_DIR"

# Clean any previous build
rm -rf build

# Build with dmake
echo "Building modules_basic with dmake..."
$DMAKE -j4

# Check that output exists
if [ ! -f build/debug/modules_basic_app ]; then
    echo "Error: modules_basic_app not built"
    exit 1
fi

# Run the executable
echo "Running modules_basic_app..."
./build/debug/modules_basic_app

echo "Test passed!"
