#!/bin/bash

set -e

killall bitlbee
less ./debuglog

if cat ./debuglog | grep -i -q error
then
    rm ./debuglog
    exit 1
else
    rm ./debuglog
fi
