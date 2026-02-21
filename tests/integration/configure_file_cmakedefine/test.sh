#!/bin/bash
set -e

KILN=$1

# Build the project
$KILN

# Run the test executable
./build/debug/test_config

echo "configure_file_cmakedefine test passed"
