#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define MESSAGE_LEN 1023
#define PORTNUM 2727

char *invoke(int sock, char *cmd)
{
	char buf[MESSAGE_LEN+1];
	int len;

	write(sock, cmd, strlen(cmd));
	len = recv(sock, buf, MESSAGE_LEN, 0);
	buf[len] = '\0';
	return strdup(buf);
}

int main(int argc, char *argv[])
{
	int sock;
	struct sockaddr_in dest;
	char *ptr;

	sock = socket(AF_INET, SOCK_STREAM, 0);

	memset(&dest, 0, sizeof(dest));
	dest.sin_family = AF_INET;
	dest.sin_addr.s_addr = inet_addr("127.0.0.1");
	dest.sin_port = htons(PORTNUM);

	connect(sock, (struct sockaddr *)&dest, sizeof(struct sockaddr));

	ptr = invoke(sock, "SET USER foo ISAUTHORIZED FALSE");
	printf("ptr: '%s'\n", ptr);
	close(sock);
	return(0);
}
