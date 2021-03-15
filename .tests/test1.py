import socket
import sys
import time
import select

MESSAGETEST = True
BLOCKTEST = False
OFFLINETEST = False
RENAMETEST = True

FAILED = False

class IrcClient:
    def __init__(self, nick, pwd):
        self.nick = nick
        self.pwd = pwd
        self.log = ''
        self.sck = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

    def send_raw(self, msg, loud = True):
        self.receive()
        if loud:
            print('FROM '+ self.nick + '|| ' + msg)
        self.log += msg+'\r\n'
        self.sck.send((msg+'\r\n').encode())

    def send_priv_msg(self, recip, msg, loud = True):
        self.send_raw('PRIVMSG '+recip+' :'+msg, loud)

    def connect(self):
        try:
            self.sck.connect(('127.0.0.1', 6667))
        except:
            print("IRC connection failed for " + self.nick)
            sys.exit(1)
        
        print("IRC connection established for " + self.nick)

        self.send_raw('USER ' + (self.nick + " ")*3)
        self.send_raw('NICK ' + self.nick)
        self.send_raw('JOIN &bitlbee')

    def jabber_login(self):
        self.send_priv_msg("&bitlbee", "account add jabber "+self.nick+"@localhost "+self.pwd)
        time.sleep(0.3)
        self.send_priv_msg("&bitlbee", "account on")
        time.sleep(1)
        self.receive()
        if self.log.find('Logged in') == -1:
            print("Jabber login failed for " + self.nick)
            sys.exit(1)
        else:
            print("Jabber login successful for " + self.nick)

    def receive(self):
        text = ''
        while True:
            readable, _, _ = select.select([self.sck], [], [], 5)
            if self.sck in readable:
                text += self.sck.recv(2040).decode()
                for line in text.split('\n'):
                    if line.find('PING') != -1:
                        self.send_raw('PONG ' + line.split()[1])
            else:
                break
        self.log += text
        return text

    def add_jabber_buddy(self, nick):
        self.send_priv_msg("&bitlbee", "add 0 " + nick+"@localhost")
    
    def block_jabber_buddy(self, nick):
        self.send_priv_msg("&bitlbee", "block " + nick)

    def unblock_jabber_buddy(self, nick):
        self.send_priv_msg("&bitlbee", "allow " + nick)

    def rename_jabber_buddy(self, oldnick, newnick):
        self.send_priv_msg("&bitlbee", "rename " + oldnick + " " + newnick)
        
def test_send_message(sender, receiver, message):
    sender.send_priv_msg(receiver.nick, message)
    received = receiver.receive().find(message) != -1
    return received;

def run_tests():
    global FAILED
    clis = []
    clis += [IrcClient('test1', 'asd')]
    clis += [IrcClient('test2', 'asd')]
    for cli in clis:
        cli.connect()
        cli.jabber_login()

    clis[0].add_jabber_buddy(clis[1].nick)

    if MESSAGETEST:
        print("Test: Send message")
        ret = test_send_message(clis[0], clis[1], 'ohai <3')
        ret = ret & test_send_message(clis[1], clis[0], 'uwu *pounces*')
        if ret:
            print("Test passed")
        else:
            print("Test failed")
            FAILED = True;
            

    if BLOCKTEST:
        print("Test: Block/Unblock")
        clis[0].block_jabber_buddy(clis[1].nick)
        ret = not test_send_message(clis[1], clis[0], 'm-meow?')
        clis[0].unblock_jabber_buddy(clis[1].nick)
        ret = ret & test_send_message(clis[1], clis[0], '*purrs*')
        if ret:
            print("Test passed")
        else:
            print("Test failed")
            FAILED = True;

    if RENAMETEST:
        print("Test: Rename buddy")
        newname = "xXx_pup_LINKENPARK4EVA"
        message = "rawr meanmz i luv<3 u in dinosaur"

        clis[0].rename_jabber_buddy(clis[1].nick, newname)
        clis[0].send_priv_msg(newname, message)
        ret = clis[1].receive().find(message) != -1

        clis[0].rename_jabber_buddy(newname, clis[1].nick)
        ret = ret & test_send_message(clis[0], clis[1], "rawr")
        if ret:
            print("Test passed")
        else:
            print("Test failed")
            FAILED = True;

    if FAILED:
        print("\ntest1 Log:\n"+clis[0].log)
        print("\ntest2 Log:\n"+clis[1].log)
    
if __name__ == "__main__":
    run_tests()
    if FAILED:
        sys.exit(1)
