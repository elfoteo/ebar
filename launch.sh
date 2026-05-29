#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# If arguments are provided, assume it's a CLI command (brightness, togglefloat, etc.)
# In this case, we don't want to kill the bar or restart it.
if [ $# -gt 0 ]; then
    "$DIR/build/ebar" "$@"
    exit 0
fi

# Check if make needs to run
if make -C "$DIR" -q 2>/dev/null; then
    : # Up to date
else
    notify-send "Building ebar..."
    if ! make -C "$DIR"; then
        notify-send "ebar compilation failed"
        exit 1
    fi
fi

killall ebar 2>/dev/null
"$DIR/build/ebar" &
