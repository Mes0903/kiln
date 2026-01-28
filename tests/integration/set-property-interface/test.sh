#!/bin/bash
set -e

DMAKE=$1
cd "$(dirname "$0")"

echo "Building with dmake (set_property INTERFACE test)..."
$DMAKE app

echo "Running app..."
./build/debug/app

echo "Test passed!"
