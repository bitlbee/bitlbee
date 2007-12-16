import socket

client = socket.socket ( socket.AF_INET, socket.SOCK_STREAM )
client.connect ( ( 'localhost', 2727 ) )

client.send("GET USERSTATUS")
print client.recv(1024)
