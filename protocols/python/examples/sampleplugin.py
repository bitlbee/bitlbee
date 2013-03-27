"""sampleplugin.py - sample plugin for bitlbee-python.

Place this file in bitlbee's plugin directory. bitlbee-python
will pick it up as long as it has a .py extension.
"""

import sys
import bpython
print ("hello, I'm a test plugin running on Python {}.{}.{}".format(
    sys.version_info.major, sys.version_info.minor, sys.version_info.micro))

class MyProtocol(bpython.Protocol):
    pass

myproto = MyProtocol()

bpython.register_protocol('myproto', myproto)

