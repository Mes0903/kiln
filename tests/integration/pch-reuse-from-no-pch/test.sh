#!/bin/bash
set -e

# Provider has no PCH — REUSE_FROM should be a silent no-op (matches CMake's
# behavior when a provider's PCH is gated by an option that's off).
"$1"

if [ ! -f "build/debug/reuser" ]; then
    echo "Reuser binary not found"
    exit 1
fi

# Neither target should have produced a .gch
if [ -f "build/debug/objs/provider_pch.hxx.gch" ] || [ -f "build/debug/objs/reuser_pch.hxx.gch" ]; then
    echo "No .gch should be produced when provider has no PRECOMPILE_HEADERS"
    exit 1
fi

./build/debug/reuser

echo "PCH REUSE_FROM no-pch (no-op) test passed!"
