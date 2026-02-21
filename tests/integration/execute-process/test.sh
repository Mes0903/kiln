#!/bin/bash
set -e
KILN=$1

"$KILN" | grep "ExecuteProcess tests passed in CMakeLists.txt"
