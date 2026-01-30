#!/bin/bash
set -e

DMAKE=$1

# Build the project
$DMAKE

# Verify executables were built
test -f build/debug/test_std17
test -f build/debug/test_lambdas
test -f build/debug/test_std14
test -f build/debug/test_propagation
test -f build/debug/test_override
test -f build/debug/test_c

# Run the tests
./build/debug/test_std17
./build/debug/test_lambdas
./build/debug/test_std14
./build/debug/test_propagation
./build/debug/test_override
./build/debug/test_c

echo "All compile features tests passed!"
