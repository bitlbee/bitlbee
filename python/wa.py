#!/usr/bin/python

import collections
import logging
import threading
import time

import yowsup

from yowsup.layers.auth                        import YowAuthenticationProtocolLayer
from yowsup.layers.protocol_acks               import YowAckProtocolLayer
from yowsup.layers.protocol_chatstate          import YowChatstateProtocolLayer
from yowsup.layers.protocol_contacts           import YowContactsIqProtocolLayer
from yowsup.layers.protocol_groups             import YowGroupsProtocolLayer
from yowsup.layers.protocol_ib                 import YowIbProtocolLayer
from yowsup.layers.protocol_iq                 import YowIqProtocolLayer
from yowsup.layers.protocol_messages           import YowMessagesProtocolLayer
from yowsup.layers.protocol_notifications      import YowNotificationsProtocolLayer
from yowsup.layers.protocol_presence           import YowPresenceProtocolLayer
from yowsup.layers.protocol_privacy            import YowPrivacyProtocolLayer
from yowsup.layers.protocol_profiles           import YowProfilesProtocolLayer
from yowsup.layers.protocol_receipts           import YowReceiptProtocolLayer
from yowsup.layers.network                     import YowNetworkLayer
from yowsup.layers.coder                       import YowCoderLayer
from yowsup.stacks import YowStack, YowStackBuilder
from yowsup.common import YowConstants
from yowsup.layers import YowLayerEvent
from yowsup.stacks import YowStack, YOWSUP_CORE_LAYERS
from yowsup import env

from yowsup.layers.interface                             import YowInterfaceLayer, ProtocolEntityCallback
from yowsup.layers.protocol_acks.protocolentities        import *
from yowsup.layers.protocol_chatstate.protocolentities   import *
from yowsup.layers.protocol_contacts.protocolentities    import *
from yowsup.layers.protocol_groups.protocolentities      import *
from yowsup.layers.protocol_ib.protocolentities          import *
from yowsup.layers.protocol_iq.protocolentities          import *
from yowsup.layers.protocol_media.mediauploader import MediaUploader
from yowsup.layers.protocol_media.protocolentities       import *
from yowsup.layers.protocol_messages.protocolentities    import *
from yowsup.layers.protocol_notifications.protocolentities import *
from yowsup.layers.protocol_presence.protocolentities    import *
from yowsup.layers.protocol_privacy.protocolentities     import *
from yowsup.layers.protocol_profiles.protocolentities    import *
from yowsup.layers.protocol_receipts.protocolentities    import *
from yowsup.layers.axolotl.protocolentities.iq_key_get import GetKeysIqProtocolEntity

import implugin

logger = logging.getLogger("yowsup.layers.logger.layer")
logger.setLevel(logging.DEBUG)
ch = logging.StreamHandler()
ch.setLevel(logging.DEBUG)
logger.addHandler(ch)

"""
TODO/Things I'm unhappy about:

About the fact that WhatsApp is a rubbish protocol that happily rejects
every second stanza you send it if you're trying to implement a client that
doesn't keep local state. See how to cope with that.. It'd help if Yowsup
came with docs on what a normal login sequence looks like instead of just
throwing some stanzas over a wall but hey.

The randomness of where which bits/state live, in the implugin and the
yowsup layer. Can't really merge this but at least state should live in
one place.

Mix of silly CamelCase and proper_style. \o/

Most important: This is NOT thread-clean. implugin can call into yowsup
cleanly by throwing closures into a queue, but there's no mechanism in
the opposite direction, I'll need to cook up some hack to make this
possible through bjsonrpc's tiny event loop. I think I know how...

And more. But let's first get this into a state where it even works..
"""

class BitlBeeLayer(YowInterfaceLayer):

	def __init__(self, *a, **kwa):
		super(BitlBeeLayer, self).__init__(*a, **kwa)
		# Offline messages are sent while we're still logging in.
		self.msg_queue = []

	def receive(self, entity):
		print "Received: %r" % entity
		#print entity
		super(BitlBeeLayer, self).receive(entity)

	def Ship(self, entity):
		"""Send an entity into Yowsup, but through the correct thread."""
		print "Queueing: %s" % entity.getTag()
		#print entity
		def doit():
			self.toLower(entity)
		self.getStack().execDetached(doit)

	@ProtocolEntityCallback("success")
	def onSuccess(self, entity):
		self.b = self.getStack().getProp("org.bitlbee.Bijtje")
		self.cb = self.b.bee
		self.b.yow = self
		
		self.cb.log("Authenticated, syncing contact list")
		
		# We're done once this set is empty.
		self.todo = set(["contacts", "groups", "ping"])
		
		# Supposedly WA can also do national-style phone numbers without
		# a + prefix BTW (relative to I guess the user's country?). I
		# don't want to support this at least for now.
		numbers = [("+" + x.split("@")[0]) for x in self.cb.get_local_contacts()]
		self.toLower(GetSyncIqProtocolEntity(numbers))
		self.toLower(ListGroupsIqProtocolEntity())
		self.b.keepalive()
		
		try:
			self.toLower(PresenceProtocolEntity(name=self.b.setting("name")))
		except KeyError:
			pass

	def check_connected(self, done):
		if not self.todo:
			return
		self.todo.remove(done)
		if not self.todo:
			self.cb.connected()
			self.flush_msg_queue()
	
	def flush_msg_queue(self):
		for msg in self.msg_queue:
			self.onMessage(msg)
		self.msg_queue = None
	
	@ProtocolEntityCallback("failure")
	def onFailure(self, entity):
		self.b = self.getStack().getProp("org.bitlbee.Bijtje")
		self.cb = self.b.bee
		self.cb.error(entity.getReason())
		self.cb.logout(False)

	def onEvent(self, event):
		# TODO: Make this work without, hmm, over-recursing. (This handler
		# getting called when we initiated the disconnect, which upsets yowsup.)
		if event.getName() == "orgopenwhatsapp.yowsup.event.network.disconnected":
			self.cb.error(event.getArg("reason"))
			self.cb.logout(True)
			self.getStack().execDetached(self.daemon.StopDaemon)
		else:
			print "Received event: %s name %s" % (event, event.getName())
	
	@ProtocolEntityCallback("presence")
	def onPresence(self, pres):
		if pres.getFrom() == self.b.account["user"]:
			# WA returns our own presence. Meh.
			return
		
		# Online/offline is not really how WA works. Let's show everyone
		# as online but unavailable folks as away. This also solves the
		# problem of offline->IRC /quit causing the persons to leave chat
		# channels as well (and not reappearing there when they return).
		status = 8 | 1  # MOBILE | ONLINE
		if pres.getType() == "unavailable":
			status |= 4  # AWAY
		self.cb.buddy_status(pres.getFrom(), status, None, None)
		
		try:
			# Last online time becomes idle time which I guess is
			# sane enough?
			self.cb.buddy_times(pres.getFrom(), 0, int(pres.getLast()))
		except (ValueError, TypeError):
			# Could be "error" or, more likely, "deny", or None.
			pass
	
	@ProtocolEntityCallback("message")
	def onMessage(self, msg):
		if self.todo:
			# We're still logging in, so wait.
			self.msg_queue.append(msg)
			return

		self.b.show_message(msg)

		# ACK is required! So only use return above in case of errors.
		# (So that we will/might get a retry after restarting.)
		self.toLower(OutgoingReceiptProtocolEntity(msg.getId(), msg.getFrom()))

	@ProtocolEntityCallback("receipt")
	def onReceipt(self, entity):
		ack = OutgoingAckProtocolEntity(entity.getId(), entity.getTag(),
		                                entity.getType(), entity.getFrom())
		self.toLower(ack)

	@ProtocolEntityCallback("iq")
	def onIq(self, entity):
		if isinstance(entity, ResultSyncIqProtocolEntity):
			return self.onSyncResult(entity)
		elif isinstance(entity, ListGroupsResultIqProtocolEntity):
			return self.onListGroupsResult(entity)
		elif type(entity) == IqProtocolEntity:  # Pong has no type, sigh.
			self.b.last_pong = time.time()
			if self.todo:
				return self.onLoginPong()
	
	def onSyncResult(self, entity):
		# TODO HERE AND ELSEWHERE: Thread idiocy happens when going
		# from here to the IMPlugin. Check how bjsonrpc lets me solve that.
		for num, jid in entity.inNumbers.iteritems():
			self.toLower(SubscribePresenceProtocolEntity(jid))
			self.cb.add_buddy(jid, "")
		if entity.outNumbers:
			self.cb.error("Not on WhatsApp: %s" %
			              ", ".join(entity.outNumbers.keys()))
		if entity.invalidNumbers:
			self.cb.error("Invalid numbers: %s" %
			              ", ".join(entity.invalidNumbers))

		#self.getStatuses(entity.inNumbers.values())
		self.check_connected("contacts")

	def onSyncResultFail(self):
		# Whatsapp rate-limits sync stanzas, so in case of failure
		# just assume all contacts are valid.
		for jid in self.cb.get_local_contacts():
			self.toLower(SubscribePresenceProtocolEntity(jid))
			self.cb.add_buddy(jid, "")
		#self.getStatuses?
		self.check_connected("contacts")

	def onListGroupsResult(self, groups):
		"""Save group info for later if the user decides to join."""
		for g in groups.getGroups():
			jid = g.getId()
			if "@" not in jid:
				jid += "@g.us"
			group = self.b.groups[jid]
			try:
				group["participants"] = g.getParticipants().keys()
			except AttributeError:
				# Depends on a change I made to yowsup that may
				# or may not get merged..
				group["participants"] = []
			
			# Save it. We're going to mix ListGroups elements and
			# Group-Subject notifications there, which don't have
			# consistent fieldnames for the same bits of info \o/
			g.getSubjectTimestamp = g.getSubjectTime
			group["topic"] = g

		self.check_connected("groups")

	def onLoginPong(self):
		if "contacts" in self.todo:
			# Shitty Whatsapp rejected the sync request, and
			# annoying Yowsup doesn't inform on error responses.
			# So instead, if we received no response to it but
			# did get our ping back, declare failure.
			self.onSyncResultFail()
		if "groups" in self.todo:
			# Well fuck this. Just reject ALL the things!
			# Maybe I don't need this one then.
			self.check_connected("groups")
		self.check_connected("ping")

	def getStatuses(self, contacts):
		return # Disabled since yowsup won't give us the result...
		self.toLower(GetStatusIqProtocolEntity(contacts))
		self.todo.add("statuses")

	@ProtocolEntityCallback("notification")
	def onNotification(self, ent):
		if isinstance(ent, StatusNotificationProtocolEntity):
			return self.onStatusNotification(ent)
		elif isinstance(ent, SubjectGroupsNotificationProtocolEntity):
			return self.onGroupSubjectNotification(ent)

	def onStatusNotification(self, status):
		print "New status for %s: %s" % (status.getFrom(), status.status)
		self.cb.buddy_status_msg(status.getFrom(), status.status)
	
	def onGroupSubjectNotification(self, sub):
		print "New /topic for %s: %s" % (sub.getFrom(), sub.getSubject())
		group = self.b.groups[sub.getFrom()]
		group["topic"] = sub
		id = group.get("id", None)
		if id is not None:
			self.cb.chat_topic(id, sub.getSubjectOwner(),
			                   sub.getSubject(), sub.getSubjectTimestamp())

	@ProtocolEntityCallback("media")
	def onMedia(self, med):
		"""Your PC better be MPC3 compliant!"""
		print "YAY MEDIA! %r" % med
		print med

	#@ProtocolEntityCallback("chatstate")
	#def onChatstate(self, entity):
	#	print(entity)


class YowsupDaemon(threading.Thread):
	daemon = True
	stack = None

	class Terminate(Exception):
		pass

	def run(self):
		try:
			self.stack.loop(timeout=0.2, discrete=0.2, count=1)
		except YowsupDaemon.Terminate:
			print "Exiting loop!"
			pass
	
	def StopDaemon(self):
		# Ugly, but yowsup offers no "run single iteration" version
		# of their event loop :-(
		raise YowsupDaemon.Terminate


class YowsupIMPlugin(implugin.BitlBeeIMPlugin):
	NAME = "wa"
	SETTINGS = {
		"cc": {
			# Country code. Seems to be required for registration only.
			"type": "int",
		},
		"reg_mode": {
			"default": "sms",
		},
		"name": {
			"flags": 0x100, # NULL_OK
		},
		# EW! Need to include this setting to trick BitlBee into
		# doing registration instead of refusing to login w/o pwd.
		# TODO: Make this a flag instead of faking oauth.
		"oauth": {
			"default": True,
		},
	}
	AWAY_STATES = ["Away"]
	ACCOUNT_FLAGS = 14 # HANDLE_DOMAINS + STATUS_MESSAGE + LOCAL_CONTACTS
	# TODO: HANDLE_DOMAIN in right place (add ... ... nick bug)
	PING_INTERVAL = 299 # seconds
	PING_TIMEOUT = 360 # seconds

	def login(self, account):
		super(YowsupIMPlugin, self).login(account)
		self.account = account
		self.number = self.account["user"].split("@")[0]
		self.registering = False
		if not self.account["pass"]:
			return self._register()
		
		self.stack = self._build_stack()
		self.daemon = YowsupDaemon(name="yowsup")
		self.daemon.stack = self.stack
		self.daemon.start()
		self.bee.log("Started yowsup thread")
		
		self.groups = collections.defaultdict(dict)
		self.groups_by_id = {}

		self.next_ping = None
		self.last_pong = time.time()

	def keepalive(self):
		if (time.time() - self.last_pong) > self.PING_TIMEOUT:
			self.bee.error("Ping timeout")
			self.bee.logout(True)
			return
		if self.next_ping and (time.time() < self.next_ping):
			return
		self.yow.Ship(PingIqProtocolEntity(to="s.whatsapp.net"))
		self.next_ping = time.time() + self.PING_INTERVAL

	def logout(self):
		self.stack.broadcastEvent(YowLayerEvent(YowNetworkLayer.EVENT_STATE_DISCONNECT))
		self.stack.execDetached(self.daemon.StopDaemon)

	def _register(self):
		self.registering = True
		self.bee.log("New account, starting registration")
		from yowsup.registration import WACodeRequest
		cr = WACodeRequest(str(self.setting("cc")), self.number,
		                   "000", "000", "000", "000",
		                   self.setting("reg_mode"))
		res = cr.send()
		res = {k: v for k, v in res.iteritems() if v is not None}
		if res.get("status", "") != "sent":
			self.bee.error("Failed to start registration: %r" % res)
			self.bee.logout(False)
			return
		
		text = ("Registration request sent. You will receive a SMS or "
		        "call with a confirmation code. Please respond to this "
		        "message with that code.")
		sender = "wa_%s" % self.number
		self.bee.add_buddy(sender, "")
		self.bee.buddy_msg(sender, text, 0, 0)

	def _register_confirm(self, code):
		from yowsup.registration import WARegRequest
		code = code.strip().replace("-", "")
		rr = WARegRequest(str(self.setting("cc")), self.number, code)
		res = rr.send()
		res = {k: v for k, v in res.iteritems() if v is not None}
		if (res.get("status", "") != "ok") or (not self.get("pw", "")):
			self.bee.error("Failed to finish registration: %r" % res)
			self.bee.logout(False)
			return
		self.bee.log("Registration finished, attempting login")
		self.bee.set_setstr("password", res["pw"])
		self.account["pass"] = res["pw"]
		self.login(self.account)

	def buddy_msg(self, to, text, flags):
		if self.registering:
			return self._register_confirm(text)
		msg = TextMessageProtocolEntity(text, to=to)
		self.yow.Ship(msg)

	def add_buddy(self, handle, _group):
		self.yow.Ship(GetSyncIqProtocolEntity(
		    ["+" + handle.split("@")[0]], mode=GetSyncIqProtocolEntity.MODE_DELTA))

	def remove_buddy(self, handle, _group):
		self.yow.Ship(UnsubscribePresenceProtocolEntity(handle))

	def set_away(self, state, status):
		print "Trying to set status to %r, %r" % (state, status)
		if state:
			# Only one option offered so None = available, not None = away.
			self.yow.Ship(AvailablePresenceProtocolEntity())
		else:
			self.yow.Ship(UnavailablePresenceProtocolEntity())
		if status:
			self.yow.Ship(SetStatusIqProtocolEntity(status))

	def set_set_name(self, _key, value):
		#self.yow.Ship(PresenceProtocolEntity(name=value))
		pass

	def chat_join(self, id, name, _nick, _password, settings):
		print "New chat created with id: %d" % id
		group = self.groups[name]
		group.update({"id": id, "name": name})
		self.groups_by_id[id] = group
		
		gi = group.get("topic", None)
		if gi:
			self.bee.chat_topic(id, gi.getSubjectOwner(),
			                    gi.getSubject(), gi.getSubjectTimestamp())
		
		# WA doesn't really have a concept of joined or not, just
		# long-term membership. Let's just sync state (we have
		# basic info but not yet a member list) and ACK the join
		# once that's done.
		# Well except that WA/YS killed this one. \o/
		#self.yow.Ship(ParticipantsGroupsIqProtocolEntity(name))
		
		# So for now do without a participant list..
		self.chat_join_participants(group)
		self.chat_send_backlog(group)

	def chat_join_participants(self, group):
		for p in group.get("participants", []):
			if p != self.account["user"]:
				self.bee.chat_add_buddy(group["id"], p)

	def chat_send_backlog(self, group):
		# Add the user themselves last to avoid a visible join flood.
		self.bee.chat_add_buddy(group["id"], self.account["user"])
		for msg in group.setdefault("queue", []):
			self.show_message(msg)
		del group["queue"]
	
	def chat_msg(self, id, text, flags):
		msg = TextMessageProtocolEntity(text, to=self.groups_by_id[id]["name"])
		self.yow.Ship(msg)

	def chat_leave(self, id):
		# WA never really let us leave, so just disconnect id and jid.
		group = self.groups_by_id[id]
		del self.groups_by_id[id]
		del group["id"]

	def _build_stack(self):
		creds = (self.number, self.account["pass"])

		stack = (YowStackBuilder()
		         .pushDefaultLayers(True)
		         .push(BitlBeeLayer)
		         .build())
		stack.setProp(YowAuthenticationProtocolLayer.PROP_CREDENTIALS, creds)
		stack.setProp(YowNetworkLayer.PROP_ENDPOINT, YowConstants.ENDPOINTS[0])
		stack.setProp(YowCoderLayer.PROP_DOMAIN, YowConstants.DOMAIN)
		stack.setProp(YowCoderLayer.PROP_RESOURCE, env.YowsupEnv.getCurrent().getResource())
		try:
			stack.setProp(YowIqProtocolLayer.PROP_PING_INTERVAL, 0)
		except AttributeError:
			# Ping setting only exists since May 2015.
			from yowsup.layers.protocol_iq.layer import YowPingThread
			YowPingThread.start = lambda x: None

		stack.setProp("org.bitlbee.Bijtje", self)

		stack.broadcastEvent(YowLayerEvent(YowNetworkLayer.EVENT_STATE_CONNECT))

		return stack


	# Not RPCs from here on.
	def show_message(self, msg):
		if hasattr(msg, "getBody"):
			text = msg.getBody()
		elif hasattr(msg, "getCaption") and hasattr(msg, "getMediaUrl"):
			lines = []
			if msg.getMediaUrl():
				lines.append(msg.getMediaUrl())
			else:
				lines.append("<Broken link>")
			if msg.getCaption():
				lines.append(msg.getCaption())
			text = "\n".join(lines)
		else:
			text = "Message of unknown type %r" % type(msg)

		if msg.getParticipant():
			group = self.groups[msg.getFrom()]
			if "id" in group:
				self.bee.chat_add_buddy(group["id"], msg.getParticipant())
				self.bee.chat_msg(group["id"], msg.getParticipant(), text, 0, msg.getTimestamp())
			else:
				self.bee.log("Warning: Activity in room %s" % msg.getFrom())
				self.groups[msg.getFrom()].setdefault("queue", []).append(msg)
		else:
			self.bee.buddy_msg(msg.getFrom(), text, 0, msg.getTimestamp())


implugin.RunPlugin(YowsupIMPlugin, debug=True)
