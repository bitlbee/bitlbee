#!/bin/bash

set -e

killall bitlbee || true
rm -r /var/lib/bitlbee/* || true
sudo make uninstall-etc

printf 'Bitlbee output:\n'
less ./debuglog

if cat ./debuglog | grep -i -q error
then
    rm ./debuglog
    exit 1
else
    rm ./debuglog
fi
