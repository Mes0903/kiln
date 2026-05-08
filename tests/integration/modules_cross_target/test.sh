#!/usr/bin/env bash
set -e

KILN=$1
TEST_DIR=$(cd "$(dirname "$0")" && pwd)
cd "$TEST_DIR"

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

rm -rf build

echo "Building modules_cross_target with kiln..."
$KILN -j4

if [ ! -f build/debug/app ]; then
    echo "Error: app not built"
    exit 1
fi

echo "Running app..."
./build/debug/app

echo "Test passed!"
