#!/bin/bash
set -e

KILN="$1"
DIR="$(cd "$(dirname "$0")" && pwd)"

rm -rf "$DIR/build"

cd "$DIR"
"$KILN" --config debug -j 1

# If we got here, compilation succeeded — the genex resolved correctly.
# consumer.cpp would fail to compile without include_a and FROM_PROVIDER.
# pub_consumer.cpp would fail to compile without include_b.
echo "  PASS: INTERFACE_INCLUDE_DIRECTORIES via genex"
echo "  PASS: INTERFACE_COMPILE_DEFINITIONS via genex"
echo "  PASS: PUBLIC include dirs visible via INTERFACE_ genex"

echo "All interface_property_genex tests passed!"
