#!/bin/bash
# Test export() and install(EXPORT) functionality

set -e

KILN="$1"
TEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Clean up any previous test
rm -rf "$TEST_DIR/build"

cd "$TEST_DIR"

# Build with kiln
"$KILN" 2>&1

# Check that the export file was generated
EXPORT_FILE="$TEST_DIR/build/debug/MyLibTargets.cmake"
if [ ! -f "$EXPORT_FILE" ]; then
    echo "FAIL: Export file not generated at $EXPORT_FILE"
    exit 1
fi

# Verify the export file contains expected content
if ! grep -q "add_library(MyLib::mylib" "$EXPORT_FILE"; then
    echo "FAIL: Export file missing MyLib::mylib target"
    cat "$EXPORT_FILE"
    exit 1
fi

if ! grep -q "add_library(MyLib::myinterface" "$EXPORT_FILE"; then
    echo "FAIL: Export file missing MyLib::myinterface target"
    cat "$EXPORT_FILE"
    exit 1
fi

if ! grep -q "INTERFACE_INCLUDE_DIRECTORIES" "$EXPORT_FILE"; then
    echo "FAIL: Export file missing INTERFACE_INCLUDE_DIRECTORIES"
    cat "$EXPORT_FILE"
    exit 1
fi

if ! grep -q "INTERFACE_COMPILE_DEFINITIONS" "$EXPORT_FILE"; then
    echo "FAIL: Export file missing INTERFACE_COMPILE_DEFINITIONS"
    cat "$EXPORT_FILE"
    exit 1
fi

if ! grep -q "MYLIB_ENABLED" "$EXPORT_FILE"; then
    echo "FAIL: Export file missing MYLIB_ENABLED definition"
    cat "$EXPORT_FILE"
    exit 1
fi

# Check for CMake-compatible format elements
if ! grep -q "cmake_policy(PUSH)" "$EXPORT_FILE"; then
    echo "FAIL: Export file missing cmake_policy header"
    cat "$EXPORT_FILE"
    exit 1
fi

if ! grep -q "cmake_policy(POP)" "$EXPORT_FILE"; then
    echo "FAIL: Export file missing cmake_policy footer"
    cat "$EXPORT_FILE"
    exit 1
fi

if ! grep -q "CMAKE_IMPORT_FILE_VERSION" "$EXPORT_FILE"; then
    echo "FAIL: Export file missing CMAKE_IMPORT_FILE_VERSION"
    cat "$EXPORT_FILE"
    exit 1
fi

if ! grep -q "IMPORTED_CONFIGURATIONS" "$EXPORT_FILE"; then
    echo "FAIL: Export file missing IMPORTED_CONFIGURATIONS"
    cat "$EXPORT_FILE"
    exit 1
fi

# Check for per-configuration IMPORTED_LOCATION (should have _DEBUG or similar suffix)
if ! grep -q "IMPORTED_LOCATION_" "$EXPORT_FILE"; then
    echo "FAIL: Export file missing per-config IMPORTED_LOCATION (e.g., IMPORTED_LOCATION_DEBUG)"
    cat "$EXPORT_FILE"
    exit 1
fi

# Check for expected targets protection
if ! grep -q "_cmake_targets_defined" "$EXPORT_FILE"; then
    echo "FAIL: Export file missing expected targets check"
    cat "$EXPORT_FILE"
    exit 1
fi

# Verify the executable was built
if [ ! -f "$TEST_DIR/build/debug/myapp" ]; then
    echo "FAIL: Executable not built"
    exit 1
fi

# Run the executable
if ! "$TEST_DIR/build/debug/myapp"; then
    echo "FAIL: Executable returned non-zero"
    exit 1
fi

echo "PASS: export-basic test passed"
exit 0
