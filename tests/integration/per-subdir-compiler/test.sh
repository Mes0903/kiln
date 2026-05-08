#!/bin/bash
set -e

if ! command -v clang++ >/dev/null 2>&1; then
    echo "clang++ not installed; skipping"
    exit 0
fi

# This test asserts the top-level target uses GCC and the subdir
# overrides to clang. If the runner already forced CXX=clang++ at the
# top level, the contrast is gone — skip cleanly rather than fail.
case "${CXX:-}" in
    *clang*) echo "top-level CXX is already clang ($CXX); contrast not possible; skipping"; exit 0 ;;
esac

OUT=$("$1" 2>&1)
echo "$OUT" | tail -40

# Top-level target compiled with default g++ — its objects must contain
# GCC-version markers, not clang's.
HOST_OBJ=$(find build/debug -name 'main.cpp.o' | head -1)
SUB_OBJ=$(find build/debug -name 'subsource.cpp.o' | head -1)

[ -n "$HOST_OBJ" ] || { echo "main.cpp.o not found"; exit 1; }
[ -n "$SUB_OBJ" ]  || { echo "subsource.cpp.o not found"; exit 1; }

# Check producer marker in .comment section.
host_producer=$(strings "$HOST_OBJ" | grep -m1 -E "GCC|clang" || true)
sub_producer=$(strings "$SUB_OBJ"  | grep -m1 -E "GCC|clang" || true)

echo "host producer:  $host_producer"
echo "sub producer:   $sub_producer"

echo "$host_producer" | grep -qi "GCC"
echo "$sub_producer"  | grep -qi "clang"

# Sanity: the host_app actually runs.
[ -x build/debug/host_app ]
./build/debug/host_app | grep -q "host_app"
