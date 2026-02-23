#!/bin/bash
set -e

KILN=$1

echo "Testing genex recovery and ANGLE-R patterns..."
$KILN --config debug

check() {
    local file="$1"
    local expected="$2"
    local desc="$3"
    local actual
    actual=$(cat "$file")
    if [ "$actual" != "$expected" ]; then
        echo "FAIL: $desc"
        echo "  expected: $expected"
        echo "  actual:   $actual"
        exit 1
    fi
    echo "  PASS: $desc"
}

check build/debug/test1.txt "a;b,c>d" "SEMICOLON/COMMA/ANGLE-R constants"
check build/debug/test2.txt "a>b"     "ANGLE-R inside conditional"
check build/debug/test3.txt "a,b,c"   "JOIN with COMMA separator"
check build/debug/test4.txt "included" "literal boolean conditionals"

echo "All genex_recovery tests passed!"
