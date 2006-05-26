#include <errno.h>
#include <fcntl.h>

/* To cut down on the ifdef stuff a little bit in other places */
#ifdef IPV6
#define AF_INETx AF_INET6
#else
#define AF_INETx AF_INET
#endif

#ifndef _WIN32
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#define sock_make_nonblocking(fd) fcntl(fd, F_SETFL, O_NONBLOCK)
#define sock_make_blocking(fd) fcntl(fd, F_SETFL, 0)
#define sockerr_again() (errno == EINPROGRESS || errno == EINTR)
#define closesocket(a) close(a)
#else
# include <winsock2.h>
# ifdef IPV6
#  include <ws2tcpip.h>
# endif
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
