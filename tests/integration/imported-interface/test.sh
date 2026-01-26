#!/bin/bash
set -e
"$1" .
if [ ! -f build/debug/app ]; then
    echo "Executable 'app' not found"
    exit 1
fi
./build/debug/app | grep -q "Success!"
