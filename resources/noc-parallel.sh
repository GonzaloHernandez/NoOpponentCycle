#!/bin/bash

if [ $# -eq 0 ]; then
    ./noc --help
    exit 1
fi

user_args=("$@")

./build/noc "${user_args[@]}" --noc-even & PID1=$!

./build/noc "${user_args[@]}" --noc-odd & PID2=$!

wait -n

kill $PID1 $PID2 2>/dev/null
