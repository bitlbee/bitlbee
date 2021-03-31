#!/bin/bash

sudo prosodyctl deluser test1@localhost >/dev/null 2>/dev/null
sudo prosodyctl deluser test2@localhost >/dev/null 2>/dev/null
sudo prosodyctl stop >/dev/null 2>/dev/null

set -e
sudo /etc/init.d/prosody start
sudo prosodyctl register test1 localhost asd
sudo prosodyctl register test2 localhost asd
