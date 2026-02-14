#!/bin/bash
set -e

# find_package with REQUIRED for a nonexistent package must fail
if "$1" 2>&1; then
    echo "FAIL: find_package(REQUIRED) should have failed for nonexistent package"
    exit 1
fi

echo "REQUIRED test passed!"
