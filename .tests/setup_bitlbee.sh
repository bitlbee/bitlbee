#!/bin/bash


killall bitlbee >/dev/null 2>/dev/null
rm -r /var/lib/bitlbee/* >/dev/null 2>/dev/null
sudo make uninstall-etc >/dev/null 2>/dev/null

set -e
sudo make install-etc
(BITLBEE_DEBUG=1 ./bitlbee -Dnv 2> ./debuglog) &
