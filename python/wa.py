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
from yowsup.layers.axolotl import YowAxolotlLayer
from yowsup.common.tools import ModuleTools

import implugin

logger = logging.getLogger("yowsup.layers.network.layer")
logger.setLevel(logging.DEBUG)
ch = logging.StreamHandler()
ch.setLevel(logging.DEBUG)
logger.addHandler(ch)

"""
TODO/Things I'm unhappy about:

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

# Tried this but yowsup is not passing back the result, will have to update the library. :-(
class GetStatusIqProtocolEntity(IqProtocolEntity):
	def __init__(self, jids=None):
		super(GetStatusIqProtocolEntity, self).__init__("status", None, _type="get", to="s.whatsapp.net")
		self.jids = jids or []

	def toProtocolTreeNode(self):
		from yowsup.structs import ProtocolTreeNode
		
		node = super(GetStatusIqProtocolEntity, self).toProtocolTreeNode()
		sr = ProtocolTreeNode("status")
		node.addChild(sr)
		for jid in self.jids:
			sr.addChild(ProtocolTreeNode("user", {"jid": jid}))
		return node


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
		elif isinstance(entity, ListParticipantsResultIqProtocolEntity):
			return self.b.chat_join_participants(entity)
		elif isinstance(entity, ListGroupsResultIqProtocolEntity):
			return self.onListGroupsResult(entity)
		elif "ping" in self.todo: # Pong has no type, sigh.
			if "contacts" in self.todo:
				# Shitty Whatsapp rejected the sync request, and
				# annoying Yowsup doesn't inform on error responses.
				# So instead, if we received no response to it but
				# did get our ping back, declare failure.
				self.onSyncResultFail()
			self.check_connected("ping")
	
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
			
			# Save it. We're going to mix ListGroups elements and
			# Group-Subject notifications there, which don't have
			# consistent fieldnames for the same bits of info \o/
			g.getSubjectTimestamp = g.getSubjectTime
			group["topic"] = g

		self.check_connected("groups")

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
		self.bee.buddy_status_msg(status.getFrom(), status.status)
	
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
		"name": {
			"flags": 0x100, # NULL_OK
		},
	}
	AWAY_STATES = ["Away"]
	ACCOUNT_FLAGS = 14 # HANDLE_DOMAINS + STATUS_MESSAGE + LOCAL_CONTACTS
	# TODO: HANDLE_DOMAIN in right place (add ... ... nick bug)
	PING_INTERVAL = 299 # seconds

	def login(self, account):
		self.stack = self.build_stack(account)
		self.daemon = YowsupDaemon(name="yowsup")
		self.daemon.stack = self.stack
		self.daemon.start()
		self.bee.log("Started yowsup thread")
		
		self.groups = collections.defaultdict(dict)
		self.groups_by_id = {}

		self.next_ping = None

	def keepalive(self):
		if self.next_ping and (time.time() < self.next_ping):
			return
		self.yow.Ship(PingIqProtocolEntity(to="s.whatsapp.net"))
		self.next_ping = time.time() + self.PING_INTERVAL

	def logout(self):
		self.stack.broadcastEvent(YowLayerEvent(YowNetworkLayer.EVENT_STATE_DISCONNECT))
		self.stack.execDetached(self.daemon.StopDaemon)

	def buddy_msg(self, to, text, flags):
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
		self.yow.Ship(PresenceProtocolEntity(name=value))

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
		self.yow.Ship(ParticipantsGroupsIqProtocolEntity(name))

	def chat_join_participants(self, entity):
		group = self.groups[entity.getFrom()]
		id = group["id"]
		for p in entity.getParticipants():
			if p != self.account["user"]:
				self.bee.chat_add_buddy(id, p)

		# Add the user themselves last to avoid a visible join flood.
		self.bee.chat_add_buddy(id, self.account["user"])
		for msg in group.setdefault("queue", []):
			self.b.show_message(msg)
		del group["queue"]
	
	def chat_msg(self, id, text, flags):
		msg = TextMessageProtocolEntity(text, to=self.groups_by_id[id]["name"])
		self.yow.Ship(msg)

	def chat_leave(self, id):
		# WA never really let us leave, so just disconnect id and jid.
		group = self.groups_by_id[id]
		del self.groups_by_id[id]
		del group["id"]

	def build_stack(self, account):
		self.account = account
		creds = (account["user"].split("@")[0], account["pass"])

		stack = (YowStackBuilder()
		         .pushDefaultLayers(False)
		         .push(BitlBeeLayer)
		         .build())
		stack.setProp(YowAuthenticationProtocolLayer.PROP_CREDENTIALS, creds)
		stack.setProp(YowNetworkLayer.PROP_ENDPOINT, YowConstants.ENDPOINTS[0])
		stack.setProp(YowCoderLayer.PROP_DOMAIN, YowConstants.DOMAIN)
		stack.setProp(YowCoderLayer.PROP_RESOURCE, env.CURRENT_ENV.getResource())
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

		if msg.getParticipant():
			group = self.groups[msg.getFrom()]
			if "id" in group:
				self.bee.chat_msg(group["id"], msg.getParticipant(), text, 0, msg.getTimestamp())
			else:
				self.bee.log("Warning: Activity in room %s" % msg.getFrom())
				self.groups[msg.getFrom()].setdefault("queue", []).append(msg)
		else:
			self.bee.buddy_msg(msg.getFrom(), text, 0, msg.getTimestamp())


implugin.RunPlugin(YowsupIMPlugin, debug=True)
