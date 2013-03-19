"""sampleplugin.py - sample plugin for bitlbee-python.

Place this file in bitlbee's plugin directory. bitlbee-python
will pick it up as long as it has a .py extension.
"""

import sys
import bpython
print "hello, I'm a test plugin running on Python", sys.version_info

print bpython.Protocol
print bpython.Protocol()
print bpython.register_protocol

try:
    class MyProtocol(bpython.Protocol):
        pass
except Exception, e:
    print "error:", e

myproto = MyProtocol()
print "myproto: ", myproto

bpython.register_protocol('echo "hello"')
