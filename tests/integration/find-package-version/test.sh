#!/bin/bash
set -e

# Run dmake
# We expect the build to fail if our assertions in CMakeLists.txt trigger (SEND_ERROR or FATAL_ERROR)
# Currently, without the fix, Test 2 will likely trigger SEND_ERROR because it finds the package.

if "$1" .; then
    echo "Build succeeded (unexpectedly if version check is missing and we asserted failure)"
else
    echo "Build failed as expected (or due to other errors)"
fi

# We actually want the script to verify the *behavior*.
# If we implement the fix, the cmake run should succeed (Test 2 checks !Found and prints STATUS).
# Before the fix, Test 2 finds it and prints SEND_ERROR.

# So, let's capture output.
OUTPUT=$("$1" . 2>&1)
echo "$OUTPUT"

if echo "$OUTPUT" | grep -q "Incorrectly found MockPkg"; then
    echo "FAILURE: Version checking is not working (found incompatible package)."
    exit 1
fi

if echo "$OUTPUT" | grep -q "Correctly did not find MockPkg"; then
    echo "SUCCESS: Version checking logic is working."
    exit 0
fi

echo "UNKNOWN STATE"
exit 1
