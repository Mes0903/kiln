#!/bin/bash
set -e
KILN=$1
cd "$(dirname "$0")"

# Trailing-args form: tokens after the script become CMAKE_ARGV3..
"$KILN" -P script.cmake alpha "beta gamma" -DLOOKS_LIKE_OPTION=1

# `--` separator form: should behave identically (the `--` is consumed).
"$KILN" -P script.cmake -- alpha "beta gamma" -DLOOKS_LIKE_OPTION=1
