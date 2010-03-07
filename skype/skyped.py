#!/usr/bin/env python
# 
#   skyped.py
#  
#   Copyright (c) 2007, 2008, 2009 by Miklos Vajna <vmiklos@frugalware.org>
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
import hashlib
from ConfigParser import ConfigParser, NoOptionError
from traceback import print_exception

__version__ = "0.1.1"

def eh(type, value, tb):
	if type != KeyboardInterrupt:
		print_exception(type, value, tb)
	gobject.MainLoop().quit()
	# shut down client if it's running
	try:
		skype.skype.Client.Shutdown()
	except NameError:
		pass
	sys.exit("Exiting.")

sys.excepthook = eh

def input_handler(fd, io_condition):
	global options
	if options.buf:
		for i in options.buf:
			skype.send(i.strip())
		options.buf = None
	else:
		try:
			input = fd.recv(1024)
		except Exception, s:
			dprint("Warning, receiving 1024 bytes failed (%s)." % s)
			fd.close()
			return False
		for i in input.split("\n"):
			skype.send(i.strip())
		return True

def skype_idle_handler(skype):
	try:
		c = skype.skype.Command("PING", Block=True)
		skype.skype.SendCommand(c)
	except Skype4Py.SkypeAPIError, s:
		dprint("Warning, pinging Skype failed (%s)." % (s))
	return True

def bitlbee_idle_handler(skype):
	if options.conn:
		try:
			e = "PING"
			options.conn.send("%s\n" % e)
		except Exception, s:
			dprint("Warning, sending '%s' failed (%s)." % (e, s))
			options.conn.close()
	return True

def server(host, port):
	global options
	try:
		if "SKYPED_NO_GNUTLS" in os.environ.keys():
			dprint("Warning, using OpenSSL instead of gnutls as requested (not recommended).")
			raise ImportError
		from gnutls import crypto, connection
		cert = crypto.X509Certificate(open(options.config.sslcert).read())
		key = crypto.X509PrivateKey(open(options.config.sslkey).read())
		cred = connection.X509Credentials(cert, key)
		sock = connection.ServerSessionFactory(socket.socket(), cred)
	except ImportError:
		from OpenSSL import SSL
		ctx = SSL.Context(SSL.TLSv1_METHOD)
		ctx.use_privatekey_file(options.config.sslkey)
		ctx.use_certificate_file(options.config.sslcert)
		sock = SSL.Connection(ctx, socket.socket())
	sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
	sock.bind((host, port))
	sock.listen(1)
	gobject.io_add_watch(sock, gobject.IO_IN, listener)

def listener(sock, *args):
	global options
	options.conn, addr = sock.accept()
	if hasattr(options.conn, 'handshake'):
		try:
			options.conn.handshake()
		except Exception:
			dprint("Warning, handshake failed, closing connection.")
			return False
	ret = 0
	try:
		line = options.conn.recv(1024)
		if line.startswith("USERNAME") and line.split(' ')[1].strip() == options.config.username:
			ret += 1
		line = options.conn.recv(1024)
		if line.startswith("PASSWORD") and hashlib.sha1(line.split(' ')[1].strip()).hexdigest() == options.config.password:
			ret += 1
	except Exception, s:
		dprint("Warning, receiving 1024 bytes failed (%s)." % s)
		options.conn.close()
		return False
	if ret == 2:
		dprint("Username and password OK.")
		options.conn.send("PASSWORD OK\n")
		gobject.io_add_watch(options.conn, gobject.IO_IN, input_handler)
		return True
	else:
		dprint("Username and/or password WRONG.")
		options.conn.send("PASSWORD KO\n")
		return False

def dprint(msg):
	from time import strftime
	global options

	now = strftime("%Y-%m-%d %H:%M:%S")

	if options.debug:
		print now + ": " + msg
	if options.log:
		sock = open(options.log, "a")
		sock.write("%s: %s\n" % (now, msg))
		sock.close()

class SkypeApi:
	def __init__(self):
		self.skype = Skype4Py.Skype()
		self.skype.OnNotify = self.recv
		self.skype.Client.Start()

	def recv(self, msg_text):
		global options
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
			if options.conn:
				try:
					options.conn.send(e + "\n")
				except Exception, s:
					dprint("Warning, sending '%s' failed (%s)." % (e, s))
					options.conn.close()

	def send(self, msg_text):
		if not len(msg_text) or msg_text == "PONG":
			return
		try:
			encoding = locale.getdefaultlocale()[1]
			if not encoding:
				raise ValueError
			e = msg_text.decode(encoding)
		except ValueError:
			e = msg_text.decode('UTF-8')
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
		self.cfgpath = os.path.join(os.environ['HOME'], ".skyped", "skyped.conf")
		# for backwards compatibility
		self.syscfgpath = "/usr/local/etc/skyped/skyped.conf"
		if os.path.exists(self.syscfgpath):
			self.cfgpath = self.syscfgpath
		self.daemon = True
		self.debug = False
		self.help = False
		self.host = "0.0.0.0"
		self.log = None
		self.port = None
		self.version = False
		# well, this is a bit hackish. we store the socket of the last connected client
		# here and notify it. maybe later notify all connected clients?
		self.conn = None
		# this will be read first by the input handler
		self.buf = None


	def usage(self, ret):
		print """Usage: skyped [OPTION]...

skyped is a daemon that acts as a tcp server on top of a Skype instance.

Options:
	-c      --config        path to configuration file (default: %s)
	-d	--debug		enable debug messages
	-h	--help		this help
	-H	--host		set the tcp host (default: %s)
	-l      --log           set the log file in background mode (default: none)
	-n	--nofork	don't run as daemon in the background
	-p	--port		set the tcp port (default: %s)
	-v	--version	display version information""" % (self.cfgpath, self.host, self.port)
		sys.exit(ret)

if __name__=='__main__':
	options = Options()
	try:
		opts, args = getopt.getopt(sys.argv[1:], "c:dhH:l:np:v", ["config=", "daemon", "help", "host=", "log=", "nofork", "port=", "version"])
	except getopt.GetoptError:
		options.usage(1)
	for opt, arg in opts:
		if opt in ("-c", "--config"):
			options.cfgpath = arg
		elif opt in ("-d", "--debug"):
			options.debug = True
		elif opt in ("-h", "--help"):
			options.help = True
		elif opt in ("-H", "--host"):
			options.host = arg
		elif opt in ("-l", "--log"):
			options.log = arg
		elif opt in ("-n", "--nofork"):
			options.daemon = False
		elif opt in ("-p", "--port"):
			options.port = int(arg)
		elif opt in ("-v", "--version"):
			options.version = True
	if options.help:
		options.usage(0)
	elif options.version:
		print "skyped %s" % __version__
		sys.exit(0)
	# parse our config
	if not os.path.exists(options.cfgpath):
		print "Can't find configuration file at '%s'." % options.cfgpath
		print "Use the -c option to specify an alternate one."
		sys.exit(1)
	options.config = ConfigParser()
	options.config.read(options.cfgpath)
	options.config.username = options.config.get('skyped', 'username').split('#')[0]
	options.config.password = options.config.get('skyped', 'password').split('#')[0]
	options.config.sslkey = options.config.get('skyped', 'key').split('#')[0]
	options.config.sslcert = options.config.get('skyped', 'cert').split('#')[0]
	# hack: we have to parse the parameters first to locate the
	# config file but the -p option should overwrite the value from
	# the config file
	try:
		options.config.port = int(options.config.get('skyped', 'port').split('#')[0])
		if not options.port:
			options.port = options.config.port
	except NoOptionError:
		pass
	if not options.port:
		options.port = 2727
	dprint("Parsing config file '%s' done, username is '%s'." % (options.cfgpath, options.config.username))
	if options.daemon:
		pid = os.fork()
		if pid == 0:
			nullin = file(os.devnull, 'r')
			nullout = file(os.devnull, 'w')
			os.dup2(nullin.fileno(), sys.stdin.fileno())
			os.dup2(nullout.fileno(), sys.stdout.fileno())
			os.dup2(nullout.fileno(), sys.stderr.fileno())
		else:
			print 'skyped is started on port %s, pid: %d' % (options.port, pid)
			sys.exit(0)
	else:
		dprint('skyped is started on port %s' % options.port)
	server(options.host, options.port)
	try:
		skype = SkypeApi()
	except Skype4Py.SkypeAPIError, s:
		sys.exit("%s. Are you sure you have started Skype?" % s)
	gobject.timeout_add(2000, skype_idle_handler, skype)
	gobject.timeout_add(60000, bitlbee_idle_handler, skype)
	gobject.MainLoop().run()
