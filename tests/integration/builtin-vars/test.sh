#!/bin/bash
set -e
KILN=$1
cd "$(dirname "$0")"
"$KILN" -P script.cmake
