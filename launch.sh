#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
killall ebar
make -C "$DIR"
"$DIR/build/ebar" &
