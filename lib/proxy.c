/*
 * gaim
 *
 * Copyright (C) 1998-1999, Mark Spencer <markster@marko.net>
 * Copyright (C) 2002-2004, Wilmer van der Gaast, Jelmer Vernooij
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#define BITLBEE_CORE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "nogaim.h"
#include "proxy.h"
#include "base64.h"

char proxyhost[128] = "";
int proxyport = 0;
int proxytype = PROXY_NONE;
char proxyuser[128] = "";
char proxypass[128] = "";

/* Some systems don't know this one. It's not essential, so set it to 0 then. */
#ifndef AI_NUMERICSERV
#define AI_NUMERICSERV 0
#endif
#ifndef AI_ADDRCONFIG
#define AI_ADDRCONFIG 0
#endif

struct PHB {
	b_event_handler func, proxy_func;
	gpointer data, proxy_data;
	char *host;
	int port;
	int fd;
	gint inpa;
	struct addrinfo *gai, *gai_cur;
};

static int proxy_connect_none(const char *host, unsigned short port_, struct PHB *phb);

static gboolean phb_close(struct PHB *phb)
{
	close(phb->fd);
	phb->func(phb->data, -1, B_EV_IO_READ);
	g_free(phb->host);
	g_free(phb);
	return FALSE;
}

static gboolean proxy_connected(gpointer data, gint source, b_input_condition cond)
{
	struct PHB *phb = data;
	socklen_t len;
	int error = ETIMEDOUT;

	len = sizeof(error);

	if (getsockopt(source, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error) {
		if ((phb->gai_cur = phb->gai_cur->ai_next)) {
			int new_fd;
			b_event_remove(phb->inpa);
			if ((new_fd = proxy_connect_none(NULL, 0, phb))) {
				b_event_remove(phb->inpa);
				closesocket(source);
				dup2(new_fd, source);
				closesocket(new_fd);
				phb->inpa = b_input_add(source, B_EV_IO_WRITE, proxy_connected, phb);
				return FALSE;
			}
		}
		closesocket(source);
		source = -1;
		/* socket is dead, but continue to clean up */
	} else {
		sock_make_blocking(source);
	}

	freeaddrinfo(phb->gai);
	b_event_remove(phb->inpa);
	phb->inpa = 0;
	if (phb->proxy_func) {
		phb->proxy_func(phb->proxy_data, source, B_EV_IO_READ);
	} else {
		phb->func(phb->data, source, B_EV_IO_READ);
		g_free(phb);
	}

	return FALSE;
}

static int proxy_connect_none(const char *host, unsigned short port_, struct PHB *phb)
{
	struct sockaddr_in me;
	int fd = -1;

	if (phb->gai_cur == NULL) {
		int ret;
		char port[6];
		struct addrinfo hints;

		g_snprintf(port, sizeof(port), "%d", port_);

		memset(&hints, 0, sizeof(struct addrinfo));
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_flags = AI_ADDRCONFIG | AI_NUMERICSERV;

		if (!(ret = getaddrinfo(host, port, &hints, &phb->gai))) {
			phb->gai_cur = phb->gai;
		} else {
			event_debug("gai(): %s\n", gai_strerror(ret));
		}
	}

	for (; phb->gai_cur; phb->gai_cur = phb->gai_cur->ai_next) {
		if ((fd = socket(phb->gai_cur->ai_family, phb->gai_cur->ai_socktype, phb->gai_cur->ai_protocol)) < 0) {
			event_debug("socket failed: %d\n", errno);
			continue;
		}

		sock_make_nonblocking(fd);

		if (global.conf->iface_out) {
			me.sin_family = AF_INET;
			me.sin_port = 0;
			me.sin_addr.s_addr = inet_addr(global.conf->iface_out);

			if (bind(fd, (struct sockaddr *) &me, sizeof(me)) != 0) {
				event_debug("bind( %d, \"%s\" ) failure\n", fd, global.conf->iface_out);
			}
		}

		event_debug("proxy_connect_none( \"%s\", %d ) = %d\n", host, port_, fd);

		if (connect(fd, phb->gai_cur->ai_addr, phb->gai_cur->ai_addrlen) < 0 && !sockerr_again()) {
			event_debug("connect failed: %s\n", strerror(errno));
			closesocket(fd);
			fd = -1;
			continue;
		} else {
			phb->inpa = b_input_add(fd, B_EV_IO_WRITE, proxy_connected, phb);
			phb->fd = fd;

			break;
		}
	}

	if (fd < 0 && host) {
		g_free(phb);
	}

	return fd;
}


/* Connecting to HTTP proxies */

#define HTTP_GOODSTRING "HTTP/1.0 200"
#define HTTP_GOODSTRING2 "HTTP/1.1 200"

static gboolean http_canread(gpointer data, gint source, b_input_condition cond)
{
	int nlc = 0;
	int pos = 0;
	struct PHB *phb = data;
	char inputline[8192];

	b_event_remove(phb->inpa);

	while ((pos < sizeof(inputline) - 1) && (nlc != 2) && (read(source, &inputline[pos++], 1) == 1)) {
		if (inputline[pos - 1] == '\n') {
			nlc++;
		} else if (inputline[pos - 1] != '\r') {
			nlc = 0;
		}
	}
	inputline[pos] = '\0';

	if ((memcmp(HTTP_GOODSTRING, inputline, strlen(HTTP_GOODSTRING)) == 0) ||
	    (memcmp(HTTP_GOODSTRING2, inputline, strlen(HTTP_GOODSTRING2)) == 0)) {
		phb->func(phb->data, source, B_EV_IO_READ);
		g_free(phb->host);
		g_free(phb);
		return FALSE;
	}

	return phb_close(phb);
}

static gboolean http_canwrite(gpointer data, gint source, b_input_condition cond)
{
	char cmd[384];
	struct PHB *phb = data;
	socklen_t len;
	int error = ETIMEDOUT;

	if (phb->inpa > 0) {
		b_event_remove(phb->inpa);
	}
	len = sizeof(error);
	if (getsockopt(source, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
		return phb_close(phb);
	}
	sock_make_blocking(source);

	g_snprintf(cmd, sizeof(cmd), "CONNECT %s:%d HTTP/1.1\r\nHost: %s:%d\r\n", phb->host, phb->port,
	           phb->host, phb->port);
	if (send(source, cmd, strlen(cmd), 0) < 0) {
		return phb_close(phb);
	}

	if (strlen(proxyuser) > 0) {
		char *t1, *t2;
		t1 = g_strdup_printf("%s:%s", proxyuser, proxypass);
		t2 = tobase64(t1);
		g_free(t1);
		g_snprintf(cmd, sizeof(cmd), "Proxy-Authorization: Basic %s\r\n", t2);
		g_free(t2);
		if (send(source, cmd, strlen(cmd), 0) < 0) {
			return phb_close(phb);
		}
	}

	g_snprintf(cmd, sizeof(cmd), "\r\n");
	if (send(source, cmd, strlen(cmd), 0) < 0) {
		return phb_close(phb);
	}

	phb->inpa = b_input_add(source, B_EV_IO_READ, http_canread, phb);

	return FALSE;
}

static int proxy_connect_http(const char *host, unsigned short port, struct PHB *phb)
{
	phb->host = g_strdup(host);
	phb->port = port;
	phb->proxy_func = http_canwrite;
	phb->proxy_data = phb;

	return(proxy_connect_none(proxyhost, proxyport, phb));
}


/* Connecting to SOCKS4 proxies */

static gboolean s4_canread(gpointer data, gint source, b_input_condition cond)
{
	unsigned char packet[12];
	struct PHB *phb = data;

	b_event_remove(phb->inpa);

	memset(packet, 0, sizeof(packet));
	if (read(source, packet, 9) >= 4 && packet[1] == 90) {
		phb->func(phb->data, source, B_EV_IO_READ);
		g_free(phb->host);
		g_free(phb);
		return FALSE;
	}

	return phb_close(phb);
}

static gboolean s4_canwrite(gpointer data, gint source, b_input_condition cond)
{
	unsigned char packet[12];
	struct hostent *hp;
	struct PHB *phb = data;
	socklen_t len;
	int error = ETIMEDOUT;

	if (phb->inpa > 0) {
		b_event_remove(phb->inpa);
	}
	len = sizeof(error);
	if (getsockopt(source, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
		return phb_close(phb);
	}
	sock_make_blocking(source);

	/* XXX does socks4 not support host name lookups by the proxy? */
	if (!(hp = gethostbyname(phb->host))) {
		return phb_close(phb);
	}

	packet[0] = 4;
	packet[1] = 1;
	packet[2] = phb->port >> 8;
	packet[3] = phb->port & 0xff;
	packet[4] = (unsigned char) (hp->h_addr_list[0])[0];
	packet[5] = (unsigned char) (hp->h_addr_list[0])[1];
	packet[6] = (unsigned char) (hp->h_addr_list[0])[2];
	packet[7] = (unsigned char) (hp->h_addr_list[0])[3];
	packet[8] = 0;
	if (write(source, packet, 9) != 9) {
		return phb_close(phb);
	}

	phb->inpa = b_input_add(source, B_EV_IO_READ, s4_canread, phb);

	return FALSE;
}

static int proxy_connect_socks4(const char *host, unsigned short port, struct PHB *phb)
{
	phb->host = g_strdup(host);
	phb->port = port;
	phb->proxy_func = s4_canwrite;
	phb->proxy_data = phb;

	return(proxy_connect_none(proxyhost, proxyport, phb));
}


/* Connecting to SOCKS5 proxies */

static gboolean s5_canread_again(gpointer data, gint source, b_input_condition cond)
{
	unsigned char buf[512];
	struct PHB *phb = data;

	b_event_remove(phb->inpa);

	if (read(source, buf, 10) < 10) {
		return phb_close(phb);
	}
	if ((buf[0] != 0x05) || (buf[1] != 0x00)) {
		return phb_close(phb);
	}

	phb->func(phb->data, source, B_EV_IO_READ);
	g_free(phb->host);
	g_free(phb);

	return FALSE;
}

static void s5_sendconnect(gpointer data, gint source)
{
	unsigned char buf[512];
	struct PHB *phb = data;
	int hlen = strlen(phb->host);

	buf[0] = 0x05;
	buf[1] = 0x01;          /* CONNECT */
	buf[2] = 0x00;          /* reserved */
	buf[3] = 0x03;          /* address type -- host name */
	buf[4] = hlen;
	memcpy(buf + 5, phb->host, hlen);
	buf[5 + strlen(phb->host)] = phb->port >> 8;
	buf[5 + strlen(phb->host) + 1] = phb->port & 0xff;

	if (write(source, buf, (5 + strlen(phb->host) + 2)) < (5 + strlen(phb->host) + 2)) {
		phb_close(phb);
		return;
	}

	phb->inpa = b_input_add(source, B_EV_IO_READ, s5_canread_again, phb);
}

static gboolean s5_readauth(gpointer data, gint source, b_input_condition cond)
{
	unsigned char buf[512];
	struct PHB *phb = data;

	b_event_remove(phb->inpa);

	if (read(source, buf, 2) < 2) {
		return phb_close(phb);
	}

	if ((buf[0] != 0x01) || (buf[1] != 0x00)) {
		return phb_close(phb);
	}

	s5_sendconnect(phb, source);

	return FALSE;
}

static gboolean s5_canread(gpointer data, gint source, b_input_condition cond)
{
	unsigned char buf[512];
	struct PHB *phb = data;

	b_event_remove(phb->inpa);

	if (read(source, buf, 2) < 2) {
		return phb_close(phb);
	}

	if ((buf[0] != 0x05) || (buf[1] == 0xff)) {
		return phb_close(phb);
	}

	if (buf[1] == 0x02) {
		unsigned int i = strlen(proxyuser), j = strlen(proxypass);
		buf[0] = 0x01;  /* version 1 */
		buf[1] = i;
		memcpy(buf + 2, proxyuser, i);
		buf[2 + i] = j;
		memcpy(buf + 2 + i + 1, proxypass, j);
		if (write(source, buf, 3 + i + j) < 3 + i + j) {
			return phb_close(phb);
		}

		phb->inpa = b_input_add(source, B_EV_IO_READ, s5_readauth, phb);
	} else {
		s5_sendconnect(phb, source);
	}

	return FALSE;
}

static gboolean s5_canwrite(gpointer data, gint source, b_input_condition cond)
{
	unsigned char buf[512];
	int i;
	struct PHB *phb = data;
	socklen_t len;
	int error = ETIMEDOUT;

	if (phb->inpa > 0) {
		b_event_remove(phb->inpa);
	}
	len = sizeof(error);
	if (getsockopt(source, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
		return phb_close(phb);
	}
	sock_make_blocking(source);

	i = 0;
	buf[0] = 0x05;          /* SOCKS version 5 */
	if (proxyuser[0]) {
		buf[1] = 0x02;  /* two methods */
		buf[2] = 0x00;  /* no authentication */
		buf[3] = 0x02;  /* username/password authentication */
		i = 4;
	} else {
		buf[1] = 0x01;
		buf[2] = 0x00;
		i = 3;
	}

	if (write(source, buf, i) < i) {
		return phb_close(phb);
	}

	phb->inpa = b_input_add(source, B_EV_IO_READ, s5_canread, phb);

	return FALSE;
}

static int proxy_connect_socks5(const char *host, unsigned short port, struct PHB *phb)
{
	phb->host = g_strdup(host);
	phb->port = port;
	phb->proxy_func = s5_canwrite;
	phb->proxy_data = phb;

	return(proxy_connect_none(proxyhost, proxyport, phb));
}


/* Export functions */

int proxy_connect(const char *host, int port, b_event_handler func, gpointer data)
{
	struct PHB *phb;

	if (!host || port <= 0 || !func || strlen(host) > 128) {
		return -1;
	}

	phb = g_new0(struct PHB, 1);
	phb->func = func;
	phb->data = data;

	if (proxytype == PROXY_NONE || !proxyhost[0] || proxyport <= 0) {
		return proxy_connect_none(host, port, phb);
	} else if (proxytype == PROXY_HTTP) {
		return proxy_connect_http(host, port, phb);
	} else if (proxytype == PROXY_SOCKS4) {
		return proxy_connect_socks4(host, port, phb);
	} else if (proxytype == PROXY_SOCKS5) {
		return proxy_connect_socks5(host, port, phb);
	}

	g_free(phb);
	return -1;
}
