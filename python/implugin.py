#!/usr/bin/python

import sys
import bjsonrpc
from bjsonrpc.handlers import BaseHandler

import random
import re
import socket
import time

# List of functions an IM plugin can export. This library will indicate to
# BitlBee which functions are actually implemented so omitted features
# will be disabled, but note that some/many functions are simply mandatory.
SUPPORTED_FUNCTIONS = [
	'login', 'keepalive', 'logout', 'buddy_msg', 'set_away',
	'send_typing', 'add_buddy', 'remove_buddy', 'add_permit',
	'add_deny', 'rem_permit', 'rem_deny', 'get_info', 'chat_invite',
	'chat_kick', 'chat_leave', 'chat_msg', 'chat_with', 'chat_join',
	'chat_topic'
]

class RpcForwarder(object):
	"""Tiny object that forwards RPCs from local Python code to BitlBee
	with a marginally nicer syntax. This layer could eventually be
	used to add basic parameter checking though I don't think that should
	be done here."""
	
	def __init__(self, methods, target):
		for m in methods:
			# imc(b)_ prefix is not useful here, chop it.
			# (Maybe do this in BitlBee already as well.)
			newname = re.sub("^imcb?_", "", m)
			self.__setattr__(newname, target.__getattr__(m))

class BitlBeeIMPlugin(BaseHandler):
	# Protocol name to be used in the BitlBee CLI, etc.
	NAME = "rpc-test"

	# See account.h (TODO: Add constants.)
	ACCOUNT_FLAGS = 3
	
	# Supported away states. If your protocol supports a specific set of
	# away states, put them in a list in this variable.
	AWAY_STATES = ["Away", "Busy"] #None
	
	# Filled in during initialisation:
	# Version code in hex. So if you need to do comparisions, for example
	# check "self.bitlbee_version >= 0x030202" for versions 3.2.2+
	bitlbee_version = None
	# Full version string
	bitlbee_version_str = None
	# Will become an RpcForwarder object to call into BitlBee
	bee = None
	
	@classmethod
	def _factory(cls, *args, **kwargs):
		def handler_factory(connection):
			handler = cls(connection, *args, **kwargs)
			return handler
		return handler_factory
	
	#def __init__(self, connection, *args, **kwargs):
	#	BaseHandler.__init__(self,connection)

	def init(self, bee):
		self.bee = RpcForwarder(bee["method_list"], self._conn.call)
		self.bitlbee_version = bee["version"]
		self.bitlbee_version_str = bee["version_str"]
		# TODO: See how to call into the module here.
		return {
			"name": self.NAME,
			"method_list": list(set(dir(self)) & set(SUPPORTED_FUNCTIONS)),
			"account_flags": self.ACCOUNT_FLAGS,
			"away_state_list": self.AWAY_STATES,
			"settings": {
				"oauth": {
					"default": "off",
					"type": "bool",
				},
				"test": {
					"default": "123",
					"type": "int",
				},
				"stringetje": {
					"default": "testje",
					"flags": 0x04,
				}
			},
		}
	
	def login(self, account):
		print "Logging in with username %s and password %s" % (account['user'], account['pass'])
		self.bee.log("Blaataap %r" % account)
		self.bee.error("HALP!")
		self.bee.connected()
		return [{1:2,3:4}, {"a":"A", "b":"B"}, 1, 2, True]

	def logout(self):
		self.bee.error("Ok bye!")

	def add_buddy(self, handle, group):
		print "%s is my new best friend in %s \o/" % (handle, group)
		self.bee.add_buddy(handle, group)
		self.bee.buddy_status(handle, 5, "Friend", "Best friend!")
		print self.bee.bee_user_by_handle(handle)
		print self.bee.set_setstr("test", handle)
		print self.bee.set_reset("test")
	
	def set_away(self, state, message):
		print "You're a slacker: %s (%r)" % (state, message)
	
	def set_set(self, setting, value):
		print "Setting %s changed to %r" % (setting, value)

def RunPlugin(plugin, debug=True):
	sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
	sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
	sock.bind("/tmp/rpcplugins/test2")
	sock.listen(3)
	
	srv = bjsonrpc.server.Server(sock, plugin._factory())
	
	srv.debug_socket(debug)
	srv.serve()

RunPlugin(BitlBeeIMPlugin)
