#!/usr/bin/python

import bjsonrpc
from bjsonrpc.handlers import BaseHandler

import re
import socket

# List of functions an IM plugin can export. This library will indicate to
# BitlBee which functions are actually implemented so omitted features
# will be disabled, but note that some/many functions are simply mandatory.
# (Currently login/-out, buddy_msg.)
SUPPORTED_FUNCTIONS = [
	'login', 'keepalive', 'logout', 'buddy_msg', 'set_away',
	'send_typing', 'add_buddy', 'remove_buddy', 'add_permit',
	'add_deny', 'rem_permit', 'rem_deny', 'get_info', 'chat_invite',
	'chat_kick', 'chat_leave', 'chat_msg', 'chat_with', 'chat_join',
	'chat_topic'
]

def make_version_tuple(hex):
	"""Convert the BitlBee binary-encoded version number into something
	more "Pythonic". Could use distutils.version instead but its main
	benefit appears to be string parsing which here is not that useful."""

	return (hex >> 16, (hex >> 8) & 0xff, hex & 0xff)

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
	ACCOUNT_FLAGS = 0
	
	# Supported away states. If your protocol supports a specific set of
	# away states, put them in a list in this variable.
	AWAY_STATES = None

	SETTINGS = {
		"oauth": {
			"default": False,
			"type": "bool",
		},
		"test": {
			"default": 123,
			"type": "int",
		},
		"stringetje": {
			"default": "testje",
			"flags": 0x04,
		}
	}
	_settings_values = {}
	
	# Filled in during initialisation:
	# Version number as a three-tuple, so 3.2 becomes (3, 2, 0).
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
	
	def init(self, bee):
		self.bee = RpcForwarder(bee["method_list"], self._conn.call)
		self.bitlbee_version = make_version_tuple(bee["version"])
		self.bitlbee_version_str = bee["version_str"]

		return {
			"name": self.NAME,
			"method_list": list(set(dir(self)) & set(SUPPORTED_FUNCTIONS)),
			"account_flags": self.ACCOUNT_FLAGS,
			"away_state_list": self.AWAY_STATES,
			"settings": self.SETTINGS,
		}

	def login(self, account):
		for key, value in account.get("settings", {}).iteritems():
			self.set_set(key, value)

	def set_set(self, key, value):
		self._settings_values[key] = value
		try:
			func = self.__getattribute__("set_set_%s" % key)
		except AttributeError:
			return
		func(key, value)
	
	def setting(self, key):
		"""Throws KeyError if the setting does not exist!"""
		return self._settings_values[key]


def RunPlugin(plugin, debug=False):
	sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
	sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
	sock.bind("/tmp/rpcplugins/%s.sock" % plugin.NAME)
	sock.listen(3)
	
	srv = bjsonrpc.server.Server(sock, plugin._factory())
	
	srv.debug_socket(debug)
	srv.serve()
