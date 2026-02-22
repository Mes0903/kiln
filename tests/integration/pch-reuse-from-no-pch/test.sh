#!/bin/bash
set -e

# Provider has no PCH - should fail with an error
if "$1" 2>&1; then
    echo "Expected build to fail but it succeeded"
    exit 1
fi

echo "PCH REUSE_FROM no-pch error test passed!"
