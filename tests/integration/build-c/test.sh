#!/bin/bash

"$1" .

if [[ ! -f build/debug/main ]]
then
    echo "Build failed"
    exit 1
fi

# Run the executable and check the output
./build/debug/main | grep -q "42"
