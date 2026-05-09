#!/bin/bash
set -e

"$1"

for bin in head middle tail; do
    if [ ! -f "build/debug/$bin" ]; then
        echo "Binary $bin not found"
        exit 1
    fi
    "./build/debug/$bin" | grep -q "$bin"
done

# Only the chain head should own a .gch
if [ ! -f "build/debug/objs/head_pch.hxx.gch" ]; then
    echo "Head PCH .gch not found"
    exit 1
fi

for bin in middle tail; do
    if [ -f "build/debug/objs/${bin}_pch.hxx.gch" ]; then
        echo "$bin should not have its own .gch (chained REUSE_FROM)"
        exit 1
    fi
done

echo "PCH REUSE_FROM chain test passed!"
