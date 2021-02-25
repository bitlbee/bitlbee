#!/bin/bash

set -e

bitlbee -F
prosodyctl register test1 localhost asd
prosodyctl register test2 localhost asd
prosodyctl reload
