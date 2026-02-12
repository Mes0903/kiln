#!/bin/bash
set -e
DMAKE=$1

echo "=== ExternalProject Build-Time Test ==="

# Clean any previous build
rm -rf build

# Run dmake
"$DMAKE" 2>&1 | tee build.log

# Check: EP library should have been built (in EP's binary dir)
if [ ! -f build/debug/_ep/mylib/libmylib.a ]; then
    echo "FAIL: libmylib.a not found in EP build dir (EP tasks didn't run)"
    cat build.log
    exit 1
fi

# Check: Main app should have been built
if [ ! -f build/debug/main_app ]; then
    echo "FAIL: main_app not found (main executable wasn't built)"
    exit 1
fi

# Run the main app to verify it works
OUTPUT=$(./build/debug/main_app)
if [[ "$OUTPUT" != "Value from mylib: 42" ]]; then
    echo "FAIL: main_app output unexpected: $OUTPUT"
    exit 1
fi

echo "Output: $OUTPUT"
echo "=== ExternalProject Build-Time Test PASSED ==="
