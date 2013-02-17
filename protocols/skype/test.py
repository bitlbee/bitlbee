#!/usr/bin/env python2.7

import subprocess
import sys
import pexpect
import unittest
import shutil
import os
import hashlib

def openssl(args):
	with open(os.devnull, "w") as devnull:
		proc = subprocess.Popen(['openssl'] + args, stdin=subprocess.PIPE, stderr=devnull)
		for i in range(6):
			proc.stdin.write("\n")
		proc.stdin.close()
		proc.communicate()
def setupSkyped():
	try:
		shutil.rmtree("t/skyped")
	except OSError:
		pass
	os.makedirs("t/skyped")
	cwd = os.getcwd()
	os.chdir("t/skyped")
	try:
		shutil.copyfile("../../skyped.cnf", "skyped.cnf")
		openssl(['req', '-new', '-x509', '-days', '365', '-nodes', '-config', 'skyped.cnf', '-out', 'skyped.cert.pem', '-keyout', 'skyped.key.pem'])
		with open("skyped.conf", "w") as sock:
			sock.write("[skyped]\n")
			sock.write("username = alice\n")
			sock.write("password = %s\n" % hashlib.sha1("foo").hexdigest())
			sock.write("cert = %s/skyped.cert.pem\n" % os.getcwd())
			sock.write("key = %s/skyped.key.pem\n" % os.getcwd())
			sock.write("port = 2727\n")
	finally:
		os.chdir(cwd)

class Test(unittest.TestCase):
	def mock(self, name):
		with open("t/skyped.log", "w") as skyped_log,\
				open("t/pexpect.log", "w") as pexpect_log:
			skyped = subprocess.Popen([sys.executable, "skyped.py",
				"-c", "t/skyped/skyped.conf", "-n", "-d", "-m", "t/%s-skyped.mock" % name],
				stdout=skyped_log, stderr=subprocess.STDOUT)
			try:
				bitlbee = pexpect.spawn('../../bitlbee', ['-d', 't/bitlbee'], logfile=pexpect_log)
				if os.environ.get('ATTACH_GDB'):
					subprocess.Popen(['gdb', '-batch-silent',
						'-ex', 'set logging overwrite on',
						'-ex', 'set logging file t/gdb-%s.log' % bitlbee.pid,
						'-ex', 'set logging on',
						'-ex', 'handle all pass nostop noprint',
						'-ex', 'handle SIGSEGV pass stop print',
						'-ex', 'set pagination 0',
						'-ex', 'continue',
						'-ex', 'backtrace full',
						'-ex', 'info registers',
						'-ex', 'thread apply all backtrace',
						'-ex', 'quit',
						'../../bitlbee', str(bitlbee.pid) ])
				bitlbee_mock = open("t/%s-bitlbee.mock" % name)
				for i in bitlbee_mock.readlines():
					line = i.strip()
					if line.startswith(">> "):
						bitlbee.expect_exact(line[3:], timeout=10)
					elif line.startswith("<< "):
						bitlbee.sendline(line[3:])
				bitlbee_mock.close()
				bitlbee.close()
			finally:
				skyped.terminate()
				skyped.communicate()

	def setUp(self):
		try:
			shutil.rmtree("t/bitlbee")
		except OSError:
			pass
		os.makedirs("t/bitlbee")

	def testMsg(self):
		self.mock("msg")

	def testLogin(self):
		self.mock("login")

	def testInfo(self):
		self.mock("info")

	def testCall(self):
		self.mock("call")

	def testCallFailed(self):
		self.mock("call-failed")

	def testAddYes(self):
		self.mock("add-yes")

	def testAddedYes(self):
		self.mock("added-yes")

	def testAddedNo(self):
		self.mock("added-no")

	def testGroupchatInvited(self):
		self.mock("groupchat-invited")

	def testGroupchatInvite(self):
		self.mock("groupchat-invite")

	def testCalledYes(self):
		self.mock("called-yes")

	def testCalledNo(self):
		self.mock("called-no")

	def testFiletransfer(self):
		self.mock("filetransfer")

	def testGroupRead(self):
		self.mock("group-read")

	def testCtcpHelp(self):
		self.mock("ctcp-help")

	def testSetMoodText(self):
		self.mock("set-mood-text")

if __name__ == '__main__':
	setupSkyped()
	unittest.main()
