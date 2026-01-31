#!/bin/bash
set -e

DMAKE="$1"
cd "$(dirname "$0")"
rm -rf build

# Run dmake
"$DMAKE" 2>&1

# Check compile_commands.json exists
if [ ! -f build/debug/compile_commands.json ]; then
    echo "ERROR: compile_commands.json not found"
    exit 1
fi

# Verify -isystem is used for sysinclude directory
if ! grep -q '\-isystem[^ ]*sysinclude' build/debug/compile_commands.json; then
    echo "ERROR: -isystem not found for system include directory"
    echo "Contents of compile_commands.json:"
    cat build/debug/compile_commands.json
    exit 1
fi
echo "Found -isystem flag for sysinclude directory"

# Verify -I is used for normal include directory (not -isystem)
if ! grep -q '\-I[^ ]*include' build/debug/compile_commands.json; then
    echo "ERROR: -I not found for normal include directory"
    echo "Contents of compile_commands.json:"
    cat build/debug/compile_commands.json
    exit 1
fi
echo "Found -I flag for normal include directory"

# Verify BEFORE flag works - second should appear before first in compile_commands.json
# For before_test.cpp, we added first, then BEFORE second, so order should be: -Isecond -Ifirst
before_cmd=$(grep 'before_test.cpp' build/debug/compile_commands.json)

# Check that -Isecond appears before -Ifirst
if echo "$before_cmd" | grep -q '\-I[^ ]*second.*\-I[^ ]*first'; then
    echo "BEFORE flag working correctly: second appears before first"
else
    echo "ERROR: BEFORE flag not working - expected second before first"
    echo "Command: $before_cmd"
    exit 1
fi

# Run the built executable to verify it works
if [ -f build/debug/app ]; then
    ./build/debug/app
    if [ $? -eq 0 ]; then
        echo "App executed successfully"
    else
        echo "ERROR: App returned non-zero exit code"
        exit 1
    fi
else
    echo "ERROR: Executable 'app' not built"
    exit 1
fi

echo "PASS"
