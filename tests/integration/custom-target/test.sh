#!/bin/bash
set -e
KILN=$1

# 1. Run default build (should only run all_target)
"$KILN"
if [ ! -f build/debug/all_produced.txt ]; then
    echo "all_produced.txt not found (all_target didn't run)"
    exit 1
fi

if [ -f build/debug/produced_file.txt ]; then
    echo "produced_file.txt found (simple_target should not have run)"
    exit 1
fi

# 2. Run explicit target (should run simple_target and dependent_target)
"$KILN" build dependent_target

if [ ! -f build/debug/produced_file.txt ]; then
    echo "produced_file.txt not found (simple_target didn't run as dependency)"
    exit 1
fi

if [ ! -f build/debug/dependent_produced.txt ]; then
    echo "dependent_produced.txt not found (dependent_target didn't run)"
    exit 1
fi

echo "Custom target integration test PASSED"
