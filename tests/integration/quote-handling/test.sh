#!/bin/bash
set -e
KILN=$1

"$KILN"

# Verify custom command stripped quotes: should be -flag=hello, not -flag="hello"
OUTPUT=$(cat build/debug/quote_output.txt)
if echo "$OUTPUT" | grep -q '"'; then
    echo "FAIL: custom command args contain literal quotes"
    echo "Got: $OUTPUT"
    exit 1
fi
if ! echo "$OUTPUT" | grep -q '\-flag=hello'; then
    echo "FAIL: expected -flag=hello in output"
    echo "Got: $OUTPUT"
    exit 1
fi

# Verify compile definition preserved quotes (executable compiled and runs)
DEF_OUTPUT=$(./build/debug/test_def)
if [ "$DEF_OUTPUT" != "v1.0.0" ]; then
    echo "FAIL: expected 'v1.0.0' from test_def, got '$DEF_OUTPUT'"
    exit 1
fi

echo "Quote handling integration test PASSED"
