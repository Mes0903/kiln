#!/bin/bash
set -e

DMAKE=$1
cd "$(dirname "$0")"

echo "=== Testing set_property clearing inherited directory properties ==="

$DMAKE 2>&1 | tee output.txt

# Parent has both includes
grep -q "Parent INCLUDE_DIRECTORIES: /fake/parent/inc1;/fake/parent/inc2" output.txt || (echo "FAIL: parent includes" && exit 1)

# child1 inherits parent includes
grep -q "child1 inherited: /fake/parent/inc1;/fake/parent/inc2" output.txt || (echo "FAIL: child1 inheritance" && exit 1)

# child1 appends to inherited
grep -q "child1 after add: /fake/parent/inc1;/fake/parent/inc2;/fake/child1/inc" output.txt || (echo "FAIL: child1 append" && exit 1)

# child2 inherits before clearing
grep -q "child2 before clear: /fake/parent/inc1;/fake/parent/inc2" output.txt || (echo "FAIL: child2 pre-clear" && exit 1)

# child2 is empty after clearing
grep -q "child2 after clear: $" output.txt || (echo "FAIL: child2 post-clear not empty" && exit 1)

# child2 has only its own include after clearing and adding
grep -q "child2 final: /fake/child2/inc" output.txt || (echo "FAIL: child2 final" && exit 1)

echo "PASS: set_property clearing inherited properties works correctly"
exit 0
