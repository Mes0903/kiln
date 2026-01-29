#!/bin/bash

set -e

DMAKE=$1

if [ -z "$DMAKE" ]; then
    echo "Usage: $0 <path-to-dmake>"
    exit 1
fi

# Clean any previous build
rm -rf build

# Run dmake (which will execute the CMakeLists.txt)
# The CMakeLists.txt contains all the test logic and will fail if any test fails
$DMAKE . -B build

echo "✓ find_package COMPONENTS test passed"
