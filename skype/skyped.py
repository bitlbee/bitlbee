#!/usr/bin/env python
# 
#   skyped.py
#  
#   Copyright (c) 2007 by Miklos Vajna <vmiklos@frugalware.org>
#
#   It uses several code from a very basic python CLI interface, available at:
#
#   http://forum.skype.com/index.php?showtopic=42640
#  
#   This program is free software; you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation; either version 2 of the License, or
#   (at your option) any later version.
# 
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#  
#   You should have received a copy of the GNU General Public License
#   along with this program; if not, write to the Free Software
#   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, 
#   USA.
#

# makepkg configuration
""" GPL """
import sys
import signal
import locale
import time
import dbus
import dbus.service
import dbus.mainloop.glib
import gobject
import socket


SKYPE_SERVICE = 'com.Skype.API'
CLIENT_NAME = 'SkypeApiPythonShell'

# well, this is a bit hackish. we store the socket of the last connected client
# here and notify it. maybe later notify all connected clients?
conn = None

def sig_handler(signum, frame):
	mainloop.quit()

def input_handler(fd, io_condition):
	input = fd.recv(1024)
	for i in input.split("\n"):
		if i:
			fd.send((skype.send(i.strip()) + "\n").encode(locale.getdefaultlocale()[1]))
	return True

def server(host, port):
	sock = socket.socket()
	sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
	sock.bind((host, port))
	sock.listen(1)
	gobject.io_add_watch(sock, gobject.IO_IN, listener)

def listener(sock, *args):
	global conn
	conn, addr = sock.accept()
	fileno = conn.fileno()
	gobject.io_add_watch(conn, gobject.IO_IN, input_handler)
	return True

def dprint(msg):
	if len(sys.argv) > 1 and sys.argv[1] == "-d":
		print msg

class SkypeApi(dbus.service.Object):
	def __init__(self):
		bus = dbus.SessionBus()
		try:
			self.skype_api = bus.get_object(SKYPE_SERVICE, '/com/Skype')
		except dbus.exceptions.DBusException:
			sys.exit("Can't find any Skype instance. Are you sure you have started Skype?")

		reply = self.send('NAME ' + CLIENT_NAME)
		if reply != 'OK':
			sys.exit('Could not bind to Skype client')

		reply = self.send('PROTOCOL 5')
		try:
			dbus.service.Object.__init__(self, bus, "/com/Skype/Client", bus_name='com.Skype.API')
		except KeyError:
			sys.exit()

	# skype -> client (async)
	@dbus.service.method(dbus_interface='com.Skype.API')
	def Notify(self, msg_text):
		global conn
		dprint('<< ' + msg_text)
		if conn:
			conn.send(msg_text + "\n")

	# client -> skype (sync, 5 sec timeout)
	def send(self, msg_text):
		if not len(msg_text):
			return
		dprint('>> ' + msg_text)
		try:
			reply = self.skype_api.Invoke(msg_text)
		except dbus.exceptions.DBusException, s:
			reply = str(s)
			if(reply.startswith("org.freedesktop.DBus.Error.ServiceUnknown")):
				self.remove_from_connection(dbus.SessionBus(), "/com/Skype/Client")
				mainloop.quit()
		dprint('<< ' + reply)
		return reply

if __name__=='__main__':
	dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
	signal.signal(signal.SIGINT, sig_handler)
	mainloop = gobject.MainLoop()
	server('localhost', 2727)
	while True:
		skype = SkypeApi()
		mainloop.run()
