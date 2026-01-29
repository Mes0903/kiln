#!/bin/bash
set -e

DMAKE=$1
TESTDIR=$(dirname "$0")
INSTALL_PREFIX=$(mktemp -d)

echo "Testing install(FILES) and install(PROGRAMS)"

# Build and install
cd "$TESTDIR"
$DMAKE install --prefix "$INSTALL_PREFIX"

# Verify config file was installed
if [ ! -f "$INSTALL_PREFIX/etc/config.txt" ]; then
    echo "ERROR: Config file not found at $INSTALL_PREFIX/etc/config.txt"
    rm -rf "$INSTALL_PREFIX"
    exit 1
fi

# Verify config file permissions (644)
PERMS=$(stat -c "%a" "$INSTALL_PREFIX/etc/config.txt")
if [ "$PERMS" != "644" ]; then
    echo "ERROR: Expected permissions 644 for config.txt, got $PERMS"
    rm -rf "$INSTALL_PREFIX"
    exit 1
fi

# Verify script was installed
if [ ! -f "$INSTALL_PREFIX/bin/script.sh" ]; then
    echo "ERROR: Script not found at $INSTALL_PREFIX/bin/script.sh"
    rm -rf "$INSTALL_PREFIX"
    exit 1
fi

# Verify script permissions (755 for PROGRAMS)
PERMS=$(stat -c "%a" "$INSTALL_PREFIX/bin/script.sh")
if [ "$PERMS" != "755" ]; then
    echo "ERROR: Expected permissions 755 for script.sh, got $PERMS"
    rm -rf "$INSTALL_PREFIX"
    exit 1
fi

echo "Test passed!"
rm -rf "$INSTALL_PREFIX"
exit 0
