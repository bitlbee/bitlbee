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

import sys
import os
import signal
import locale
import time
import gobject
import socket
import getopt
import Skype4Py
import threading

__version__ = "0.1.1"

SKYPE_SERVICE = 'com.Skype.API'
CLIENT_NAME = 'SkypeApiPythonShell'

# well, this is a bit hackish. we store the socket of the last connected client
# here and notify it. maybe later notify all connected clients?
conn = None

def input_handler(fd, io_condition):
	input = fd.recv(1024)
	for i in input.split("\n"):
		skype.send(i.strip())
	return True

def idle_handler(skype):
	try:
		skype.skype.SendCommand(skype.skype.Command(-1, "PING"))
	except Skype4Py.SkypeAPIError, s:
		dprint("Warning, pinging Skype failed (%s)." % (s))
	try:
		time.sleep(2)
	except KeyboardInterrupt:
		sys.exit("Exiting.")
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
	global options

	if options.debug:
		print msg

class SkypeApi():
	def __init__(self):
		self.skype = Skype4Py.Skype()
		self.skype.OnNotify = self.recv
		self.skype.Attach()

	def recv(self, msg_text):
		global conn
		if msg_text == "PONG":
			return
		if "\n" in msg_text:
			# crappy skype prefixes only the first line for
			# multiline messages so we need to do so for the other
			# lines, too. this is something like:
			# 'CHATMESSAGE id BODY first line\nsecond line' ->
			# 'CHATMESSAGE id BODY first line\nCHATMESSAGE id BODY second line'
			prefix = " ".join(msg_text.split(" ")[:3])
			msg_text = ["%s %s" % (prefix, i) for i in " ".join(msg_text.split(" ")[3:]).split("\n")]
		else:
			msg_text = [msg_text]
		for i in msg_text:
			# use utf-8 here to solve the following problem:
			# people use env vars like LC_ALL=en_US (latin1) then
			# they complain about why can't they receive latin2
			# messages.. so here it is: always use utf-8 then
			# everybody will be happy
			e = i.encode('UTF-8')
			dprint('<< ' + e)
			if conn:
				try:
					conn.send(e + "\n")
				except IOError, s:
					dprint("Warning, sending '%s' failed (%s)." % (e, s))

	def send(self, msg_text):
		if not len(msg_text):
			return
		e = msg_text.decode(locale.getdefaultlocale()[1])
		dprint('>> ' + e)
		try:
			c = self.skype.Command(e, Block=True)
			self.skype.SendCommand(c)
			self.recv(c.Reply)
		except Skype4Py.SkypeError:
			pass
		except Skype4Py.SkypeAPIError, s:
			dprint("Warning, sending '%s' failed (%s)." % (e, s))

class Options:
	def __init__(self):
		self.daemon = True
		self.debug = False
		self.help = False
		self.host = "0.0.0.0"
		self.port = 2727
		self.version = False

	def usage(self, ret):
		print """Usage: skyped [OPTION]...

skyped is a daemon that acts as a tcp server on top of a Skype instance.

Options:
	-d	--debug		enable debug messages
	-h	--help		this help
	-H	--host		set the tcp host (default: %s)
	-n	--nofork	don't run as daemon in the background
	-p	--port		set the tcp port (default: %d)
	-v	--version	display version information""" % (self.host, self.port)
		sys.exit(ret)

if __name__=='__main__':
	options = Options()
	try:
		opts, args = getopt.getopt(sys.argv[1:], "dhH:np:v", ["daemon", "help", "host=", "nofork", "port=", "version"])
	except getopt.GetoptError:
		options.usage(1)
	for opt, arg in opts:
		if opt in ("-d", "--debug"):
			options.debug = True
		elif opt in ("-h", "--help"):
			options.help = True
		elif opt in ("-H", "--host"):
			options.host = arg
		elif opt in ("-n", "--nofork"):
			options.daemon = False
		elif opt in ("-p", "--port"):
			options.port = arg
		elif opt in ("-v", "--version"):
			options.version = True
	if options.help:
		options.usage(0)
	elif options.version:
		print "skyped %s" % __version__
		sys.exit(0)
	elif options.daemon:
		pid = os.fork()
		if pid == 0:
			nullin = file('/dev/null', 'r')
			nullout = file('/dev/null', 'w')
			os.dup2(nullin.fileno(), sys.stdin.fileno())
			os.dup2(nullout.fileno(), sys.stdout.fileno())
			os.dup2(nullout.fileno(), sys.stderr.fileno())
		else:
			print 'skyped is started on port %s, pid: %d' % (options.port, pid)
			sys.exit(0)
	server(options.host, options.port)
	try:
		skype = SkypeApi()
	except Skype4Py.SkypeAPIError, s:
		sys.exit("%s. Are you sure you have started Skype?" % s)
	gobject.idle_add(idle_handler, skype)
	gobject.MainLoop().run()
