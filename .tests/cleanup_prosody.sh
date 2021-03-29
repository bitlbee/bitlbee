#!/bin/bash

set -e

sudo prosodyctl deluser test1@localhost
sudo prosodyctl deluser test2@localhost
#sudo prosodyctl stop
