#!/bin/sh -e

cp $(ls /usr/share/automake-*/install-sh | tail -n1) ./

autoconf 

mkdir -p BUILD
cd BUILD
../configure

echo "To make: cd BUILD; make; make install"
echo
