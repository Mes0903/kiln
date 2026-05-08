#!/bin/bash
set -e

KILN=$1
TEST_DIR=$(cd "$(dirname "$0")" && pwd)

cd "$TEST_DIR"

# C++20 modules: GCC >=14 (-fdeps-format=p1689r5) or Clang with clang-scan-deps.
CXX_BIN=${CXX:-g++}
if ! command -v "$CXX_BIN" >/dev/null 2>&1; then
    echo "$CXX_BIN not found; skipping"
    exit 0
fi
ver=$("$CXX_BIN" --version 2>/dev/null | head -n1)
case "$ver" in
    *clang*)
        if ! command -v clang-scan-deps >/dev/null 2>&1; then
            echo "clang-scan-deps not installed; skipping"
            exit 0
        fi
        ;;
    *g++*|*GCC*|*gcc*)
        gcc_major=$("$CXX_BIN" -dumpfullversion -dumpversion 2>/dev/null | cut -d. -f1)
        if [ -z "$gcc_major" ] || [ "$gcc_major" -lt 14 ]; then
            echo "GCC $gcc_major lacks -fdeps-format=p1689r5 (need 14+); skipping"
            exit 0
        fi
        ;;
    *)
        echo "$CXX_BIN is neither GCC nor Clang; skipping"
        exit 0
        ;;
esac

# Clean any previous build
rm -rf build

# Build with kiln
echo "Building modules_basic with kiln..."
$KILN -j4

# Check that output exists
if [ ! -f build/debug/modules_basic_app ]; then
    echo "Error: modules_basic_app not built"
    exit 1
fi

# Run the executable
echo "Running modules_basic_app..."
./build/debug/modules_basic_app

echo "Test passed!"
