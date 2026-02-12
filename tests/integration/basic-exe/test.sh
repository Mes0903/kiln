#!/bin/bash
set -e
"$1"
if [ ! -f build/debug/hello ]; then
    echo "Executable 'hello' not found"
    exit 1
fi
./build/debug/hello | grep -q "Hello, dmake!"
