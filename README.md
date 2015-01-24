# BitlBee

![](http://bitlbee.org/style/logo.png)

[![Build Status](https://travis-ci.org/bitlbee/bitlbee.svg)](https://travis-ci.org/bitlbee/bitlbee)
[![Coverity Scan Build Status](https://scan.coverity.com/projects/4028/badge.svg)](https://scan.coverity.com/projects/4028)

An IRC to other chat networks gateway

Main website: http://www.bitlbee.org/

Bug tracker: http://bugs.bitlbee.org/

Wiki: http://wiki.bitlbee.org/

License: GPLv2

## Development

Use github pull requests against the 'develop' branch to submit patches.

The 'master' branch should be stable enough to be usable by users of the APT repo, but only requires a few days of testing in the 'develop' branch.

Building:

```
./configure --debug=1
# or, for a local install:
# ./configure --debug=1 --prefix=$HOME/bitlbee --config=$HOME/bitlbee --pidfile=$HOME/bitlbee/bitlbee.pid

# Also try --asan=1 for AddressSanitizer

make

BITLBEE_DEBUG=1 ./bitlbee -Dnv
```

See ./doc/README and ./doc/HACKING for more details.

Mappings of bzr revisions to git commits (for historical purposes) are available in ./doc/git-bzr-rev-map

## Help?

Join **#BitlBee** on OFTC (**irc.oftc.net**) (OFTC, *not* FreeNode!) and flame us right in the face. :-)
