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
		print entity
		super(BitlBeeLayer, self).receive(entity)

	def Ship(self, entity):
		"""Send an entity into Yowsup, but through the correct thread."""
		print "Queueing: %s" % entity.getTag()
		print entity
		def doit():
			self.toLower(entity)
		self.getStack().execDetached(doit)

	@ProtocolEntityCallback("success")
	def onSuccess(self, entity):
		self.b = self.getStack().getProp("org.bitlbee.Bijtje")
		self.cb = self.b.bee
		self.b.yow = self
		self.cb.connected()
		self.toLower(AvailablePresenceProtocolEntity())
	
	@ProtocolEntityCallback("failure")
	def onFailure(self, entity):
		self.b = self.getStack().getProp("org.bitlbee.Bijtje")
		self.cb = self.b.bee
		self.cb.error(entity.getReason())
		self.cb.logout(False)

	def onEvent(self, event):
		print event
		if event.getName() == "disconnect":
			self.getStack().execDetached(self.daemon.StopDaemon)
	
	@ProtocolEntityCallback("presence")
	def onPresence(self, pres):
		status = 8 # MOBILE
		online = isinstance(pres, AvailablePresenceProtocolEntity)
		if online:
			status += 1 # ONLINE
		imcb_buddy_status(pres.getFrom(), status, None, None)
	
	@ProtocolEntityCallback("message")
	def onMessage(self, msg):
		self.cb.buddy_msg(msg.getFrom(), msg.getBody(), 0, msg.getTimestamp())

		receipt = OutgoingReceiptProtocolEntity(msg.getId(), msg.getFrom())
		self.toLower(receipt)

	@ProtocolEntityCallback("receipt")
	def onReceipt(self, entity):
		print "ACK THE ACK!"
		ack = OutgoingAckProtocolEntity(entity.getId(), entity.getTag(),
		                                entity.getType(), entity.getFrom())
		self.toLower(ack)

	@ProtocolEntityCallback("iq")
	def onIq(self, entity):
		if isinstance(entity, ResultSyncIqProtocolEntity):
			return self.onSyncResult(entity)
	
	def onSyncResult(self, entity):
		# TODO HERE AND ELSEWHERE: Threat idiocy happens when going
		# from here to the IMPlugin. Check how bjsonrpc lets me solve that.
		ok = set(num.lstrip("+") for num in entity.inNumbers)
		for handle in self.b.contacts:
			if handle.split("@")[0] in ok:
				self.toLower(SubscribePresenceProtocolEntity(handle))
				self.cb.add_buddy(handle, "")
		if entity.outNumbers:
			self.cb.error("Not on WhatsApp: %s" % ", ".join(entity.outNumbers))
		if entity.invalidNumbers:
			self.cb.error("Invalid numbers: %s" % ", ".join(entity.invalidNumbers))

	@ProtocolEntityCallback("notification")
	def onNotification(self, ent):
		if isinstance(ent, StatusNotificationProtocolEntity):
			return self.onStatusNotification(ent)
	
	def onStatusNotification(self, status):
		print "New status for %s: %s" % (status.getFrom(), status.status)

	@ProtocolEntityCallback("chatstate")
	def onChatstate(self, entity):
		print(entity)


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
	AWAY_STATES = ["Available"]
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

	def keepalive(self):
		self.yow.Ship(PingIqProtocolEntity(to="s.whatsapp.net"))

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

	def set_away(self, _state, status):
		# When our first status is set, we've finalised login.
		# Which means sync the full contact list now.
		if self.logging_in:
			self.logging_in = False
			self.send_initial_contacts()
		
		# I think state is not supported?
		print "Trying to set status to %r, %r" % (_state, status)
		self.yow.Ship(SetStatusIqProtocolEntity(status))

	def send_initial_contacts(self):
		if not self.contacts:
			return
		numbers = [("+" + x.split("@")[0]) for x in self.contacts]
		self.yow.Ship(GetSyncIqProtocolEntity(numbers))

	def set_set_name(self, _key, value):
		self.yow.Ship(PresenceProtocolEntity(name=value))

	def build_stack(self, account):
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
