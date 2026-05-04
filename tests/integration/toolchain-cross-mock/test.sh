#!/bin/bash
set -e

if ! command -v clang >/dev/null 2>&1; then
    echo "clang not installed; skipping"
    exit 0
fi
if ! clang --print-targets 2>/dev/null | grep -q riscv32; then
    echo "clang has no riscv32 backend; skipping"
    exit 0
fi

OUT=$("$1" -DCMAKE_TOOLCHAIN_FILE=cross.cmake 2>&1)
echo "$OUT"

# Compiler ID came from real macro probe of clang (not hardcoded GNU).
echo "$OUT" | grep -q "C compiler ID: Clang"
# System info derived from target macros, not host uname.
echo "$OUT" | grep -q "System: Generic/riscv32"
# 32-bit pointer reported via __SIZEOF_POINTER__.
echo "$OUT" | grep -q "sizeof(void\*): 4"
echo "$OUT" | grep -q "Compiler target: riscv32-unknown-none-elf"

# Produced object must be a RISC-V 32-bit ELF.
OBJ=$(find build/debug -name 'freestanding.c.o' | head -1)
if [ -z "$OBJ" ]; then
    echo "freestanding.c.o not found"
    find build/debug -type f
    exit 1
fi
file "$OBJ"
file "$OBJ" | grep -qi "RISC-V"
file "$OBJ" | grep -qi "32-bit"
