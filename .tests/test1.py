import socket
import sys
import time
import select

YESTEST = True
MESSAGETEST = True
BLOCKTEST = False
OFFLINETEST = False
RENAMETEST = True
SHOWLOG = True

FAILED = []

SEPARATOR = "="*60
SMOLPARATOR = "-"*60

class IrcClient:
    def __init__(self, nick, pwd):
        self.nick = nick
        self.pwd = pwd
        self.log = ''
        self.tmplog = ''
        self.sck = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

    def send_raw(self, msg, loud = False):
        self.receive()
        if loud:
            print('FROM '+ self.nick + '|| ' + msg)
        self.log += msg+'\r\n'
        self.tmplog += msg+'\r\n'
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
        self.tmplog += text
        return text

    def add_jabber_buddy(self, nick):
        self.send_priv_msg("&bitlbee", "add 0 " + nick+"@localhost")
    
    def block_jabber_buddy(self, nick):
        self.send_priv_msg("&bitlbee", "block " + nick)

    def unblock_jabber_buddy(self, nick):
        self.send_priv_msg("&bitlbee", "allow " + nick)

    def rename_jabber_buddy(self, oldnick, newnick):
        self.send_priv_msg("&bitlbee", "rename " + oldnick + " " + newnick)
        
def msg_comes_thru(sender, receiver, message):
    sender.send_priv_msg(receiver.nick, message)
    received = receiver.receive().find(message) != -1
    return received

def perform_test(clis, test_function, test_name):
    global FAILED
    for cli in clis:
        cli.tmplog=""

    print("\n"+SEPARATOR)
    print("Test: "+test_name)

    if test_function(clis):
        print("Test passed")
    else:
        print("Test failed")
        FAILED += [test_name]

        for cli in clis:
            if cli.tmplog != "":
                print(SMOLPARATOR)
                print("Test Log "+ cli.nick+":")
                print(cli.tmplog)

    print(SEPARATOR)

def yes_test(clis):
    ret = False
    for _ in range(100):
        clis[0].send_raw("yes", loud = False)
        clis[0].receive()
        if (not ret) and clis[0].log.find("Did I ask you something?"):
            ret = True
        if clis[0].log.find("Buuuuuuuuuuuuuuuurp"):
            print("The RNG gods smile upon us")
            break
    return ret

def message_test(clis):
    print("Test: Send message")
    ret = msg_comes_thru(clis[0], clis[1], 'ohai <3')
    ret = ret & msg_comes_thru(clis[1], clis[0], 'uwu *pounces*')
    return ret

def block_test(clis):
    clis[0].block_jabber_buddy(clis[1].nick)
    ret = not msg_comes_thru(clis[1], clis[0], 'm-meow?')
    clis[0].unblock_jabber_buddy(clis[1].nick)
    ret = ret & msg_comes_thru(clis[1], clis[0], '*purrs*')
    return ret

def rename_test(clis):
    newname = "xXx_pup_LINKENPARK4EVA"
    message = "rawr meanmz i luv<3 u in dinosaur"

    clis[0].rename_jabber_buddy(clis[1].nick, newname)
    clis[0].send_priv_msg(newname, message)
    ret = clis[1].receive().find(message) != -1

    clis[0].rename_jabber_buddy("-del", newname)
    ret = ret & msg_comes_thru(clis[0], clis[1], "rawr")
    return ret

def run_tests():
    global FAILED
    clis = []
    clis += [IrcClient('test1', 'asd')]
    clis += [IrcClient('test2', 'asd')]
    for cli in clis:
        cli.connect()

    if YESTEST:
        perform_test(clis, yes_test, "Yes")

    for cli in clis:
        cli.jabber_login()
    clis[0].add_jabber_buddy(clis[1].nick)

    if MESSAGETEST:
        perform_test(clis, message_test, "Send message")

    if BLOCKTEST:
        perform_test(clis, block_test, "Block user")

    if RENAMETEST:
        perform_test(clis, rename_test, "Rename user")

    if FAILED or SHOWLOG:
        print("")
        for cli in clis:
            print(SMOLPARATOR)
            print("Log "+ cli.nick+":")
            print(cli.log)
        print(SMOLPARATOR)

    if FAILED:
        print("\n" + SEPARATOR + "\nSome test have failed:")
        for fail in FAILED:
            print(fail)
    else:
        print("\n" + SEPARATOR + "\nAll tests have passed")
    
if __name__ == "__main__":
    global FAILED
    run_tests()
    if FAILED:
        sys.exit(1)
