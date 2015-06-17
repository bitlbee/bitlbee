#include <errno.h>
#include <fcntl.h>

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#define sock_make_nonblocking(fd) fcntl(fd, F_SETFL, O_NONBLOCK)
#define sock_make_blocking(fd) fcntl(fd, F_SETFL, 0)
#define sockerr_again() (errno == EINPROGRESS || errno == EINTR || errno == EAGAIN)
void closesocket(int fd);
