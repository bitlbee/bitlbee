# BitlBee

![](https://www.bitlbee.org/style/logo.png)

[![Build Status](https://travis-ci.org/bitlbee/bitlbee.svg)](https://travis-ci.org/bitlbee/bitlbee)
[![Coverity Scan Build Status](https://scan.coverity.com/projects/4028/badge.svg)](https://scan.coverity.com/projects/4028)

An IRC to other chat networks gateway

Main website: https://www.bitlbee.org/

Bug tracker: https://bugs.bitlbee.org/

Wiki: https://wiki.bitlbee.org/

License: GPLv2

## Installation

BitlBee is available in the package managers of most distros.

For debian/ubuntu/etc you may use the nightly APT repository: https://code.bitlbee.org/debian/

You can also use a public server (such as `im.bitlbee.org`) instead of installing it: https://www.bitlbee.org/main.php/servers.html

## Compiling

If you wish to compile it yourself, ensure you have the following packages and their headers:

* glib 2.32 or newer (not to be confused with glibc)
* gnutls
* python 2 or 3 (for the user guide)

Some optional features have additional dependencies, such as libpurple, libotr, libevent, etc.
NSS and OpenSSL are also available but not as well supported as GnuTLS.

Once you have the dependencies, building should be a matter of:

    ./configure
    make
    sudo make install

## Development tips

* To enable debug symbols: `./configure --debug=1`
* To get some additional debug output for some protocols: `BITLBEE_DEBUG=1 ./bitlbee -Dnv`
* Use github pull requests against the 'develop' branch to submit patches.
* The coding style based on K&R with tabs and 120 columns. See `./doc/uncrustify.cfg` for the parameters used to reformat the code.
* Mappings of bzr revisions to git commits (for historical purposes) are available in `./doc/git-bzr-rev-map`
* See also `./doc/README` and `./doc/HACKING`

## Help?

Join **#BitlBee** on OFTC (**irc.oftc.net**) (OFTC, *not* freenode!)
