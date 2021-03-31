#!/bin/bash

set -e

killall bitlbee
rm -r /var/lib/bitlbee/*
sudo make uninstall-etc

printf 'Bitlbee output:\n\n'
less ./debuglog

if cat ./debuglog | grep -i -q error
then
    rm ./debuglog
    exit 1
else
    rm ./debuglog
fi
