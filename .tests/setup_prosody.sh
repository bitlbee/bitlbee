#!/bin/bash

set -e

sudo prosodyctl start
sudo prosodyctl register test1 localhost asd
sudo prosodyctl register test2 localhost asd
