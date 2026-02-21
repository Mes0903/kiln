#!/bin/bash
set -e

KILN=$1
TEST_DIR=$(dirname "$0")

echo "Running file(STRINGS) test..."
cd "$TEST_DIR"

# Run kiln
$KILN

echo "✓ file(STRINGS) test passed"
