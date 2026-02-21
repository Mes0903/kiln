#!/bin/bash
set -e

KILN="$1"
DIR="$(cd "$(dirname "$0")" && pwd)"

# Clean up
rm -rf "$DIR/build"

# Build
cd "$DIR"
"$KILN" --config debug -j 1

# Verify the executable was built
if [ ! -f "$DIR/build/debug/myapp" ]; then
    echo "ERROR: myapp not found"
    exit 1
fi

# Verify generated.cpp was created
if [ ! -f "$DIR/build/debug/generated.cpp" ]; then
    echo "ERROR: generated.cpp not found"
    exit 1
fi

# Verify config.h was created
if [ ! -f "$DIR/build/debug/config.h" ]; then
    echo "ERROR: config.h not found"
    exit 1
fi

# Verify POST_BUILD marker was created
if [ ! -f "$DIR/build/debug/build_marker.txt" ]; then
    echo "ERROR: build_marker.txt not found (POST_BUILD failed)"
    exit 1
fi

# Verify PRE_BUILD marker was created
if [ ! -f "$DIR/build/debug/pre_marker.txt" ]; then
    echo "ERROR: pre_marker.txt not found (PRE_BUILD failed)"
    exit 1
fi

# Run the executable and verify output
OUTPUT=$("$DIR/build/debug/myapp")
if [[ "$OUTPUT" != *"42"* ]]; then
    echo "ERROR: Expected output to contain '42', got: $OUTPUT"
    exit 1
fi

if [[ "$OUTPUT" != *"123"* ]]; then
    echo "ERROR: Expected output to contain '123', got: $OUTPUT"
    exit 1
fi

echo "All custom_command tests passed!"
