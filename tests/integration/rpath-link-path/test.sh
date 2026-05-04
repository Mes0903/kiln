#!/bin/bash
set -e

KILN="$1"

# Build a "vendor" shared library at a non-standard path. The directory
# containing it is the one kiln must embed in mylib's RUNPATH.
mkdir -p vendor
g++ -shared -fPIC -o vendor/libvendor.so vendor.cpp
VENDOR_DIR=$(realpath vendor)

"$KILN"

LIB=build/debug/libmylib.so
LIB_NORPATH=build/debug/libmylib_norpath.so
APP=build/debug/app

if [ ! -f "$LIB" ] || [ ! -f "$LIB_NORPATH" ] || [ ! -f "$APP" ]; then
    echo "Expected outputs not produced"
    exit 1
fi

# mylib must link successfully *and* have the vendor dir in its RUNPATH.
RPATH=$(readelf -d "$LIB" | awk '/R(UN)?PATH/ {print $5}' | tr -d '[]')
echo "mylib RUNPATH: $RPATH"
case ":$RPATH:" in
    *":$VENDOR_DIR:"*) ;;
    *) echo "FAIL: $VENDOR_DIR not in mylib RUNPATH"; exit 1 ;;
esac

# SKIP_BUILD_RPATH target must NOT contain the vendor dir.
RPATH_NO=$(readelf -d "$LIB_NORPATH" | awk '/R(UN)?PATH/ {print $5}' | tr -d '[]')
echo "mylib_norpath RUNPATH: $RPATH_NO"
case ":$RPATH_NO:" in
    *":$VENDOR_DIR:"*) echo "FAIL: SKIP_BUILD_RPATH ignored"; exit 1 ;;
esac

# End-to-end: app must run, proving transitive resolution worked at link
# time and the runtime loader can find libvendor.so via mylib's RUNPATH.
OUT=$("$APP")
if [ "$OUT" != "43" ]; then
    echo "FAIL: app printed '$OUT', expected '43'"
    exit 1
fi

echo "PASS: rpath-link-path"
