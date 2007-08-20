#!/usr/bin/env python
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

local_encoding = locale.getdefaultlocale()[1]
need_conv = (local_encoding != 'utf-8')

# well, this is a bit hackish. we store the socket of the last connected client
# here and notify it. maybe later notify all connected clients?
conn = None

def utf8_decode(utf8_str):
	if need_conv:
		return utf8_str.decode('utf-8').encode(local_encoding, 'replace')
	else:
		return utf8_str

def utf8_encode(local_str):
	if need_conv:
		return local_str.decode(local_encoding).encode('utf-8')
	else:
		return local_str

def sig_handler(signum, frame):
	mainloop.quit()

def input_handler(fd, io_condition):
	input = fd.recv(1024)
	for i in input.split("\n"):
		if i:
			fd.send(skype.send(i.strip()) + "\n")
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
		dbus.service.Object.__init__(self, bus, "/com/Skype/Client", bus_name='com.Skype.API')

	# skype -> client (async)
	@dbus.service.method(dbus_interface='com.Skype.API')
	def Notify(self, msg_text):
		global conn
		text = utf8_decode(msg_text)
		dprint('<< ' + text)
		if conn:
			conn.send(msg_text + "\n")

	# client -> skype (sync, 5 sec timeout)
	def send(self, msg_text):
		if not len(msg_text):
			return
		dprint('>> ' + msg_text)
		try:
			reply = utf8_decode(self.skype_api.Invoke(utf8_encode(msg_text)))
		except dbus.exceptions.DBusException, s:
			reply = str(s)
		dprint('<< ' + reply)
		return reply

if __name__=='__main__':
	dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
	skype = SkypeApi()
	signal.signal(signal.SIGINT, sig_handler)
	mainloop = gobject.MainLoop()
	server('localhost', 2727)
	mainloop.run()
