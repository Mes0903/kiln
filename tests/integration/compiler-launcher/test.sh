#!/bin/bash
set -e

chmod +x launcher_shim.sh launcher_shim_target.sh

export KILN_LAUNCHER_LOG="$PWD/launcher.log"
export KILN_LAUNCHER_LOG_TARGET="$PWD/launcher_target.log"
rm -f "$KILN_LAUNCHER_LOG" "$KILN_LAUNCHER_LOG_TARGET"

"$1"

if [ ! -x build/debug/hello ]; then
    echo "executable 'hello' not built"
    exit 1
fi
if [ ! -x build/debug/hello_target_prop ]; then
    echo "executable 'hello_target_prop' not built"
    exit 1
fi

# Cache-var launcher should have been invoked at least once for hello's
# main.cpp compile.
if [ ! -s "$KILN_LAUNCHER_LOG" ]; then
    echo "cache-var launcher shim was not invoked (no side-effect file)"
    exit 1
fi
if ! grep -q "main.cpp" "$KILN_LAUNCHER_LOG"; then
    echo "cache-var launcher log does not contain main.cpp argv"
    cat "$KILN_LAUNCHER_LOG"
    exit 1
fi

# Target-property override should have run for hello_target_prop only.
if [ ! -s "$KILN_LAUNCHER_LOG_TARGET" ]; then
    echo "target-property launcher shim was not invoked"
    exit 1
fi
if ! grep -q "main.cpp" "$KILN_LAUNCHER_LOG_TARGET"; then
    echo "target-property launcher log does not contain main.cpp argv"
    cat "$KILN_LAUNCHER_LOG_TARGET"
    exit 1
fi

# Sanity-check: produced executable still works.
./build/debug/hello | grep -q "hello from launcher test"
./build/debug/hello_target_prop | grep -q "hello from launcher test"
