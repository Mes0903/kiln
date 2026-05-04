#!/bin/bash
set -e

if ! command -v clang >/dev/null 2>&1 || ! command -v clang++ >/dev/null 2>&1; then
    echo "clang not installed; skipping"
    exit 0
fi

OUT=$("$1" -DCMAKE_TOOLCHAIN_FILE=clang-toolchain.cmake 2>&1)
echo "$OUT"

# Toolchain file must produce Clang ID, not GNU.
echo "$OUT" | grep -q "Detected C compiler ID: Clang"
echo "$OUT" | grep -q "Detected CXX compiler ID: Clang"
echo "$OUT" | grep -q "C compiler binary: clang"

# Binary must build and run
[ -x build/debug/hello ]
./build/debug/hello | grep -q "Hello from clang"
