#!/bin/bash
set -e

if ! command -v tcc >/dev/null 2>&1; then
    echo "tcc not installed; skipping"
    exit 0
fi

# Configure + build with TCC pinned as the C compiler.
"$1" -DCMAKE_C_COMPILER=tcc

[ -x build/debug/hello ] || { echo "build/debug/hello not produced"; exit 1; }
[ -f build/debug/libutil.a ] || { echo "libutil.a not produced"; exit 1; }

# The static archive must come from `tcc -ar` (TCC's built-in archiver),
# not GNU ar. They produce slightly different headers; we just sanity-check
# the binary runs and prints what we expect.
./build/debug/hello | grep -q "hello 42"
