#!/bin/bash
set -e

KILN=$1
TEST_DIR=$(dirname "$0")

cd "$TEST_DIR"

# Clean any previous build
rm -rf build
mkdir -p build

# Run kiln (creates build/debug/)
$KILN

# Navigate to the actual build directory
cd build/debug

echo "Checking config.h..."
if [ ! -f config.h ]; then
    echo "ERROR: config.h was not generated"
    exit 1
fi

# Verify variable substitutions
grep -q '#define PROJECT_NAME "TestProject"' config.h || { echo "ERROR: @PROJECT_NAME@ not substituted"; exit 1; }
grep -q '#define VERSION "1.2.3"' config.h || { echo "ERROR: @VERSION@ not substituted"; exit 1; }
grep -q '#define FULL_NAME "TestProject v1.2.3"' config.h || { echo "ERROR: \${VAR} not substituted"; exit 1; }

# Verify #cmakedefine directives
grep -q '#define FEATURE_ENABLED' config.h || { echo "ERROR: #cmakedefine FEATURE_ENABLED failed"; exit 1; }
grep -q '/\* #undef FEATURE_DISABLED \*/' config.h || { echo "ERROR: #cmakedefine FEATURE_DISABLED failed"; exit 1; }
grep -q '#define BOOL_FEATURE 1' config.h || { echo "ERROR: #cmakedefine01 failed"; exit 1; }
grep -q '#define VALUE_FEATURE CustomValue' config.h || { echo "ERROR: #cmakedefine with value failed"; exit 1; }

echo "Checking script.sh (@ONLY mode)..."
if [ ! -f script.sh ]; then
    echo "ERROR: script.sh was not generated"
    exit 1
fi

# Verify @ONLY - @ variables should be expanded, ${} should NOT
grep -q 'PROJECT="TestProject"' script.sh || { echo "ERROR: @PROJECT_NAME@ not substituted in @ONLY mode"; exit 1; }
grep -q 'BASH_VAR="\${HOME}/path"' script.sh || { echo "ERROR: \${HOME} was expanded in @ONLY mode (should not be)"; exit 1; }
grep -q 'ANOTHER="\${USER}"' script.sh || { echo "ERROR: \${USER} was expanded in @ONLY mode (should not be)"; exit 1; }

echo "Checking permissions..."
if [ ! -f config_no_perms.h ]; then
    echo "ERROR: config_no_perms.h was not generated"
    exit 1
fi

if [ ! -f config_perms.h ]; then
    echo "ERROR: config_perms.h was not generated"
    exit 1
fi

# Check that NO_SOURCE_PERMISSIONS gives 644 (owner read/write, group/other read)
PERMS_NO_SOURCE=$(stat -c "%a" config_no_perms.h 2>/dev/null || stat -f "%OLp" config_no_perms.h)
if [ "$PERMS_NO_SOURCE" != "644" ]; then
    echo "ERROR: NO_SOURCE_PERMISSIONS should give 644, got $PERMS_NO_SOURCE"
    exit 1
fi

# Check that USE_SOURCE_PERMISSIONS copies the source file permissions
SOURCE_PERMS=$(stat -c "%a" ../../config.h.in 2>/dev/null || stat -f "%OLp" ../../config.h.in)
PERMS_WITH_SOURCE=$(stat -c "%a" config_perms.h 2>/dev/null || stat -f "%OLp" config_perms.h)
if [ "$PERMS_WITH_SOURCE" != "$SOURCE_PERMS" ]; then
    echo "ERROR: USE_SOURCE_PERMISSIONS should copy source permissions ($SOURCE_PERMS), got $PERMS_WITH_SOURCE"
    exit 1
fi

# Check that default behavior (config.h) also uses source permissions
PERMS_DEFAULT=$(stat -c "%a" config.h 2>/dev/null || stat -f "%OLp" config.h)
if [ "$PERMS_DEFAULT" != "$SOURCE_PERMS" ]; then
    echo "ERROR: Default behavior should copy source permissions ($SOURCE_PERMS), got $PERMS_DEFAULT"
    exit 1
fi

echo "  - NO_SOURCE_PERMISSIONS: $PERMS_NO_SOURCE (expected 644)"
echo "  - USE_SOURCE_PERMISSIONS: $PERMS_WITH_SOURCE (matches source: $SOURCE_PERMS)"
echo "  - Default (USE_SOURCE_PERMISSIONS): $PERMS_DEFAULT (matches source: $SOURCE_PERMS)"

echo "All configure_file tests passed!"
exit 0
