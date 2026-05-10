#!/bin/bash
set -e
"$1"

LIB_DIR=build/debug

# Real file is the fully-versioned name
if [ ! -f "$LIB_DIR/libmylib.so.1.9.7" ] || [ -L "$LIB_DIR/libmylib.so.1.9.7" ]; then
    echo "Expected libmylib.so.1.9.7 to exist as a regular file"
    exit 1
fi

# SOVERSION symlink → real file
if [ ! -L "$LIB_DIR/libmylib.so.27" ]; then
    echo "Expected libmylib.so.27 to be a symlink"
    exit 1
fi
target=$(readlink "$LIB_DIR/libmylib.so.27")
if [ "$target" != "libmylib.so.1.9.7" ]; then
    echo "libmylib.so.27 should point to libmylib.so.1.9.7, got: $target"
    exit 1
fi

# Unversioned symlink → SOVERSION symlink
if [ ! -L "$LIB_DIR/libmylib.so" ]; then
    echo "Expected libmylib.so to be a symlink"
    exit 1
fi
target=$(readlink "$LIB_DIR/libmylib.so")
if [ "$target" != "libmylib.so.27" ]; then
    echo "libmylib.so should point to libmylib.so.27, got: $target"
    exit 1
fi

# DT_SONAME embedded in the real file matches SOVERSION
soname=$(readelf -d "$LIB_DIR/libmylib.so.1.9.7" | awk '/SONAME/ {gsub(/[\[\]]/,"",$NF); print $NF}')
if [ "$soname" != "libmylib.so.27" ]; then
    echo "DT_SONAME should be libmylib.so.27, got: $soname"
    exit 1
fi

# Executable's NEEDED entry refers to the SOVERSION (DT_SONAME), and resolves at runtime
needed=$(readelf -d "$LIB_DIR/app" | awk '/NEEDED/ && /libmylib/ {gsub(/[\[\]]/,"",$NF); print $NF}')
if [ "$needed" != "libmylib.so.27" ]; then
    echo "app NEEDED should be libmylib.so.27, got: $needed"
    exit 1
fi

LD_LIBRARY_PATH="$LIB_DIR" "$LIB_DIR/app" | grep -q "Success!"
