#ifndef _WIN32
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#define sock_make_nonblocking(fd) fcntl(fd, F_SETFL, O_NONBLOCK)
#define sockerr_again() (errno == EINPROGRESS || errno == EINTR)
#define closesocket(a) close(a)
#else
# include <winsock2.h>
# ifndef _MSC_VER
#  include <ws2tcpip.h>
# endif
# if !defined(BITLBEE_CORE) && defined(_MSC_VER)
#   pragma comment(lib,"bitlbee.lib")
# endif
# include <io.h>
# define read(a,b,c) recv(a,b,c,0)
# define write(a,b,c) send(a,b,c,0)
# define umask _umask
# define mode_t int
# define sock_make_nonblocking(fd) { int non_block = 1; ioctlsocket(fd, FIONBIO, &non_block); }
# define sockerr_again() (WSAGetLastError() == WSAEINTR || WSAGetLastError() == WSAEINPROGRESS || WSAGetLastError() == WSAEWOULDBLOCK)
# define ETIMEDOUT WSAETIMEDOUT
# define sleep(a) Sleep(a*1000)
#endif
