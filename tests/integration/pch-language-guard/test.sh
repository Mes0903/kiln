#!/bin/bash
set -e

"$1"

# Check that the shared library was built
if [[ ! -f build/debug/libmylib.so ]]; then
    echo "FAIL: libmylib.so not found"
    exit 1
fi

# Check that the executable was built
if [[ ! -f build/debug/myapp ]]; then
    echo "FAIL: myapp not found"
    exit 1
fi

# Run and verify output
OUTPUT=$(./build/debug/myapp)
if [[ "$OUTPUT" != "PASS" ]]; then
    echo "FAIL: unexpected output: $OUTPUT"
    exit 1
fi

echo "All PCH language guard tests passed!"
