#!/bin/bash
set -e

# REUSE_FROM is declared before provider gets its PCH - should still work
"$1"

./build/debug/provider | grep -q "provider"
./build/debug/reuser | grep -q "reuser"

echo "PCH REUSE_FROM order independence test passed!"
