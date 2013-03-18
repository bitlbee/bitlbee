"""sampleplugin.py - sample plugin for bitlbee-python.

Place this file in bitlbee's plugin directory. bitlbee-python
will pick it up as long as it has a .py extension.
"""

import sys
import bpython
print "hello, I'm a test plugin running on Python", sys.version_info

# plugin name:
name = 'moose'

print bpython.register_protocol

bpython.register_protocol('echo "hello"')
