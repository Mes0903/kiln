#!/bin/bash
set -e
DMAKE=$1

echo "=== ExternalProject Build-Time Test ==="

# Clean any previous build
rm -rf build

# Run dmake
"$DMAKE" 2>&1 | tee build.log

# Check: EP install output should exist (proves build-time execution)
if [ ! -f build/debug/install/lib/mylib.txt ]; then
    echo "FAIL: mylib.txt not found (EP BUILD_COMMAND didn't run)"
    cat build.log
    exit 1
fi

if [ ! -f build/debug/install/lib/mylib_installed.txt ]; then
    echo "FAIL: mylib_installed.txt not found (EP INSTALL_COMMAND didn't run)"
    exit 1
fi

# Check: Main app should have been built
if [ ! -f build/debug/main_app ]; then
    echo "FAIL: main_app not found (main executable wasn't built)"
    exit 1
fi

echo "=== ExternalProject Build-Time Test PASSED ==="
