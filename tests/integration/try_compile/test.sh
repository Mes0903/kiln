#!/bin/bash
set -e

KILN=$1
cd "$(dirname "$0")"

# Clean any previous build
rm -rf build

# Run kiln (which will interpret CMakeLists.txt and run try_compile tests)
echo "Running try_compile integration tests..."
$KILN || {
    echo "ERROR: kiln failed"
    exit 1
}

# Verify cache file was created
if [ ! -f build/debug/.kiln_subsystem_cache.json ]; then
    echo "ERROR: Cache file not created"
    exit 1
fi

# Verify cache contains try_compile entries
if ! grep -q "try_compile_cache" build/debug/.kiln_subsystem_cache.json; then
    echo "ERROR: No try_compile entries in cache"
    exit 1
fi

# Run again - should be faster (cache hits)
echo "Testing cache hits (second run)..."
START=$(date +%s%N)
$KILN || {
    echo "ERROR: Second build failed"
    exit 1
}
END=$(date +%s%N)
ELAPSED=$(( (END - START) / 1000000 )) # Convert to milliseconds

echo "Second build took ${ELAPSED}ms"

# Clean up
rm -rf build

echo "PASS: All try_compile integration tests succeeded!"
