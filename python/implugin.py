#!/usr/bin/python

import sys
import bjsonrpc
from bjsonrpc.handlers import BaseHandler

import operator
import random
import re
import socket
import time

import requests

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
	
	# Filled in during initialisation:
	# Version number as a three-tuple, so 3.2 becomes (3, 2, 0).
	bitlbee_version = None
	# Full version string
	bitlbee_version_str = None
	# Will become an RpcForwarder object to call into BitlBee
	bee = None
	
	BASE_URL = "https://newsblur.com"
	
	@classmethod
	def _factory(cls, *args, **kwargs):
		def handler_factory(connection):
			handler = cls(connection, *args, **kwargs)
			return handler
		return handler_factory
	
	#def __init__(self, connection, *args, **kwargs):
	#	BaseHandler.__init__(self,connection)

	def url(self, path):
		return (self.BASE_URL + path)

	def init(self, bee):
		self.bee = RpcForwarder(bee["method_list"], self._conn.call)
		self.bitlbee_version = make_version_tuple(bee["version"])
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
		self.ua = requests.Session()
		creds = {"username": account["user"], "password": account["pass"]}
		r = self.ua.post(self.url("/api/login"), creds)
		self.bee.log("You're running BitlBee %d.%d.%d" % self.bitlbee_version)
		if r.status_code != 200:
			self.bee.error("HTTP error %d" % r.status_code)
			self.bee.logout(True)
		elif r.json()["errors"]:
			self.bee.error("Authentication error")
			self.bee.logout(False)
		else:
			self.bee.add_buddy("rss", None)
			self.bee.connected()
			self.seen_hashes = set()
			self.keepalive()

	def logout(self):
		self.bee.error("Ok bye!")

	def buddy_msg(self, handle, msg, flags):
		feed = self.feeds[handle]
		cmd = re.split(r"\s+", msg)
	
	def set_set(self, setting, value):
		print "Setting %s changed to %r" % (setting, value)
	
	# BitlBee will call us here every minute which is actually a neat way
	# to get periodic work (like RSS polling) scheduled. :-D
	def keepalive(self):
		r = self.ua.post(
			self.url("/reader/unread_story_hashes"),
			{"include_timestamps": True})
		if r.status_code != 200:
			self.bee.error("HTTP error %d" % r.status_code)
			return

		# Throw all unread-post hashes in a long list and sort it by posting time.
		#feed_hashes = r.json()["unread_feed_story_hashes"]
		wtf = r.json()
		feed_hashes = wtf["unread_feed_story_hashes"]
		all_hashes = []
		for feed, hashes in feed_hashes.iteritems():
			all_hashes += [tuple(h) for h in hashes]
		all_hashes.sort(key=operator.itemgetter(1))
		
		# Look at the most recent 20, grab the ones we haven't shown yet.
		req_hashes = []
		for hash, _ in all_hashes[-20:]:
			if hash not in self.seen_hashes:
				req_hashes.append(hash)
		
		if not req_hashes:
			return
		print req_hashes
		
		# Grab post details.
		r = self.ua.post(self.url("/reader/river_stories"), {"h": req_hashes})
		if r.status_code != 200:
			self.bee.error("HTTP error %d" % r.status_code)
			return
		
		# Response is not in the order we requested. :-(
		wtf = r.json()
		stories = {}
		for s in wtf["stories"]:
			stories[s["story_hash"]] = s
		
		for s in (stories[hash] for hash in req_hashes):
			line = "%(story_title)s <%(story_permalink)s>" % s
			ts = int(s.get("story_timestamp", "0"))
			self.bee.buddy_msg("rss", line, 0, ts)
			self.seen_hashes.add(s["story_hash"])
			print s["story_hash"]


def RunPlugin(plugin, debug=True):
	sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
	sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
	sock.bind("/tmp/rpcplugins/test2")
	sock.listen(3)
	
	srv = bjsonrpc.server.Server(sock, plugin._factory())
	
	srv.debug_socket(debug)
	srv.serve()

RunPlugin(BitlBeeIMPlugin)
