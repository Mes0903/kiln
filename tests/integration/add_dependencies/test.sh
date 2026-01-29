#!/bin/bash
set -e

DMAKE=$1

echo "Testing add_dependencies()..."

# Clean and build
rm -rf build
$DMAKE

# Run the executable - it should have access to the generated header
./build/debug/app

echo "add_dependencies test passed!"
