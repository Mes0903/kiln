#!/bin/bash
set -e

if [ -z "$1" ]; then
    echo "Usage: $0 <path-to-kiln-binary>"
    exit 1
fi

KILN_BIN=$(realpath "$1")
TEST_ROOT=$(realpath $(dirname "$0"))
TEMP_DIR=$(mktemp -d -t kiln-tests-XXXXXXXXXX)

# Optional compiler selection — passed positionally:
#   run_tests.sh <kiln-binary> [<cxx>] [<cc>]
# When supplied, we (a) export CXX/CC for per-test gates that conditionally
# skip on compiler family, and (b) wrap the kiln binary so every invocation
# gets -DCMAKE_CXX_COMPILER / -DCMAKE_C_COMPILER without each test.sh having
# to forward extra args.
USER_CXX="${2:-}"
USER_CC="${3:-}"
if [ -n "$USER_CXX" ] || [ -n "$USER_CC" ]; then
    [ -n "$USER_CXX" ] && export CXX="$USER_CXX"
    [ -n "$USER_CC" ]  && export CC="$USER_CC"
    WRAPPER="$TEMP_DIR/kiln-wrapper.sh"
    {
        echo '#!/bin/bash'
        echo -n "exec \"$KILN_BIN\""
        [ -n "$USER_CXX" ] && echo -n " -DCMAKE_CXX_COMPILER=\"$USER_CXX\""
        [ -n "$USER_CC" ]  && echo -n " -DCMAKE_C_COMPILER=\"$USER_CC\""
        echo ' "$@"'
    } > "$WRAPPER"
    chmod +x "$WRAPPER"
    KILN_BIN="$WRAPPER"
fi

echo "Using kiln: $KILN_BIN"
[ -n "$USER_CXX$USER_CC" ] && echo "Compiler override: CC=${CC:-} CXX=${CXX:-}"
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
    if ./test.sh "$KILN_BIN" ; then
        echo "PASSED"
        PASSED=$((PASSED + 1))
    else
        echo "FAILED"
        if [ -f test.log ]; then
            sed 's/^/  /' test.log
        fi
    fi
    cd "$TEST_ROOT"
done

echo ""
echo "Result: $PASSED / $TOTAL tests passed."

if [ "$PASSED" -ne "$TOTAL" ]; then
    exit 1
fi
