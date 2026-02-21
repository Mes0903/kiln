#!/bin/bash
set -e

KILN=$1
TEST_DIR=$(dirname "$0")

echo "Running file(DOWNLOAD) and file(ARCHIVE_EXTRACT) test..."
cd "$TEST_DIR"

# Clean previous runs
rm -rf build

# Run kiln
$KILN

echo "file(DOWNLOAD) and file(ARCHIVE_EXTRACT) test passed"
