#!/usr/bin/python
#
# Part of BitlBee. Reads a libpurple accounts.xml file and generates some
# commands/XML that BitlBee understands. For easy migration from Pidgin/
# Finch/whatever to BitlBee, be it a public server or your own.
#
# Licensed under the GPL2 like the rest of BitlBee.
#
# Copyright 2010 Wilmer van der Gaast <wilmer@gaast.net>
#

import getopt
import getpass
import os
import subprocess
import sys

import xml.dom.minidom

BITLBEE = '/usr/sbin/bitlbee'

def parse_purple(f):
	protomap = {
		'msn-pecan': 'msn',
		'aim': 'oscar',
		'icq': 'oscar',
	}
	supported = ('msn', 'jabber', 'oscar', 'yahoo', 'twitter')
	accs = list()
	
	if os.path.isdir(f):
		f = f + '/accounts.xml'
	xt = xml.dom.minidom.parse(f)
	for acc in xt.getElementsByTagName('account')[1:]:
		protocol = acc.getElementsByTagName('protocol')[0].firstChild.wholeText
		name = acc.getElementsByTagName('name')[0].firstChild.wholeText
		try:
			password = acc.getElementsByTagName('password')[0].firstChild.wholeText
		except IndexError:
			password = ''
		if protocol.startswith('prpl-'):
			protocol = protocol[5:]
		if name.endswith('/'):
			name = name[:-1]
		if protocol in protomap:
			protocol = protomap[protocol]
		if protocol not in supported:
			print 'Warning: protocol probably not supported by BitlBee: ' + protocol
		accs.append((protocol, name, password))
	
	return accs

def print_commands(accs):
	print 'To copy all your Pidgin accounts to BitlBee, just copy-paste the following'
	print 'commands into your &bitlbee channel:'
	print
	for acc in accs:
		print 'account add %s %s "%s"' % acc

def bitlbee_x(*args):
	bb = subprocess.Popen([BITLBEE, '-x'] + list(args), stdout=subprocess.PIPE)
	return bb.stdout.read().strip()

def print_xml(accs):
	try:
		bitlbee_x('hash', 'blaataap')
	except:
		print "Can't find/use BitlBee binary. It has to be a 1.2.5 binary or higher."
		print
		usage()
	
	print 'BitlBee .xml files are encrypted using the identify password. Please type your'
	print 'preferred identify password.'
	user = getpass.getuser()
	pwd = getpass.getpass()
	
	root = xml.dom.minidom.Element('user')
	root.setAttribute('nick', user)
	root.setAttribute('password', bitlbee_x('hash', pwd))
	root.setAttribute('version', '1')
	for acc in accs:
		accx = xml.dom.minidom.Element('account')
		accx.setAttribute('protocol', acc[0])
		accx.setAttribute('handle', acc[1])
		accx.setAttribute('password', bitlbee_x('enc', pwd, acc[2]))
		accx.setAttribute('autoconnect', '1')
		root.appendChild(accx)
	
	print
	print 'Write the following XML data to a file called %s.xml (rename it if' % user.lower()
	print 'you want to use a different nickname). It should be in the directory where'
	print 'your BitlBee account files are stored (most likely /var/lib/bitlbee).'
	print
	print root.toprettyxml()

def usage():
	print 'Usage: %s [-f <purple accounts file>] [-b <bitlbee executable>] [-x]' % sys.argv[0]
	print
	print 'Generates "account add" commands by default. -x generates a .xml file instead.'
	print 'The accounts file can normally be found in ~/.purple/.'
	sys.exit(os.EX_USAGE)

try:
	flags = dict(getopt.getopt(sys.argv[1:], 'f:b:x')[0])
except getopt.GetoptError:
	usage()
if '-f' not in flags:
	usage()
if '-b' in flags:
	BITLBEE = flags['-b']

parsed = parse_purple(flags['-f'])
if '-x' in flags:
	print_xml(parsed)
else:
	print_commands(parsed)
