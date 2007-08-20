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

USER_PROPS = ('FULLNAME', 'SEX', 'LANGUAGE', 'COUNTRY', 'CITY', 'ABOUT',
             'ISAUTHORIZED', 'BUDDYSTATUS')


local_encoding = "latin2"
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

def ts():
   return time.strftime('[%H:%M:%S]')


def sig_handler(signum, frame):
   print '### caught signal %d, exiting' % signum
   mainloop.quit()
   #sys.exit()


def excepthook(type, value, traceback):
   mainloop.quit()
   return sys.__excepthook__(type, value, traceback)


def input_handler(fd, io_condition):
   #print '### fd=%d cond=%d' % (fd.fileno(), io_condition)
   input = fd.recv(1024)
   #if len(input) == 0:     # EOF
   #    mainloop.quit()
   #    return 0
   for i in input.split("\n"):
       do_command(i, fd)
   return True


def do_command(input, fd):
   line = input.strip()
   argv = line.split(None, 1)
   if len(argv) == 0:      # empty command
       return 1
   #print '###', argv

   cmd = argv[0]
   if cmd == 'q':
       mainloop.quit()
       return 0
   elif commands.has_key(cmd):
       commands[cmd](argv)
   else:
       # send as-is
       print fd
       fd.send(skype.send(line) + "\n")
   return 1


def cmd_help(argv):
   """help"""
   print 'q - quit'
   for cmd, handler in commands.items():
       if handler.__doc__:
           print cmd, '-', handler.__doc__
   print '<cmd> - send API <cmd> to skype'


def cmd_list_users(argv):
   """list user status"""
   reply = skype.send('SEARCH FRIENDS')
   if reply.startswith('USERS '):
       user_list = reply[6:].split(', ')
       online = {}
       for user in user_list:
           reply = skype.send('GET USER %s ONLINESTATUS' % user)
           status = reply.split()[3]
           if status != 'SKYPEOUT':
               online[user] = status;
       for user, status in online.items():
           print '%-16s [%s]' % (user, status)
   else:
       print reply


def cmd_who(argv):
   """list who's online"""
   reply = skype.send('SEARCH FRIENDS')
   if reply.startswith('USERS '):
       user_list = reply[6:].split(', ')
       who = {}
       for user in user_list:
           reply = skype.send('GET USER %s ONLINESTATUS' % user)
           status = reply.split()[3]
           if status != 'SKYPEOUT' and status != 'OFFLINE':
               who[user] = status;
       for user, status in who.items():
           print '%-16s [%s]' % (user, status)
   else:
       print reply


def cmd_message(argv):
   """send message"""
   if len(argv) < 2 or argv[1].find(' ') == -1:
       print 'usage: m user text...'
   else:
       (user, text) = argv[1].split(None, 1)
       print skype.send(' '.join((skype.msg_cmd, user, text)))


def cmd_userinfo(argv):
   """show user info"""
   if len(argv) == 1:
       print 'usage: i user'
   else:
       user = argv[1]
       for prop in USER_PROPS:
           reply = skype.send('GET USER %s %s' % (user, prop))
           if reply.startswith('USER '):
               res = reply.split(None, 3)
               if len(res) > 3:
                   print '%-13s: %s' % (prop.title(), res[3])
           else:
               print reply


def cmd_test(argv):
   """test"""
   print skype.send("MESSAGE echo123 one two three")


def cb_message(argv):
   args = argv[1].split(None, 3)
   msg_cmd = argv[0]
   msg_id = args[0]
   if args[1] == 'STATUS' and args[2] == 'READ':
       reply = skype.send('GET %s %s PARTNER_HANDLE' % (msg_cmd, msg_id))
       user = reply.split(None, 3)[3]
       reply = skype.send('GET %s %s BODY' % (msg_cmd, msg_id))
       res = reply.split(None, 3)
       print ts(), user, '>', res[3]


def cb_call(argv):
   args = argv[1].split(None, 3)
   call_id = args[0]
   if args[1] == 'STATUS':
       if args[2] == 'RINGING':
           reply = skype.send('GET CALL %s PARTNER_HANDLE' % call_id)
           user = reply.split()[3]
           reply = skype.send('GET CALL %s TYPE' % call_id)
           call_type = reply.split()[3]
           call_media = call_type.split('_')[1]
           if call_type.startswith('INCOMING'):
               print ts(), '*** Incoming', call_media, 'call from', user
           elif call_type.startswith('OUTGOING'):
               print ts(), '*** Outgoing', call_media, 'call to', user
       elif args[2] == 'MISSED':
           reply = skype.send('GET CALL %s PARTNER_HANDLE' % call_id)
           user = reply.split()[3]
           print ts(), '*** missed call from', user


def cb_user(argv):
   args = argv[1].split(None, 2)
   user = args[0]
   if args[1] == 'ONLINESTATUS' and args[2] != 'SKYPEOUT':
       print ts(), '***', user, 'is', args[2]


class SkypeApi(dbus.service.Object):
   def __init__(self):
       bus = dbus.SessionBus()

       try:
           self.skype_api = bus.get_object(SKYPE_SERVICE, '/com/Skype')
       except dbus.exceptions.DBusException:
           print "Can't find any Skype instance. Are you sure you have started Skype?"
	   sys.exit(0)

       reply = self.send('NAME ' + CLIENT_NAME)
       if reply != 'OK':
           sys.exit('Could not bind to Skype client')

       reply = self.send('PROTOCOL 5')
       #if reply != 'PROTOCOL 5':
       #    sys.exit('This test program only supports Skype API protocol version 1')
       self.msg_cmd = 'MESSAGE'

       self.callbacks = {'MESSAGE' : cb_message,
                         'CHATMESSAGE' : cb_message,
                         'USER' : cb_user,
                         'CALL' : cb_call}
       
       dbus.service.Object.__init__(self, bus, "/com/Skype/Client", bus_name='com.Skype.API')


   # skype -> client (async)
   @dbus.service.method(dbus_interface='com.Skype.API')
   def Notify(self, msg_text):
       global conn
       text = utf8_decode(msg_text)
       print ts(), '<<<', text
       if conn:
           conn.send(msg_text + "\n")
       argv = text.split(None, 1)
       if self.callbacks.has_key(argv[0]):
           self.callbacks[argv[0]](argv)

   # client -> skype (sync, 5 sec timeout)
   def send(self, msg_text):
       print '>> ', msg_text
       try:
	       reply = utf8_decode(self.skype_api.Invoke(utf8_encode(msg_text)))
       except dbus.exceptions.DBusException, s:
	       reply = str(s)
       print '<< ', reply
       return reply


commands = {'?' : cmd_help,
           'l' : cmd_list_users,
           'w' : cmd_who,
           'm' : cmd_message,
           'i' : cmd_userinfo,
           't' : cmd_test}

dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
skype = SkypeApi()

#print skype.send('GET SKYPEVERSION')
#print skype.send('GET USERSTATUS')

signal.signal(signal.SIGINT, sig_handler)
#gobject.io_add_watch(sys.stdin,
#                    gobject.IO_IN | gobject.IO_ERR | gobject.IO_HUP,
#                    input_handler)
#cmd_help(None)

mainloop = gobject.MainLoop()
sys.excepthook = excepthook

def server(host, port):
	'''Initialize server and start listening.'''
	sock = socket.socket()
	sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
	sock.bind((host, port))
	sock.listen(1)
	gobject.io_add_watch(sock, gobject.IO_IN, listener)
def listener(sock, *args):
	'''Asynchronous connection listener. Starts a handler for each connection.'''
	global conn
	conn, addr = sock.accept()
	fileno = conn.fileno()
	gobject.io_add_watch(conn, gobject.IO_IN, input_handler)
	return True
if __name__=='__main__':
	server('localhost', 2727)
	mainloop.run()
