import socket, sys

client = socket.socket ( socket.AF_INET, socket.SOCK_STREAM )
client.connect ( ( 'localhost', 2727 ) )

client.send(sys.argv[1])
print client.recv(1024)
