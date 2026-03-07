#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
killall ebar
sleep 1
"$DIR/build/ebar" &
