#!/bin/bash
set -e

KILN=$1

echo "Testing directory properties (add_definitions, add_compile_definitions, etc.)..."

# Build the project
$KILN

# Test 1: Retroactive application
echo "Testing retroactive application (target before add_definitions)..."
./build/debug/before | grep -q "before: TEST_VALUE=100 MODERN_DEF=200 COMPILE_OPTION=300"

# Test 2: Normal application
echo "Testing normal application (target after add_definitions)..."
./build/debug/after | grep -q "after: TEST_VALUE=100 MODERN_DEF=200 COMPILE_OPTION=300"

# Test 3: link_libraries
echo "Testing link_libraries..."
./build/debug/linked | grep -q "linked: Successfully called library function, got 42"

# Test 4: Subdirectory inheritance
echo "Testing subdirectory inheritance..."
./build/debug/sub/child | grep -q "child: TEST_VALUE=100 MODERN_DEF=200 COMPILE_OPTION=300 CHILD_VALUE=400 lib_value=42"

echo "All tests passed!"
