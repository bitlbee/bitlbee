#!/bin/bash

set -e

sudo /etc/init.d/prosody start
sudo prosodyctl register test1 localhost asd
sudo prosodyctl register test2 localhost asd
