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

#define MAX_PORT 65535
/* two port numbers or service names up to size 20, plus a - */
#define MAX_PORT_RANGE_LEN 41

/*
 * Parse the port number in value
 *
 * Converts to the integer value if in range and the whole string is an
 * integer. Else tries to look up the value by service name.
 *
 * Returns -1 if no valid port could be determined.
 */
static int parse_port(char *value) {
	size_t len;
	char *endptr;
	long port;

	len = strlen(value);
	if (len == 0) {
		return -1;
	}

	port = strtol(value, &endptr, 10);

	/* if not whole string was a number */
	if ((size_t) (endptr - value) < len) {
		struct servent *s = getservbyname(value, NULL);
		if (s) {
			return ntohs(s->s_port);
		} else {
			return -1;
		}
	} else if (port < 0 || port > MAX_PORT) {
		return -1;
	} else {
		return (int) port;
	}
}

/*
 * Creates a listening socket and returns it in saddr_ptr.
 */
int ft_listen(struct sockaddr_storage *saddr_ptr, char *host, char *port, int copy_fd, int for_bitlbee_client,
              char **errptr)
{
	int fd, gret, saddrlen;
	struct addrinfo hints, *rp;
	socklen_t ssize = sizeof(struct sockaddr_storage);
	struct sockaddr_storage saddrs = {0}, *saddr = &saddrs;
	static char errmsg[1024];
	char *ftlisten = global.conf->ft_listen;
	char port_range[MAX_PORT_RANGE_LEN + 1] = {0};
	int port_start = 0, port_end = 0;
	int current_port, port_count, port_base_offset, port_offset;
	/* when trying a port range start at a different position each time
	 * to reduce collisions */
	static int port_search_start_offset = 0;
	char *dash_pos;

	if (errptr) {
		*errptr = errmsg;
	}

	strcpy(port, "0");

	/* Format is <IP-A>[:<Port-A>];<IP-B>[:<Port-B>] where
	 * A is for connections with the bitlbee client (DCC)
	 * and B is for connections with IM peers.
	 * Port-A and Port-B can be ranges like "3000-3010"
	 */
	if (ftlisten) {
		char *scolon = strchr(ftlisten, ';');
		char *colon;

		if (scolon) {
			if (for_bitlbee_client) {
				*scolon = '\0';
				strncpy(host, ftlisten, NI_MAXHOST);
				*scolon = ';';
			} else {
				strncpy(host, scolon + 1, NI_MAXHOST);
			}
		} else {
			strncpy(host, ftlisten, NI_MAXHOST);
		}

		if ((colon = strchr(host, ':'))) {
			*colon = '\0';
			strncpy(port_range, colon + 1, MAX_PORT_RANGE_LEN);

			/* Check if port is a range (contains '-') */
			if ((dash_pos = strchr(port_range, '-'))) {
				*dash_pos = '\0';
				port_start = parse_port(port_range);
				port_end = parse_port(dash_pos + 1);
				*dash_pos = '-';  /* restore for potential error messages */

				if (port_start < 0 || port_end < 0 || port_start > port_end) {
					sprintf(errmsg, "Invalid port range: %s", port_range);
					return -1;
				}
			} else {
				/* Single port */
				port_start = port_end = parse_port(port_range);
				if (port_start < 0) {
					sprintf(errmsg, "Invalid port: %s", port_range);
					return -1;
				}
			}
		}
	} else if (copy_fd >= 0 && getsockname(copy_fd, (struct sockaddr*) &saddrs, &ssize) == 0 &&
		(saddrs.ss_family == AF_INET || saddrs.ss_family == AF_INET6) &&
		getnameinfo((struct sockaddr*) &saddrs, ssize, host, NI_MAXHOST,
	                       NULL, 0, NI_NUMERICHOST) == 0) {
		/* We just took our local address on copy_fd, which is likely to be a
		   sensible address from which we can do a file transfer now - the
		   most sensible we can get easily. */
	} else {
		ASSERTSOCKOP(gethostname(host, NI_MAXHOST), "gethostname()");
	}

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_NUMERICSERV;

	/* Try each port in the range
	 * Start from a different offset each time to reduce collisions. */
	port_count = port_end - port_start + 1;
	port_base_offset = port_search_start_offset % port_count;
	/* Next time, start from next port along */
	port_search_start_offset = (port_search_start_offset + 1) % (MAX_PORT + 1);
	for (port_offset = 0; port_offset < port_count; port_offset++) {
		current_port = port_start + (port_base_offset + port_offset) % port_count;
		g_snprintf(port, 6, "%d", current_port);

		if (port_offset == 0) {
			if ((gret = getaddrinfo(host, port, &hints, &rp)) != 0) {
				g_snprintf(errmsg, sizeof(errmsg), "getaddrinfo() failed: %s", gai_strerror(gret));
				return -1;
			}
			saddrlen = rp->ai_addrlen;
			memcpy(saddr, rp->ai_addr, saddrlen);
			freeaddrinfo(rp);
		} else {
			// re-use previous address in saddr with new port
			if (saddr->ss_family == AF_INET) {
				((struct sockaddr_in *) saddr)->sin_port = htons(current_port);
			} else {
				((struct sockaddr_in6 *) saddr)->sin6_port = htons(current_port);
			}
		}

		if ((fd = socket(saddr->ss_family, SOCK_STREAM, 0)) == -1) {
			/* trying a different port won't help */
			g_snprintf(errmsg, sizeof(errmsg), "Opening socket: %s", strerror(errno));
			return -1;
		}

		if (bind(fd, (struct sockaddr *) saddr, saddrlen) == -1) {
			close(fd);
			if (port_offset == port_count - 1) {
				g_snprintf(errmsg, sizeof(errmsg), "Binding socket: %s. Tried port(s) %s", strerror(errno), port_range);
				return -1;
			}
			continue;
		}

		if (listen(fd, 1) == -1) {
			close(fd);
			if (port_offset == port_count - 1) {
				g_snprintf(errmsg, sizeof(errmsg), "Making socket listen: %s. Tried port(s) %s", strerror(errno), port_range);
				return -1;
			}
			continue;
		}

		/* Success! Break out of the loop */
		break;
	}

	if (!inet_ntop(saddr->ss_family, saddr->ss_family == AF_INET ?
	               ( void * ) &(( struct sockaddr_in * ) saddr)->sin_addr.s_addr :
	               ( void * ) &(( struct sockaddr_in6 * ) saddr)->sin6_addr.s6_addr,
	               host, NI_MAXHOST)) {
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
	host[NI_MAXHOST - 1] = '\0';
	port[5] = '\0';

	return fd;
}
