import socket, sys, time, select

class ircClient:
    def __init__(self, nick, pw):
        self.nick = nick
        self.pw = pw
        self.log = ''
        self.sck = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

    def sendRaw(self, msg):
        self.receive()
        self.log += msg+'\r\n'
        self.sck.send((msg+'\r\n').encode())

    def sendPrivMsg(self, recip, msg):
        self.sendRaw('PRIVMSG '+recip+' :'+msg)

    def connect(self):
        try:
            self.sck.connect(('127.0.0.1', 6667))
        except:
            print("Connection failed")
            sys.exit(1)

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
            print("Jabber Login failed")
            sys.exit(1)

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

def runTests():
    clis = []
    clis += [ircClient('test1', 'asd')]
    clis += [ircClient('test2', 'asd')]
    for cli in clis:
        cli.connect()
        cli.jabberLogin()
    a, b = clis[0], clis[1]

    a.sendPrivMsg(b.nick, 'ohai qtie')
    a.receive()
    b.receive()
    if b.log.find('ohai qtie') == -1:
        print('Message not coming through')
        print('Sender Log:' + a.log)
        print('Receiver Log:' + b.log)
        sys.exit(1)

if __name__ == "__main__":
    runTests()
