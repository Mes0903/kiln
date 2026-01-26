#!/bin/bash
set -e

# Initial build
"$1" .

BINARY="build/debug/pch_test"
OBJ="build/debug/objs/main.cpp.o"

if [ ! -f "$BINARY" ]; then
    echo "Binary not found after first build"
    exit 1
fi

if [ ! -f "$OBJ" ]; then
    echo "Object file not found after first build"
    exit 1
fi

OBJ_TS1=$(stat -c %Y "$OBJ")
BIN_TS1=$(stat -c %Y "$BINARY")

# Second run (should be incremental - no rebuild)
"$1" .
OBJ_TS2=$(stat -c %Y "$OBJ")
BIN_TS2=$(stat -c %Y "$BINARY")

if [ "$OBJ_TS1" != "$OBJ_TS2" ]; then
    echo "Error: Object file was rebuilt on incremental build (no changes)"
    exit 1
fi

if [ "$BIN_TS1" != "$BIN_TS2" ]; then
    echo "Error: Binary was rebuilt on incremental build (no changes)"
    exit 1
fi

# Modify PCH header - should trigger object and binary rebuild
sleep 1
echo -e "#pragma once\n\n#include <iostream>\n// Modified" > pch.hpp
"$1" .

OBJ_TS3=$(stat -c %Y "$OBJ")
BIN_TS3=$(stat -c %Y "$BINARY")

if [ "$OBJ_TS1" == "$OBJ_TS3" ]; then
    echo "Error: Object file was NOT rebuilt after PCH header changed"
    exit 1
fi

if [ "$BIN_TS1" == "$BIN_TS3" ]; then
    echo "Error: Binary was NOT rebuilt after PCH header changed"
    exit 1
fi

# Verify the binary still works
if ! ./build/debug/pch_test | grep -q "Hello, World!"; then
    echo "Error: Binary output is incorrect"
    exit 1
fi

echo "All PCH dependency tracking tests passed!"
