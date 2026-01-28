#!/bin/bash
set -e

DMAKE=$1
cd "$(dirname "$0")"

echo "=== Testing property system ==="

# Run dmake - it should interpret the CMakeLists.txt and print messages
$DMAKE . 2>&1 | tee output.txt

# Verify expected messages are present
grep -q "Global property: GlobalValue" output.txt || (echo "FAIL: Global property not set" && exit 1)
grep -q "Global property defined: 1" output.txt || (echo "FAIL: Global property DEFINED query failed" && exit 1)
grep -q "Global property set: 1" output.txt || (echo "FAIL: Global property SET query failed" && exit 1)
grep -q "Global property after append: GlobalValue;AppendedValue" output.txt || (echo "FAIL: Global property APPEND failed" && exit 1)
grep -q "Directory property: DirValue" output.txt || (echo "FAIL: Directory property not set" && exit 1)
grep -q "Target property: TargetValue" output.txt || (echo "FAIL: Target property not set" && exit 1)
grep -q "Target property after append: TargetValue;AppendedTargetValue" output.txt || (echo "FAIL: Target property APPEND failed" && exit 1)
grep -q "Source property: SourceValue" output.txt || (echo "FAIL: Source property not set" && exit 1)
grep -q "Global property brief docs: A custom global property" output.txt || (echo "FAIL: BRIEF_DOCS retrieval failed" && exit 1)
grep -q "Global property full docs: This is a test property for global scope" output.txt || (echo "FAIL: FULL_DOCS retrieval failed" && exit 1)
grep -q "Property not found correctly returns empty" output.txt || (echo "FAIL: Property not found check failed" && exit 1)
grep -q "Property System Test Complete" output.txt || (echo "FAIL: Test did not complete" && exit 1)

# Check that the library was built
if [ ! -f "build/debug/libtestlib.a" ]; then
    echo "FAIL: Library was not built"
    exit 1
fi

echo "PASS: All property system tests passed"
exit 0
