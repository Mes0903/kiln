#!/usr/bin/env bash
set -e

KILN=$1
TEST_DIR=$(cd "$(dirname "$0")" && pwd)
cd "$TEST_DIR"
rm -rf build

# C++20 modules via P1689r5 dependency scanning: GCC 14+ only.
CXX_BIN=${CXX:-g++}
if ! command -v "$CXX_BIN" >/dev/null 2>&1; then
    echo "$CXX_BIN not found; skipping"
    exit 0
fi
if ! "$CXX_BIN" --version 2>/dev/null | head -n1 | grep -qi 'g++\|gcc'; then
    echo "$CXX_BIN is not GCC; kiln requires GCC for C++ modules; skipping"
    exit 0
fi
gcc_major=$("$CXX_BIN" -dumpfullversion -dumpversion 2>/dev/null | cut -d. -f1)
if [ -z "$gcc_major" ] || [ "$gcc_major" -lt 14 ]; then
    echo "GCC $gcc_major lacks -fdeps-format=p1689r5 (need 14+); skipping"
    exit 0
fi

# Build all targets
$KILN test_basic test_modules test_lib

# Run the basic test
./build/debug/test_basic

# Run the modules test
./build/debug/test_modules

# Verify library was built
if [ ! -f build/debug/libtest_lib.a ]; then
    echo "ERROR: Library not built"
    exit 1
fi

echo "All tests passed!"
