#include <errno.h>
#include <fcntl.h>

#ifndef _WIN32
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#define sock_make_nonblocking(fd) fcntl(fd, F_SETFL, O_NONBLOCK)
#define sock_make_blocking(fd) fcntl(fd, F_SETFL, 0)
#define sockerr_again() (errno == EINPROGRESS || errno == EINTR)
#ifndef EVENTS_LIBEVENT
#define closesocket(a) close(a)
#endif
#else
# include <winsock2.h>
# include <ws2tcpip.h>
# if !defined(BITLBEE_CORE) && defined(_MSC_VER)
#   pragma comment(lib,"bitlbee.lib")
# endif
# include <io.h>
# define sock_make_nonblocking(fd) { int non_block = 1; ioctlsocket(fd, FIONBIO, &non_block); }
# define sock_make_blocking(fd) { int non_block = 0; ioctlsocket(fd, FIONBIO, &non_block); }
# define sockerr_again() (WSAGetLastError() == WSAEINTR || WSAGetLastError() == WSAEINPROGRESS || WSAGetLastError() == WSAEWOULDBLOCK)
# define ETIMEDOUT WSAETIMEDOUT
# define sleep(a) Sleep(a*1000)
#endif
