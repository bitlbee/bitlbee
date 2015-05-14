#!/usr/bin/python

import operator
import re

import requests

import implugin

class NewsBlurPlugin(implugin.BitlBeeIMPlugin):
	NAME = "newsblur"
	ACCOUNT_FLAGS = 0
	SETTINGS = {
		"backlog": {
			"default": 20,
			"type": "int",
		},
	}
	
	BASE_URL = "https://newsblur.com"
	
	def url(self, path):
		return (self.BASE_URL + path)

	def login(self, account):
		super(NewsBlurPlugin, self).login(account)
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
		self.bee.log("Ok bye!")

	def buddy_msg(self, handle, msg, flags):
		feed = self.feeds[handle]
		cmd = re.split(r"\s+", msg)
	
	def set_set_backlog(self, setting, value):
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

		# Throw all unread-post hashes in a flat list and sort it by posting time.
		feed_hashes = r.json()["unread_feed_story_hashes"]
		all_hashes = []
		for feed, hashes in feed_hashes.iteritems():
			all_hashes += [tuple(h) for h in hashes]
		all_hashes.sort(key=operator.itemgetter(1))
		
		# Look at the most recent posts, grab the ones we haven't shown yet.
		req_hashes = []
		for hash, _ in all_hashes[-self.setting("backlog"):]:
			if hash not in self.seen_hashes:
				req_hashes.append(hash)
		
		if not req_hashes:
			return
		
		# Grab post details.
		r = self.ua.post(self.url("/reader/river_stories"), {"h": req_hashes})
		if r.status_code != 200:
			self.bee.error("HTTP error %d" % r.status_code)
			return
		
		# Response is not in the order we requested. :-( Make it a hash
		# and reconstruct order from our request.
		stories = {s["story_hash"]: s for s in r.json()["stories"]}
		for s in (stories[hash] for hash in req_hashes):
			line = "%(story_title)s <%(story_permalink)s>" % s
			ts = int(s.get("story_timestamp", "0"))
			self.bee.buddy_msg("rss", line, 0, ts)
			self.seen_hashes.add(s["story_hash"])


implugin.RunPlugin(NewsBlurPlugin, debug=True)
