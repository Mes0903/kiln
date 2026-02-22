#!/bin/bash
set -e

"$1"

# Both binaries should exist
if [ ! -f "build/debug/provider" ]; then
    echo "Provider binary not found"
    exit 1
fi

if [ ! -f "build/debug/reuser" ]; then
    echo "Reuser binary not found"
    exit 1
fi

# Both should run correctly
./build/debug/provider | grep -q "provider"
./build/debug/reuser | grep -q "reuser"

# Provider should have its own .gch
if [ ! -f "build/debug/objs/provider_pch.hpp.gch" ]; then
    echo "Provider PCH .gch not found"
    exit 1
fi

# Reuser should NOT have its own .gch
if [ -f "build/debug/objs/reuser_pch.hpp.gch" ]; then
    echo "Reuser should not have its own .gch file"
    exit 1
fi

echo "PCH REUSE_FROM test passed!"
