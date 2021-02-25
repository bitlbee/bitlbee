import socket, sys, time, select

class ircClient:
    def __init__(self, nick, pw):
        self.nick = nick
        self.pw = pw
        self.log = ''
        self.sck = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

    def sendRaw(self, msg, loud = True):
        self.receive()
        if loud:
            print('FROM '+ self.nick + '|| ' + msg)
        self.log += msg+'\r\n'
        self.sck.send((msg+'\r\n').encode())

    def sendPrivMsg(self, recip, msg, loud = True):
        self.sendRaw('PRIVMSG '+recip+' :'+msg, loud)

    def connect(self):
        try:
            self.sck.connect(('127.0.0.1', 6667))
        except:
            print("IRC connection failed for " + self.nick)
            sys.exit(1)
        
        print("IRC connection established for " + self.nick)

        self.sendRaw('USER ' + (self.nick + " ")*3)
        self.sendRaw('NICK ' + self.nick)
        self.sendRaw('JOIN &bitlbee')

    def jabberLogin(self):
        self.sendPrivMsg("&bitlbee", "account add jabber "+self.nick+"@localhost "+self.pw)
        time.sleep(0.3)
        self.sendPrivMsg("&bitlbee", "account on")
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
                        self.sendRaw('PONG ' + line.split()[1])
            else:
                break
        self.log += text
        return text

    def addJabberBuddy(self, nick):
        self.sendPrivMsg("&bitlbee", "add 0 " + nick+"@localhost")
    
    def blockJabberBuddy(self, nick):
        self.sendPrivMsg("&bitlbee", "block " + nick)

    def unblockJabberBuddy(self, nick):
        self.sendPrivMsg("&bitlbee", "allow " + nick)

    def renameJabberBuddy(self, oldnick, newnick)
        self.sendPrivMsg("&bitlbee", "rename " + oldnick + " " + newnick)
        
def testSendMessage(sender, receiver, message, shouldreceive = True):
    sender.sendPrivMsg(b.nick, message)
    msginhere = receiver.receive()
    received = !(msginhere.find(message) == -1)
    if shouldreceive ^ received:
        print('Test failed: Message from ' + sender.nick + ' to ' + receiver.nick)
        print('Sender Log:' + a.log)
        print('Receiver Log:' + b.log)
        sys.exit(1)
    else:

def runTests():
    clis = []
    clis += [ircClient('test1', 'asd')]
    clis += [ircClient('test2', 'asd')]
    for cli in clis:
        cli.connect()
        cli.jabberLogin()

    clis[0].addJabberBuddy(clis[1].nick)

    print("Test: Send message")
    testSendMessage(clis[0],clis[1]),'ohai <3')
    testSendMessage(clis[1],clis[0]),'uwu *pounces*')
    print("Test passed")

    print("Test: Block/Unblock")
    clis[0].blockJabberBuddy(clis[1].nick)
    testSendMessage(clis[1],clis[0]),'m-meow?', shouldreceive = False)
    clis[0].unblockJabberBuddy(clis[1].nick)
    testSendMessage(clis[1],clis[0]),'*purrs*')
    print("Test passed")
    
if __name__ == "__main__":
    runTests()
