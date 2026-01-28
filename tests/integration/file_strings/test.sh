#!/bin/bash
set -e

DMAKE=$1
TEST_DIR=$(dirname "$0")

echo "Running file(STRINGS) test..."
cd "$TEST_DIR"

# Run dmake
$DMAKE

echo "✓ file(STRINGS) test passed"
