#!/bin/bash
set -e

DMAKE=$1

# Build the project
$DMAKE

# Run the test executable
./build/debug/test_config

echo "configure_file_cmakedefine test passed"
