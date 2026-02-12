#!/bin/bash
set -e
DMAKE="$1"
DIR="$(cd "$(dirname "$0")" && pwd)"

output=$("$DMAKE" -P /dev/null -DCMAKE_CURRENT_SOURCE_DIR="$DIR" -- 2>&1 || true)

# Run dmake on the test project
output=$("$DMAKE" -C "$DIR" 2>&1)

echo "$output"

# Check absolute glob found both
if ! echo "$output" | grep -q "libs/alpha/CMakeLists.txt"; then
    echo "FAIL: absolute glob missing libs/alpha/CMakeLists.txt"
    exit 1
fi
if ! echo "$output" | grep -q "libs/beta/CMakeLists.txt"; then
    echo "FAIL: absolute glob missing libs/beta/CMakeLists.txt"
    exit 1
fi

# Check relative glob found both
if ! echo "$output" | grep -q "REL: libs/alpha/CMakeLists.txt;libs/beta/CMakeLists.txt"; then
    echo "FAIL: relative glob didn't produce expected result"
    # Show what we got
    echo "$output" | grep "REL:"
    exit 1
fi

echo "PASS: glob-wildcard-dirs"
