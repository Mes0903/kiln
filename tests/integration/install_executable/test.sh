#!/bin/bash
set -e

KILN=$1
TESTDIR=$(dirname "$0")
INSTALL_PREFIX=$(mktemp -d)

echo "Testing install(TARGETS ... RUNTIME) with install prefix: $INSTALL_PREFIX"

# Build and install
cd "$TESTDIR"
$KILN install --prefix "$INSTALL_PREFIX"

# Verify executable was installed
if [ ! -x "$INSTALL_PREFIX/bin/myapp" ]; then
    echo "ERROR: Executable not found at $INSTALL_PREFIX/bin/myapp"
    rm -rf "$INSTALL_PREFIX"
    exit 1
fi

# Verify it's executable (755 permissions)
PERMS=$(stat -c "%a" "$INSTALL_PREFIX/bin/myapp")
if [ "$PERMS" != "755" ]; then
    echo "ERROR: Expected permissions 755, got $PERMS"
    rm -rf "$INSTALL_PREFIX"
    exit 1
fi

# Test that it runs
if ! "$INSTALL_PREFIX/bin/myapp" | grep -q "Hello from install test!"; then
    echo "ERROR: Executable didn't run correctly"
    rm -rf "$INSTALL_PREFIX"
    exit 1
fi

echo "Test passed!"
rm -rf "$INSTALL_PREFIX"
exit 0
