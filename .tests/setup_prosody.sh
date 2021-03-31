#!/bin/bash

sudo prosodyctl deluser test1@localhost
sudo prosodyctl deluser test2@localhost
sudo prosodyctl stop

set -e
sudo /etc/init.d/prosody start
sudo prosodyctl register test1 localhost asd
sudo prosodyctl register test2 localhost asd
