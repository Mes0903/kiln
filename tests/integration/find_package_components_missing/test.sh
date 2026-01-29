#!/bin/bash

set -e

DMAKE=$1

if [ -z "$DMAKE" ]; then
    echo "Usage: $0 <path-to-dmake>"
    exit 1
fi

# Clean any previous build
rm -rf build

# This should FAIL because a required component is missing
if $DMAKE . -B build 2>&1 | grep -q "missing required components: MissingComp"; then
    echo "✓ Correctly detected missing required component"
else
    echo "✗ Failed to detect missing required component"
    exit 1
fi
