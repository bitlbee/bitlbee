#!/bin/bash

killall bitlbee >/dev/null 2>/dev/null

set -e
(BITLBEE_DEBUG=1 ./bitlbee -Dnv 2> ./debuglog) &
