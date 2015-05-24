#!/usr/bin/python

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

class BitlBeeLayer(YowInterfaceLayer):

	def __init__(self, *a, **kwa):
		super(BitlBeeLayer, self).__init__(*a, **kwa)

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
		self.cb.connected()
		self.toLower(ListGroupsIqProtocolEntity())
		try:
			self.toLower(PresenceProtocolEntity(name=self.b.setting("name")))
		except KeyError:
			pass
		# Should send the contact list now, but BitlBee hasn't given
		# it yet. See set_away() and send_initial_contacts() below.
	
	@ProtocolEntityCallback("failure")
	def onFailure(self, entity):
		self.b = self.getStack().getProp("org.bitlbee.Bijtje")
		self.cb = self.b.bee
		self.cb.error(entity.getReason())
		self.cb.logout(False)

	def onEvent(self, event):
		print "Received event: %s name %s" % (event, event.getName())
		if event.getName() == "disconnect":
			self.getStack().execDetached(self.daemon.StopDaemon)
	
	@ProtocolEntityCallback("presence")
	def onPresence(self, pres):
		status = 8 # MOBILE
		if pres.getType() != "unavailable":
			status |= 1 # ONLINE
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
		receipt = OutgoingReceiptProtocolEntity(msg.getId(), msg.getFrom())
		self.toLower(receipt)

		if msg.getParticipant():
			group = self.b.groups.get(msg.getFrom(), None)
			if not group or "id" not in group:
				self.cb.log("Warning: Activity in room %s" % msg.getFrom())
				self.b.groups.setdefault(msg.getFrom(), {}).setdefault("queue", []).append(msg)
				return
			self.cb.chat_msg(group["id"], msg.getParticipant(), msg.getBody(), 0, msg.getTimestamp())
		else:
			self.cb.buddy_msg(msg.getFrom(), msg.getBody(), 0, msg.getTimestamp())

	@ProtocolEntityCallback("receipt")
	def onReceipt(self, entity):
		ack = OutgoingAckProtocolEntity(entity.getId(), entity.getTag(),
		                                entity.getType(), entity.getFrom())
		self.toLower(ack)

	@ProtocolEntityCallback("iq")
	def onIq(self, entity):
		if isinstance(entity, ResultSyncIqProtocolEntity):
			print "XXX SYNC RESULT RECEIVED!"
			return self.onSyncResult(entity)
		elif isinstance(entity, ListParticipantsResultIqProtocolEntity):
			return self.b.chat_join_participants(entity)
		elif isinstance(entity, ListGroupsResultIqProtocolEntity):
			return self.onListGroupsResult(entity)
	
	def onSyncResult(self, entity):
		# TODO HERE AND ELSEWHERE: Thread idiocy happens when going
		# from here to the IMPlugin. Check how bjsonrpc lets me solve that.
		# ALSO TODO: See why this one doesn't seem to be called for adds later.
		ok = set(jid.lower() for jid in entity.inNumbers.values())
		for handle in self.b.contacts:
			if handle.lower() in ok:
				self.toLower(SubscribePresenceProtocolEntity(handle))
				self.cb.add_buddy(handle, "")
		if entity.outNumbers:
			self.cb.error("Not on WhatsApp: %s" %
			              ", ".join(entity.outNumbers.keys()))
		if entity.invalidNumbers:
			self.cb.error("Invalid numbers: %s" %
			              ", ".join(entity.invalidNumbers.keys()))

	def onListGroupsResult(self, groups):
		"""Save group info for later if the user decides to join."""
		for g in groups.getGroups():
			jid = g.getId()
			if "@" not in jid:
				jid += "@g.us"
			group = self.b.groups.setdefault(jid, {})
			group["info"] = g

	@ProtocolEntityCallback("notification")
	def onNotification(self, ent):
		if isinstance(ent, StatusNotificationProtocolEntity):
			return self.onStatusNotification(ent)

	def onStatusNotification(self, status):
		print "New status for %s: %s" % (status.getFrom(), status.status)
		self.bee.buddy_status_msg(status.getFrom(), status.status)

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
			"type": "int",
		},
		"name": {
			"flags": 0x100, # NULL_OK
		},
	}
	AWAY_STATES = ["Away"]
	ACCOUNT_FLAGS = 14 # HANDLE_DOMAINS + STATUS_MESSAGE + LOCAL_CONTACTS
	# TODO: LOCAL LIST CAUSES CRASH!
	# TODO: HANDLE_DOMAIN in right place (add ... ... nick bug)
	# TODO? Allow set_away (for status msg) even if AWAY_STATES not set?
	#   and/or, see why with the current value set_away state is None.

	def login(self, account):
		self.stack = self.build_stack(account)
		self.daemon = YowsupDaemon(name="yowsup")
		self.daemon.stack = self.stack
		self.daemon.start()
		self.bee.log("Started yowsup thread")
		
		self.logging_in = True
		self.contacts = set()
		self.groups = {}
		self.groups_by_id = {}

	def keepalive(self):
		# Too noisy while debugging
		pass
		#self.yow.Ship(PingIqProtocolEntity(to="s.whatsapp.net"))

	def logout(self):
		self.stack.broadcastEvent(YowLayerEvent(YowNetworkLayer.EVENT_STATE_DISCONNECT))
		self.stack.execDetached(self.daemon.StopDaemon)

	def buddy_msg(self, to, text, flags):
		msg = TextMessageProtocolEntity(text, to=to)
		self.yow.Ship(msg)

	def add_buddy(self, handle, _group):
		if self.logging_in:
			# Need to batch up the initial adds. This is a "little" ugly.
			self.contacts.add(handle)
		else:
			self.yow.Ship(GetSyncIqProtocolEntity(
			    ["+" + handle.split("@")[0]], mode=GetSyncIqProtocolEntity.MODE_DELTA))
			self.yow.Ship(SubscribePresenceProtocolEntity(handle))

	def remove_buddy(self, handle, _group):
		self.yow.Ship(UnsubscribePresenceProtocolEntity(handle))

	def set_away(self, state, status):
		# When our first status is set, we've finalised login.
		# Which means sync the full contact list now.
		if self.logging_in:
			self.logging_in = False
			self.send_initial_contacts()
		
		print "Trying to set status to %r, %r" % (state, status)
		if state:
			# Only one option offered so None = available, not None = away.
			self.yow.Ship(AvailablePresenceProtocolEntity())
		else:
			self.yow.Ship(UnavailablePresenceProtocolEntity())
		if status:
			self.yow.Ship(SetStatusIqProtocolEntity(status))

	def send_initial_contacts(self):
		if not self.contacts:
			return
		numbers = [("+" + x.split("@")[0]) for x in self.contacts]
		self.yow.Ship(GetSyncIqProtocolEntity(numbers))

	def set_set_name(self, _key, value):
		self.yow.Ship(PresenceProtocolEntity(name=value))

	def chat_join(self, id, name, _nick, _password, settings):
		print "New chat created with id: %d" % id
		self.groups.setdefault(name, {}).update({"id": id, "name": name})
		group = self.groups[name]
		self.groups_by_id[id] = group
		
		gi = group.get("info", None)
		if gi:
			self.bee.chat_topic(id, gi.getSubjectOwner(),
			                    gi.getSubject(), gi.getSubjectTime())
		
		# WA doesn't really have a concept of joined or not, just
		# long-term membership. Let's just get a list of members and
		# pretend we've "joined" then.
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
			self.bee.chat_msg(group["id"], msg.getParticipant(), msg.getBody(), 0, msg.getTimestamp())
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
		stack.setProp("org.bitlbee.Bijtje", self)

		stack.broadcastEvent(YowLayerEvent(YowNetworkLayer.EVENT_STATE_CONNECT))

		return stack

implugin.RunPlugin(YowsupIMPlugin, debug=True)
