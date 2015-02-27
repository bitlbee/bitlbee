/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Utility functions for file transfer                                      *
*                                                                           *
*  Copyright 2008 Uli Meis <a.sporto+bee@gmail.com>                         *
*                                                                           *
*  This program is free software; you can redistribute it and/or modify     *
*  it under the terms of the GNU General Public License as published by     *
*  the Free Software Foundation; either version 2 of the License, or        *
*  (at your option) any later version.                                      *
*                                                                           *
*  This program is distributed in the hope that it will be useful,          *
*  but WITHOUT ANY WARRANTY; without even the implied warranty of           *
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
*  GNU General Public License for more details.                             *
*                                                                           *
*  You should have received a copy of the GNU General Public License along  *
*  with this program; if not, write to the Free Software Foundation, Inc.,  *
*  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.              *
*                                                                           *
\***************************************************************************/

#define BITLBEE_CORE
#include "bitlbee.h"
#include <poll.h>
#include <netinet/tcp.h>
#include "lib/ftutil.h"

#define ASSERTSOCKOP(op, msg) \
	if ((op) == -1) { \
		g_snprintf(errmsg, sizeof(errmsg), msg ": %s", strerror(errno)); \
		return -1; }

/*
 * Creates a listening socket and returns it in saddr_ptr.
 */
int ft_listen(struct sockaddr_storage *saddr_ptr, char *host, char *port, int copy_fd, int for_bitlbee_client,
              char **errptr)
{
	int fd, gret, saddrlen;
	struct addrinfo hints, *rp;
	socklen_t ssize = sizeof(struct sockaddr_storage);
	struct sockaddr_storage saddrs, *saddr = &saddrs;
	static char errmsg[1024];
	char *ftlisten = global.conf->ft_listen;

	if (errptr) {
		*errptr = errmsg;
	}

	strcpy(port, "0");

	/* Format is <IP-A>[:<Port-A>];<IP-B>[:<Port-B>] where
	 * A is for connections with the bitlbee client (DCC)
	 * and B is for connections with IM peers.
	 */
	if (ftlisten) {
		char *scolon = strchr(ftlisten, ';');
		char *colon;

		if (scolon) {
			if (for_bitlbee_client) {
				*scolon = '\0';
				strncpy(host, ftlisten, HOST_NAME_MAX);
				*scolon = ';';
			} else {
				strncpy(host, scolon + 1, HOST_NAME_MAX);
			}
		} else {
			strncpy(host, ftlisten, HOST_NAME_MAX);
		}

		if ((colon = strchr(host, ':'))) {
			*colon = '\0';
			strncpy(port, colon + 1, 5);
		}
	} else if (copy_fd >= 0 && getsockname(copy_fd, (struct sockaddr*) &saddrs, &ssize) == 0 &&
	           (saddrs.ss_family == AF_INET || saddrs.ss_family == AF_INET6) &&
	           getnameinfo((struct sockaddr*) &saddrs, ssize, host, HOST_NAME_MAX,
	                       NULL, 0, NI_NUMERICHOST) == 0) {
		/* We just took our local address on copy_fd, which is likely to be a
		   sensible address from which we can do a file transfer now - the
		   most sensible we can get easily. */
	} else {
		ASSERTSOCKOP(gethostname(host, HOST_NAME_MAX + 1), "gethostname()");
	}

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_NUMERICSERV;

	if ((gret = getaddrinfo(host, port, &hints, &rp)) != 0) {
		sprintf(errmsg, "getaddrinfo() failed: %s", gai_strerror(gret));
		return -1;
	}

	saddrlen = rp->ai_addrlen;

	memcpy(saddr, rp->ai_addr, saddrlen);

	freeaddrinfo(rp);

	ASSERTSOCKOP(fd = socket(saddr->ss_family, SOCK_STREAM, 0), "Opening socket");
	ASSERTSOCKOP(bind(fd, ( struct sockaddr *) saddr, saddrlen), "Binding socket");
	ASSERTSOCKOP(listen(fd, 1), "Making socket listen");

	if (!inet_ntop(saddr->ss_family, saddr->ss_family == AF_INET ?
	               ( void * ) &(( struct sockaddr_in * ) saddr)->sin_addr.s_addr :
	               ( void * ) &(( struct sockaddr_in6 * ) saddr)->sin6_addr.s6_addr,
	               host, HOST_NAME_MAX)) {
		strcpy(errmsg, "inet_ntop failed on listening socket");
		return -1;
	}

	ssize = sizeof(struct sockaddr_storage);
	ASSERTSOCKOP(getsockname(fd, ( struct sockaddr *) saddr, &ssize), "Getting socket name");

	if (saddr->ss_family == AF_INET) {
		g_snprintf(port, 6, "%d", ntohs(((struct sockaddr_in *) saddr)->sin_port));
	} else {
		g_snprintf(port, 6, "%d", ntohs(((struct sockaddr_in6 *) saddr)->sin6_port));
	}

	if (saddr_ptr) {
		memcpy(saddr_ptr, saddr, saddrlen);
	}

	/* I hate static-length strings.. */
	host[HOST_NAME_MAX - 1] = '\0';
	port[5] = '\0';

	return fd;
}
