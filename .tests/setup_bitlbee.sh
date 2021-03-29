#!/bin/bash

set -e

(BITLBEE_DEBUG=1 ./bitlbee -Dnv 2> ./debuglog) &
