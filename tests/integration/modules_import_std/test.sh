#!/bin/bash
set -e

KILN=$1
TEST_DIR=$(cd "$(dirname "$0")" && pwd)

cd "$TEST_DIR"

# `import std;` requires GCC 15+ which ships libstdc++.modules.json.
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
if [ -z "$gcc_major" ] || [ "$gcc_major" -lt 15 ]; then
    echo "GCC $gcc_major lacks libstdc++.modules.json (need 15+); skipping"
    exit 0
fi
modules_json=$("$CXX_BIN" -print-file-name=libstdc++.modules.json 2>/dev/null)
if [ ! -f "$modules_json" ]; then
    echo "libstdc++.modules.json not found at '$modules_json'; skipping"
    exit 0
fi

rm -rf build

echo "Building modules_import_std with kiln..."
$KILN -j4

if [ ! -f build/debug/import_std_app ]; then
    echo "Error: import_std_app not built"
    exit 1
fi

echo "Running import_std_app..."
out=$(./build/debug/import_std_app)
echo "$out"
echo "$out" | grep -q "sum=10"

echo "Test passed!"
