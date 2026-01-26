#!/bin/bash
set -e

if [ -z "$1" ]; then
    echo "Usage: $0 <path-to-dmake-binary>"
    exit 1
fi

DMAKE_BIN=$(realpath "$1")
TEST_ROOT=$(realpath $(dirname "$0"))
TEMP_DIR=$(mktemp -d -t dmake-tests-XXXXXXXXXX)

echo "Using dmake: $DMAKE_BIN"
echo "Work directory: $TEMP_DIR"

cleanup() {
    rm -rf "$TEMP_DIR"
}
trap cleanup EXIT

TOTAL=0
PASSED=0

for d in $(find "$TEST_ROOT" -maxdepth 1 -type d); do
    if [ ! -f "$d/test.sh" ]; then
        continue
    fi
    test_name=$(basename "$d")
    TOTAL=$((TOTAL + 1))

    echo -n "Running $test_name... "

    # Setup work dir
    work_dir="$TEMP_DIR/$test_name"
    mkdir -p "$work_dir"
    cp -r "$d"/* "$work_dir/"
    chmod +x "$work_dir/test.sh"

    # Run test
    cd "$work_dir"
    if ./test.sh "$DMAKE_BIN" ; then
        echo "PASSED"
        PASSED=$((PASSED + 1))
    else
        echo "FAILED"
        sed 's/^/  /' test.log
    fi
    cd "$TEST_ROOT"
done

echo ""
echo "Result: $PASSED / $TOTAL tests passed."

if [ "$PASSED" -ne "$TOTAL" ]; then
    exit 1
fi
