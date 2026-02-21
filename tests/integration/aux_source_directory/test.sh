#!/bin/bash
set -e

KILN="$1"

# Build the project
"$KILN"

# Run the executable
./build/debug/test_aux_source

# Verify output
./build/debug/test_aux_source | grep -q "foo() = 42"
./build/debug/test_aux_source | grep -q "bar() = 7"
./build/debug/test_aux_source | grep -q "baz() = 13"

echo "aux_source_directory test passed!"
