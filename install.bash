#!/bin/bash
apt update
apt install libssl-dev pkg-config libglib2.0-dev
./configure --ssl=openssl && make && make install && make install-etc && make install-dev

