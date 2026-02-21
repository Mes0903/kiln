#!/bin/bash
set -e

KILN=$1
cd "$(dirname "$0")"

echo "=== Testing directory properties ==="

# Run kiln - it should interpret the CMakeLists.txt and print messages
$KILN 2>&1 | tee output.txt

# Verify expected messages are present
grep -q "Single property: TestValue" output.txt || (echo "FAIL: Single property not set" && exit 1)
grep -q "Multiple properties: ValueA, ValueB, ValueC" output.txt || (echo "FAIL: Multiple properties not set" && exit 1)
grep -q "Overwritten property: NewValue" output.txt || (echo "FAIL: Property not overwritten" && exit 1)
grep -q "Non-existent property returns empty" output.txt || (echo "FAIL: Non-existent property check failed" && exit 1)
grep -q "PARENT_DIRECTORY empty for root" output.txt || (echo "FAIL: PARENT_DIRECTORY check failed" && exit 1)
grep -q "DIRECTORY keyword: Works" output.txt || (echo "FAIL: DIRECTORY keyword not working" && exit 1)
grep -q "Property via set_property: SetPropertyValue" output.txt || (echo "FAIL: set_property interaction failed" && exit 1)
grep -q "Include directories: /test/include" output.txt || (echo "FAIL: Accumulated property not working" && exit 1)
grep -q "DEFINITION keyword: DefinitionValue" output.txt || (echo "FAIL: DEFINITION keyword not working" && exit 1)
grep -q "Subdir PARENT_DIRECTORY:" output.txt || (echo "FAIL: PARENT_DIRECTORY not set in subdirectory" && exit 1)
grep -q "Parent property from subdir: NewValue" output.txt || (echo "FAIL: Cross-directory property access failed" && exit 1)
grep -q "Subdir property from parent: SubdirValue" output.txt || (echo "FAIL: Parent accessing subdir property failed" && exit 1)
grep -q "Directory Properties Test Complete" output.txt || (echo "FAIL: Test did not complete" && exit 1)

echo "PASS: All directory properties tests passed"
exit 0
