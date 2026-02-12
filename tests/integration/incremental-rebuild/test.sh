#!/bin/bash
set -e
"$1"
BINARY="build/debug/app"
if [ ! -f "$BINARY" ]; then
    echo "Binary not found after first build"
    exit 1
fi
TS1=$(stat -c %Y "$BINARY")

# Second run (should be incremental)
"$1"
TS2=$(stat -c %Y "$BINARY")

if [ "$TS1" != "$TS2" ]; then
    echo "Error: Binary was recreated on incremental build (TS1=$TS1, TS2=$TS2)"
    exit 1
fi

# Modify source
sleep 1
touch main.cpp
"$1"
TS3=$(stat -c %Y "$BINARY")

if [ "$TS1" == "$TS3" ]; then
    echo "Error: Binary was NOT recreated after source modification"
    exit 1
fi
