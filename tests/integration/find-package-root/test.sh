#!/bin/bash
set -e

# Run dmake
"$1" .

echo "find_package root path test passed!"
