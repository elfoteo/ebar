#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

if ! make -C "$DIR"; then
    notify-send "ebar compilation failed"
    exit 1
fi

killall ebar
"$DIR/build/ebar" &
