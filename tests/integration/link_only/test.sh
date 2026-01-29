#!/bin/bash
set -e

DMAKE=$1

echo "Testing LINK_ONLY generator expression..."

# Clean and build
rm -rf build
$DMAKE

# Run both apps
echo "Running normal_app (should have MYLIB_ENABLED)..."
./build/debug/normal_app

echo "Running link_only_app (should NOT have MYLIB_ENABLED)..."
./build/debug/link_only_app

echo "LINK_ONLY test passed!"
