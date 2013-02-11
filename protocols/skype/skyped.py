#!/usr/bin/env python2.7
#
#   skyped.py
#
#   Copyright (c) 2007-2013 by Miklos Vajna <vmiklos@vmiklos.hu>
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
import time
import socket
import Skype4Py
import hashlib
from ConfigParser import ConfigParser, NoOptionError
from traceback import print_exception
from fcntl import fcntl, F_SETFD, FD_CLOEXEC
import ssl

__version__ = "0.1.1"

try:
	import gobject
	hasgobject = True
except ImportError:
	import select
	import threading
	hasgobject = False

def eh(type, value, tb):
	global options

	if type != KeyboardInterrupt:
		print_exception(type, value, tb)
	if hasgobject:
		gobject.MainLoop().quit()
	if options.conn:
		options.conn.close()
	if not options.dont_start_skype:
		# shut down client if it's running
		try:
			skype.skype.Client.Shutdown()
		except NameError:
			pass
	sys.exit("Exiting.")

sys.excepthook = eh

def wait_for_lock(lock, timeout_to_print, timeout, msg):
	start = time.time()
	locked = lock.acquire(0)
	while not(locked):
		time.sleep(0.5)
		if timeout_to_print and (time.time() - timeout_to_print > start):
			dprint("%s: Waited %f seconds" % \
					(msg, time.time() - start))
			timeout_to_print = False
		if timeout and (time.time() - timeout > start):
			dprint("%s: Waited %f seconds, giving up" % \
					(msg, time.time() - start))
			return False
		locked = lock.acquire(0)
	return True

def input_handler(fd, io_condition = None):
	global options
	global skype
	if options.buf:
		for i in options.buf:
			skype.send(i.strip())
		options.buf = None
		if not hasgobject:
			return True
	else:
		if not hasgobject:
			close_socket = False
			if wait_for_lock(options.lock, 3, 10, "input_handler"):
				try:
						input = fd.recv(1024)
						options.lock.release()
				except Exception, s:
					dprint("Warning, receiving 1024 bytes failed (%s)." % s)
					fd.close()
					options.conn = False
					options.lock.release()
					return False
				for i in input.split("\n"):
					if i.strip() == "SET USERSTATUS OFFLINE":
						close_socket = True
					skype.send(i.strip())
			return not(close_socket)
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
	except (Skype4Py.SkypeAPIError, AttributeError), s:
		dprint("Warning, pinging Skype failed (%s)." % (s))
		time.sleep(1)
	return True

def send(sock, txt):
	global options
	from time import sleep
	count = 1
	done = False
	if hasgobject:
		while (not done) and (count < 10):
			try:
				sock.send(txt)
				done = True
			except Exception, s:
				count += 1
				dprint("Warning, sending '%s' failed (%s). count=%d" % (txt, s, count))
				sleep(1)
		if not done:
			options.conn.close()
	else:
		while (not done) and (count < 10) and options.conn:
			if wait_for_lock(options.lock, 3, 10, "socket send"):
				try:
					 if options.conn: sock.send(txt)
					 options.lock.release()
					 done = True
				except Exception, s:
					options.lock.release()
					count += 1
					dprint("Warning, sending '%s' failed (%s). count=%d" % (txt, s, count))
					sleep(1)
		if not done:
			if options.conn:
				options.conn.close()
			options.conn = False
		return done

def bitlbee_idle_handler(skype):
	global options
	done = False
	if options.conn:
		try:
			e = "PING"
			done = send(options.conn, "%s\n" % e)
		except Exception, s:
			dprint("Warning, sending '%s' failed (%s)." % (e, s))
			if hasgobject:
				options.conn.close()
			else:
				if options.conn: options.conn.close()
				options.conn = False
				done = False
	if hasgobject:
		return True
	else:
		return done
	return True

def server(host, port, skype = None):
	global options
	if ":" in host:
		sock = socket.socket(socket.AF_INET6)
	else:
		sock = socket.socket()
	sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
	fcntl(sock, F_SETFD, FD_CLOEXEC);
	sock.bind((host, port))
	sock.listen(1)

	if hasgobject:
		gobject.io_add_watch(sock, gobject.IO_IN, listener)
	else:
		dprint("Waiting for connection...")
		listener(sock, skype)

def listener(sock, skype):
	global options
	if not hasgobject:
		if not(wait_for_lock(options.lock, 3, 10, "listener")): return False
	rawsock, addr = sock.accept()
	try:
		options.conn = ssl.wrap_socket(rawsock,
			server_side=True,
			certfile=options.config.sslcert,
			keyfile=options.config.sslkey,
			ssl_version=ssl.PROTOCOL_TLSv1)
	except ssl.SSLError:
		dprint("Warning, SSL init failed, did you create your certificate?")
		return False
	if hasattr(options.conn, 'handshake'):
		try:
			options.conn.handshake()
		except Exception:
			if not hasgobject:
				options.lock.release()
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
		if not hasgobject:
			options.conn = False
			options.lock.release()
		return False
	if ret == 2:
		dprint("Username and password OK.")
		options.conn.send("PASSWORD OK\n")
		if hasgobject:
			gobject.io_add_watch(options.conn, gobject.IO_IN, input_handler)
		else:
			options.lock.release()
			serverloop(options, skype)
		return True
	else:
		dprint("Username and/or password WRONG.")
		options.conn.send("PASSWORD KO\n")
		if not hasgobject:
			options.conn.close()
			options.conn = False
			options.lock.release()
		return False

def dprint(msg):
	from time import strftime
	global options

	now = strftime("%Y-%m-%d %H:%M:%S")

	if options.debug:
		try:
			print now + ": " + msg
		except Exception, s:
			try:
				sanitized = msg.encode("ascii", "backslashreplace")
			except Error, s:
				try:
					sanitized = "hex [" + msg.encode("hex") + "]"
				except Error, s:
					sanitized = "[unable to print debug message]"
			print now + "~=" + sanitized
		sys.stdout.flush()
	if options.log:
		sock = open(options.log, "a")
		sock.write("%s: %s\n" % (now, msg))
		sock.close()

class MockedSkype:
	"""Mock class for Skype4Py.Skype(), in case the -m option is used."""
	def __init__(self, mock):
		sock = open(mock)
		self.lines = sock.readlines()

	def SendCommand(self, c):
		pass

	def Command(self, msg, Block):
		if msg == "PING":
			return ["PONG"]
		line = self.lines[0].strip()
		if not line.startswith(">> "):
			raise Exception("Corrupted mock input")
		line = line[3:]
		if line != msg:
			raise Exception("'%s' != '%s'" % (line, msg))
		self.lines = self.lines[1:] # drop the expected incoming line
		ret = []
		while True:
			# and now send back all the following lines, up to the next expected incoming line
			if len(self.lines) == 0:
				break
			if self.lines[0].startswith(">> "):
				break
			if not self.lines[0].startswith("<< "):
				raise Exception("Corrupted mock input")
			ret.append(self.lines[0][3:].strip())
			self.lines = self.lines[1:]
		return ret

class SkypeApi:
	def __init__(self, mock):
		global options
		if not mock:
			self.skype = Skype4Py.Skype()
			self.skype.OnNotify = self.recv
			if not options.dont_start_skype:
				self.skype.Client.Start()
		else:
			self.skype = MockedSkype(mock)

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
			try:
				# Internally, BitlBee always uses UTF-8 and encodes/decodes as
				# necessary to communicate with the IRC client; thus send the
				# UTF-8 it expects
				e = i.encode('UTF-8')
			except:
				# Should never happen, but it's better to send difficult to
				# read data than crash because some message couldn't be encoded
				e = i.encode('ascii', 'backslashreplace')
			if options.conn:
				dprint('<< ' + e)
				try:
					send(options.conn, e + "\n")
				except Exception, s:
					dprint("Warning, sending '%s' failed (%s)." % (e, s))
					if options.conn: options.conn.close()
					options.conn = False
			else:
				dprint('-- ' + e)

	def send(self, msg_text):
		if not len(msg_text) or msg_text == "PONG":
			if msg_text == "PONG":
				options.last_bitlbee_pong = time.time()
			return
		try:
			# Internally, BitlBee always uses UTF-8 and encodes/decodes as
			# necessary to communicate with the IRC client; thus decode the
			# UTF-8 it sent us
			e = msg_text.decode('UTF-8')
		except:
			# Should never happen, but it's better to send difficult to read
			# data to Skype than to crash
			e = msg_text.decode('ascii', 'backslashreplace')
		dprint('>> ' + e)
		try:
			c = self.skype.Command(e, Block=True)
			self.skype.SendCommand(c)
			if hasattr(c, "Reply"):
				self.recv(c.Reply) # Skype4Py answer
			else:
				for i in c: # mock may return multiple iterable answers
					self.recv(i)
		except Skype4Py.SkypeError:
			pass
		except Skype4Py.SkypeAPIError, s:
			dprint("Warning, sending '%s' failed (%s)." % (e, s))


def serverloop(options, skype):
	timeout = 1; # in seconds
	skype_ping_period = 5
	bitlbee_ping_period = 10
	bitlbee_pong_timeout = 30
	now = time.time()
	skype_ping_start_time = now
	bitlbee_ping_start_time = now
	options.last_bitlbee_pong = now
	in_error = []
	handler_ok = True
	while (len(in_error) == 0) and handler_ok and options.conn:
		ready_to_read, ready_to_write, in_error = \
			select.select([options.conn], [], [options.conn], \
				timeout)
		now = time.time()
		handler_ok = len(in_error) == 0
		if (len(ready_to_read) == 1) and handler_ok:
			handler_ok = input_handler(ready_to_read.pop())
			# don't ping bitlbee/skype if they already received data
			now = time.time() # allow for the input_handler to take some time
			bitlbee_ping_start_time = now
			skype_ping_start_time = now
			options.last_bitlbee_pong = now
		if (now - skype_ping_period > skype_ping_start_time) and handler_ok:
			handler_ok = skype_idle_handler(skype)
			skype_ping_start_time = now
		if now - bitlbee_ping_period > bitlbee_ping_start_time:
			handler_ok = bitlbee_idle_handler(skype)
			bitlbee_ping_start_time = now
			if options.last_bitlbee_pong:
				if (now - options.last_bitlbee_pong) > bitlbee_pong_timeout:
					dprint("Bitlbee pong timeout")
					# TODO is following line necessary? Should there be a options.conn.unwrap() somewhere?
					# options.conn.shutdown()
					if options.conn:
						options.conn.close()
					options.conn = False
			else:
				options.last_bitlbee_pong = now


def main(args=None):
	global options
	global skype

	cfgpath = os.path.join(os.environ['HOME'], ".skyped", "skyped.conf")
	syscfgpath = "/usr/local/etc/skyped/skyped.conf"
	if not os.path.exists(cfgpath) and os.path.exists(syscfgpath):
		cfgpath = syscfgpath # fall back to system-wide settings
	port = 2727

	import argparse
	parser = argparse.ArgumentParser()
	parser.add_argument('-c', '--config',
		metavar='path', default=cfgpath,
		help='path to configuration file (default: %(default)s)')
	parser.add_argument('-H', '--host', default='0.0.0.0',
		help='set the tcp host, supports IPv4 and IPv6 (default: %(default)s)')
	parser.add_argument('-p', '--port', type=int,
		help='set the tcp port (default: %(default)s)')
	parser.add_argument('-l', '--log', metavar='path',
		help='set the log file in background mode (default: none)')
	parser.add_argument('-v', '--version', action='store_true', help='display version information')
	parser.add_argument('-n', '--nofork',
		action='store_true', help="don't run as daemon in the background")
	parser.add_argument('-s', '--dont-start-skype', action='store_true',
		help="assume that skype is running independently, don't try to start/stop it")
	parser.add_argument('-m', '--mock', help='fake interactions with skype (only useful for tests)')
	parser.add_argument('-d', '--debug', action='store_true', help='enable debug messages')
	options = parser.parse_args(sys.argv[1:] if args is None else args)

	if options.version:
		print "skyped %s" % __version__
		sys.exit(0)

	# well, this is a bit hackish. we store the socket of the last connected client
	# here and notify it. maybe later notify all connected clients?
	options.conn = None
	# this will be read first by the input handler
	options.buf = None

	if not os.path.exists(options.config):
		parser.error(( "Can't find configuration file at '%s'."
			"Use the -c option to specify an alternate one." )% options.config)

	cfgpath = options.config
	options.config = ConfigParser()
	options.config.read(cfgpath)
	options.config.username = options.config.get('skyped', 'username').split('#', 1)[0]
	options.config.password = options.config.get('skyped', 'password').split('#', 1)[0]
	options.config.sslkey = os.path.expanduser(options.config.get('skyped', 'key').split('#', 1)[0])
	options.config.sslcert = os.path.expanduser(options.config.get('skyped', 'cert').split('#', 1)[0])

	# hack: we have to parse the parameters first to locate the
	# config file but the -p option should overwrite the value from
	# the config file
	try:
		options.config.port = int(options.config.get('skyped', 'port').split('#', 1)[0])
		if not options.port:
			options.port = options.config.port
	except NoOptionError:
		pass
	if not options.port:
		options.port = port
	dprint("Parsing config file '%s' done, username is '%s'." % (options.config, options.config.username))
	if not options.nofork:
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
	if hasgobject:
		server(options.host, options.port)
	try:
		skype = SkypeApi(options.mock)
	except Skype4Py.SkypeAPIError, s:
		sys.exit("%s. Are you sure you have started Skype?" % s)
	if hasgobject:
		gobject.timeout_add(2000, skype_idle_handler, skype)
		gobject.timeout_add(60000, bitlbee_idle_handler, skype)
		gobject.MainLoop().run()
	else:
		while 1:
			options.conn = False
			options.lock = threading.Lock()
			server(options.host, options.port, skype)


if __name__ == '__main__': main()
