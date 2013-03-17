# bitlbee-python

Bitlbee-python will allow bitlbee plugins to be written as Python scripts.

It will scan the plugins directory for .py files and register them with bitlbee as protocols.

## Installation

The provided `bootstrap.sh` provides the bare minimum setup to get going.

```
$ ./bootstrap.sh
[configure output]

To make: cd BUILD; make; make install
```

If you have installed bitlbee in a non-standard location, provide a different PKG_CONFIG_PATH environment variable to bootstrap.sh (or ./configure), eg:

```
PKG_CONFIG_PATH=/opt/bitlbee-bzr/lib/pkgconfig/ ./configure
```

## Usage

To do! Currently this project is a work in progress.
