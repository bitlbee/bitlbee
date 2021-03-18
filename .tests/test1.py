import socket
import sys
import time
import select

YESTEST = True
ADDBUDDYTEST = True
MESSAGETEST = True
BLOCKTEST = False
OFFLINETEST = True
RENAMETEST = True
STATUSTEST = True
DEFAULTTARGETTEST = True
HELPTEST = True
SHOWLOG = False
SHOWTESTLOG = True

FUN = [
"Did I ask you something?",
"Oh yeah, that's right.",
"Alright, alright. Now go back to work.",
"Buuuuuuuuuuuuuuuurp... Excuse me!",
"Yes?",
"No?",
]

SEPARATOR = "="*60
SMOLPARATOR = "-"*60

class IrcClient:
    def __init__(self, nick, pwd):
        self.nick = nick
        self.pwd = pwd
        self.log = ''
        self.tmplog = ''
        self.sck = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

    def send_raw(self, msg, loud = False, log = True):
        self.receive()
        if loud:
            print('FROM '+ self.nick + '|| ' + msg)
        if log:
            self.log += msg+'\r\n'
            self.tmplog += msg+'\r\n'
        self.sck.send((msg+'\r\n').encode())

    def send_priv_msg(self, recip, msg, loud = False):
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

def perform_test(failed, clis, test_function, test_name):
    fail = False
    for cli in clis:
        cli.tmplog=""

    print("\n"+SEPARATOR)
    print("Test: "+test_name)

    if test_function(clis):
        print("Test passed")
    else:
        print("Test failed")
        failed += [test_name]
        fail = True
    for cli in clis:
        cli.receive()

    if fail or SHOWTESTLOG:
        for cli in clis:
            if cli.tmplog != "":
                print(SMOLPARATOR)
                print("Test Log "+ cli.nick+":")
                print(cli.tmplog)
    print(SEPARATOR)

def yes_test(clis):
    ret = False
    clis[0].send_priv_msg("&bitlbee", "yes")
    clis[0].receive()
    for x, fun in enumerate(FUN):
        if (clis[0].log.find(fun) != -1):
            ret = True
            if x:
                print("The RNG gods smile upon us")
            break
    return ret

def add_buddy_test(clis):
    clis[0].add_jabber_buddy(clis[1].nick)
    clis[1].add_jabber_buddy(clis[0].nick)

    clis[0].send_priv_msg("&bitlbee", "yes")
    clis[1].send_priv_msg("&bitlbee", "yes")

    clis[0].send_priv_msg("&bitlbee", "blist")
    junk = clis[0].receive()
    ret = junk.find(clis[1].nick) != -1
    ret = ret & (junk.find("1 available") != -1)

    clis[0].send_priv_msg("&bitlbee", "remove " +clis[1].nick)
    clis[0].send_priv_msg("&bitlbee", "blist")
    ret = ret & (clis[0].receive().find(clis[1].nick) == -1)

    clis[0].add_jabber_buddy(clis[1].nick)
    clis[1].send_priv_msg("&bitlbee", "yes")
    clis[0].send_priv_msg("&bitlbee", "blist")
    junk = clis[0].receive()
    ret = ret & (junk.find("1 available") != -1)
    ret = ret & (junk.find(clis[1].nick) != -1)

def message_test(clis):
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

def status_test(clis):
    status = "get out of my room mom"
    clis[1].send_priv_msg("&bitlbee", "set status '"+status+"'")
    clis[0].send_priv_msg("&bitlbee", "info "+clis[1].nick)
    ret = (clis[0].receive().find("jabber - Status message: "+status) != -1)

    clis[1].send_priv_msg("&bitlbee", "set")
    ret = ret & (clis[1].receive().find(status) != -1)

    clis[1].send_priv_msg("&bitlbee", "set -del status")
    clis[0].send_priv_msg("&bitlbee", "info "+clis[1].nick)
    ret = ret & (clis[0].receive().find("jabber - Status message: (none)") != -1)
    return ret

def offline_test(clis):
    clis[0].send_priv_msg("&bitlbee", "account off")

    junk = clis[0].receive()
    ret = (junk.find(clis[1].nick) != -1)
    ret = ret & (junk.find("QUIT") != -1)

    junk = clis[1].receive()
    ret = ret & (junk.find(clis[0].nick) != -1)
    ret = ret & (junk.find("QUIT") != -1)

    clis[0].send_priv_msg(clis[1].nick, "i'm not ur mom")
    ret = ret & (clis[0].receive().find("No such nick/channel") != -1)

    clis[0].send_priv_msg("&bitlbee", "account on")

    junk = clis[0].receive()
    ret = ret & (junk.find(clis[1].nick) != -1)
    ret = ret & (junk.find("JOIN") != -1)

    junk = clis[1].receive()
    ret = ret & (junk.find(clis[0].nick) != -1)
    ret = ret & (junk.find("JOIN") != -1)

    return ret

def default_target_test(clis):
    clis[0].send_priv_msg("&bitlbee", "set default_target last")
    clis[0].send_priv_msg("&bitlbee", "test2: ur mah default now")
    
    ret = (clis[1].receive().find("ur mah default now") != -1)

    clis[0].send_priv_msg("&bitlbee", "mrow")
    ret = ret & (clis[1].receive().find("mrow") != -1)

    clis[0].send_priv_msg("root", "set default_target root")
    junk = clis[0].receive()
    ret = ret & (junk.find("default_target") != -1)
    ret = ret & (junk.find("root") != -1)

    clis[0].send_priv_msg("&bitlbee", "yes")
    ret = ret & (clis[1].receive().find("yes") == -1)
    return ret 

def help_test(clis):
    clis[0].send_priv_msg("&bitlbee", "help")
    ret = (clis[0].receive().find("identify_methods") != -1)
    clis[0].send_priv_msg("&bitlbee", "help commands")
    ret = ret & (clis[0].receive().find("qlist") != -1)
    return ret
    

def run_tests(failed):
    clis = []
    clis += [IrcClient('test1', 'asd')]
    clis += [IrcClient('test2', 'asd')]
    for cli in clis:
        cli.connect()

    if YESTEST:
        perform_test(failed, clis, yes_test, "Yes")

    print("")
    for cli in clis:
        cli.jabber_login()

    if ADDBUDDYTEST:
        perform_test(failed, clis, add_buddy_test, "Add/remove buddy")


    if MESSAGETEST:
        perform_test(failed, clis, message_test, "Send message")

    if BLOCKTEST:
        perform_test(failed, clis, block_test, "Block user")

    if RENAMETEST:
        perform_test(failed, clis, rename_test, "Rename user")

    if STATUSTEST:
        perform_test(failed, clis, status_test, "Change status")

    if OFFLINETEST:
        perform_test(failed, clis, offline_test, "Go offline")

    if DEFAULTTARGETTEST:
        perform_test(failed, clis, default_target_test, "Change default target")

    if HELPTEST:
        perform_test(failed, clis, help_test, "Ask for help")

    if failed or SHOWLOG:
        print("")
        for cli in clis:
            print(SMOLPARATOR)
            print("Log "+ cli.nick+":")
            print(cli.log)
        print(SMOLPARATOR)

    if failed:
        print("\n" + SEPARATOR + "\nSome test have failed:")
        for fail in failed:
            print(fail)
    else:
        print("\n" + SEPARATOR + "\nAll tests have passed")
    
if __name__ == "__main__":
    failed = []
    run_tests(failed)
    if failed:
        sys.exit(1)
