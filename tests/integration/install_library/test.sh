#!/bin/bash
set -e

DMAKE=$1
TESTDIR=$(dirname "$0")
INSTALL_PREFIX=$(mktemp -d)

echo "Testing install(TARGETS ... LIBRARY) with versioning"

# Build and install
cd "$TESTDIR"
$DMAKE install --prefix "$INSTALL_PREFIX"

# Verify main library file exists
if [ ! -f "$INSTALL_PREFIX/lib/libmylib.so.1.2.3" ]; then
    echo "ERROR: Versioned library not found at $INSTALL_PREFIX/lib/libmylib.so.1.2.3"
    ls -la "$INSTALL_PREFIX/lib/"
    rm -rf "$INSTALL_PREFIX"
    exit 1
fi

# Verify soversion symlink exists and points correctly
if [ ! -L "$INSTALL_PREFIX/lib/libmylib.so.1" ]; then
    echo "ERROR: SOVERSION symlink not found at $INSTALL_PREFIX/lib/libmylib.so.1"
    ls -la "$INSTALL_PREFIX/lib/"
    rm -rf "$INSTALL_PREFIX"
    exit 1
fi

SOVERSION_TARGET=$(readlink "$INSTALL_PREFIX/lib/libmylib.so.1")
if [ "$SOVERSION_TARGET" != "libmylib.so.1.2.3" ]; then
    echo "ERROR: SOVERSION symlink points to $SOVERSION_TARGET, expected libmylib.so.1.2.3"
    rm -rf "$INSTALL_PREFIX"
    exit 1
fi

# Verify unversioned symlink exists and points correctly
if [ ! -L "$INSTALL_PREFIX/lib/libmylib.so" ]; then
    echo "ERROR: Unversioned symlink not found at $INSTALL_PREFIX/lib/libmylib.so"
    ls -la "$INSTALL_PREFIX/lib/"
    rm -rf "$INSTALL_PREFIX"
    exit 1
fi

UNVERSIONED_TARGET=$(readlink "$INSTALL_PREFIX/lib/libmylib.so")
if [ "$UNVERSIONED_TARGET" != "libmylib.so.1" ]; then
    echo "ERROR: Unversioned symlink points to $UNVERSIONED_TARGET, expected libmylib.so.1"
    rm -rf "$INSTALL_PREFIX"
    exit 1
fi

# Verify permissions (755 for shared libraries)
PERMS=$(stat -c "%a" "$INSTALL_PREFIX/lib/libmylib.so.1.2.3")
if [ "$PERMS" != "755" ]; then
    echo "ERROR: Expected permissions 755, got $PERMS"
    rm -rf "$INSTALL_PREFIX"
    exit 1
fi

echo "Test passed!"
rm -rf "$INSTALL_PREFIX"
exit 0
