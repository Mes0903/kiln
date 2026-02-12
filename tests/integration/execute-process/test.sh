#!/bin/bash
set -e
DMAKE=$1

"$DMAKE" | grep "ExecuteProcess tests passed in CMakeLists.txt"
