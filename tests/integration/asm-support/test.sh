#!/bin/bash
set -e

# Skip on non-x86_64 (matches the CMake-level skip)
if [ "$(uname -m)" != "x86_64" ]; then
    echo "SKIP: not x86_64"
    exit 0
fi

"$1"
./build/debug/asm_test | grep -q "ASM test passed"
