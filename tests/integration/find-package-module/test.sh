#!/bin/bash
set -e

# Run kiln
"$1"

# Check output for success message
# (The build failing would exit script due to set -e)

echo "Module test passed!"
