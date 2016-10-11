#!/bin/bash
apt update
apt install libssl-dev
./configure --ssl=openssl && make && make install && make install-etc && make install-dev

