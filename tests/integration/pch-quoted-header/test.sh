#!/bin/bash
set -e

"$1"

if [ ! -f "build/debug/app" ]; then
    echo "app binary not found"
    exit 1
fi

# .gch should exist — confirms the quoted header was actually picked up
if [ ! -f "build/debug/objs/app_pch.hxx.gch" ]; then
    echo "PCH .gch not found"
    exit 1
fi

# Wrapper should contain a properly-formed #include with no doubled quotes
if grep -q '""' build/debug/objs/app_pch.hxx; then
    echo "PCH wrapper has doubled quotes — header name not stripped"
    cat build/debug/objs/app_pch.hxx
    exit 1
fi

./build/debug/app | grep -q "ok"

echo "PCH quoted-header test passed!"
