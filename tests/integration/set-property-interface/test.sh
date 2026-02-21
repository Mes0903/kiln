#!/bin/bash
set -e

KILN=$1
cd "$(dirname "$0")"

echo "Building with kiln (set_property INTERFACE test)..."
$KILN app

echo "Running app..."
./build/debug/app

echo "Test passed!"
