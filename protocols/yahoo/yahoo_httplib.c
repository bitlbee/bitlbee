/*
 * libyahoo2: yahoo_httplib.c
 *
 * Copyright (C) 2002-2004, Philip S Tellis <philip.tellis AT gmx.net>
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <stdio.h>
#include <stdlib.h>

#if STDC_HEADERS
# include <string.h>
#else
# if !HAVE_STRCHR
#  define strchr index
#  define strrchr rindex
# endif
char *strchr(), *strrchr();
# if !HAVE_MEMCPY
#  define memcpy(d, s, n) bcopy ((s), (d), (n))
#  define memmove(d, s, n) bcopy ((s), (d), (n))
# endif
#endif

#include <errno.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <ctype.h>
#include "yahoo2.h"
#include "yahoo2_callbacks.h"
#include "yahoo_httplib.h"
#include "yahoo_util.h"

#include "yahoo_debug.h"
#ifdef __MINGW32__
# include <winsock2.h>
# define snprintf _snprintf
#endif

#ifdef USE_STRUCT_CALLBACKS
extern struct yahoo_callbacks *yc;
#define YAHOO_CALLBACK(x)	yc->x
#else
#define YAHOO_CALLBACK(x)	x
#endif

extern enum yahoo_log_level log_level;

#if 0
int yahoo_tcp_readline(char *ptr, int maxlen, void *fd)
{
	int n, rc;
	char c;

	for (n = 1; n < maxlen; n++) {

		do {
			rc = YAHOO_CALLBACK(ext_yahoo_read) (fd, &c, 1);
		} while (rc == -1 && (errno == EINTR || errno == EAGAIN));	/* this is bad - it should be done asynchronously */

		if (rc == 1) {
			if (c == '\r')	/* get rid of \r */
				continue;
			*ptr = c;
			if (c == '\n')
				break;
			ptr++;
		} else if (rc == 0) {
			if (n == 1)
				return (0);	/* EOF, no data */
			else
				break;	/* EOF, w/ data */
		} else {
			return -1;
		}
	}

	*ptr = 0;
	return (n);
}
#endif

static int url_to_host_port_path(const char *url,
	char *host, int *port, char *path, int *ssl)
{
	char *urlcopy = NULL;
	char *slash = NULL;
	char *colon = NULL;

	/*
	 * http://hostname
	 * http://hostname/
	 * http://hostname/path
	 * http://hostname/path:foo
	 * http://hostname:port
	 * http://hostname:port/
	 * http://hostname:port/path
	 * http://hostname:port/path:foo
	 * and https:// variants of the above
	 */

	if (strstr(url, "http://") == url) {
		urlcopy = strdup(url + 7);
	} else if (strstr(url, "https://") == url) {
		urlcopy = strdup(url + 8);
		*ssl = 1;
	} else {
		WARNING(("Weird url - unknown protocol: %s", url));
		return 0;
	}

	slash = strchr(urlcopy, '/');
	colon = strchr(urlcopy, ':');

	if (!colon || (slash && slash < colon)) {
		if (*ssl)
			*port = 443;
		else
			*port = 80;
	} else {
		*colon = 0;
		*port = atoi(colon + 1);
	}

	if (!slash) {
		strcpy(path, "/");
	} else {
		strcpy(path, slash);
		*slash = 0;
	}

	strcpy(host, urlcopy);

	FREE(urlcopy);

	return 1;
}

static int isurlchar(unsigned char c)
{
	return (isalnum(c));
}

char *yahoo_urlencode(const char *instr)
{
	int ipos = 0, bpos = 0;
	char *str = NULL;
	int len = strlen(instr);

	if (!(str = y_new(char, 3 *len + 1)))
		 return "";

	while (instr[ipos]) {
		while (isurlchar(instr[ipos]))
			str[bpos++] = instr[ipos++];
		if (!instr[ipos])
			break;

		snprintf(&str[bpos], 4, "%%%02x", instr[ipos] & 0xff);
		bpos += 3;
		ipos++;
	}
	str[bpos] = '\0';

	/* free extra alloc'ed mem. */
	len = strlen(str);
	str = y_renew(char, str, len + 1);

	return (str);
}

#if 0
char *yahoo_urldecode(const char *instr)
{
	int ipos = 0, bpos = 0;
	char *str = NULL;
	char entity[3] = { 0, 0, 0 };
	unsigned dec;
	int len = strlen(instr);

	if (!(str = y_new(char, len + 1)))
		 return "";

	while (instr[ipos]) {
		while (instr[ipos] && instr[ipos] != '%')
			if (instr[ipos] == '+') {
				str[bpos++] = ' ';
				ipos++;
			} else
				str[bpos++] = instr[ipos++];
		if (!instr[ipos])
			break;

		if (instr[ipos + 1] && instr[ipos + 2]) {
			ipos++;
			entity[0] = instr[ipos++];
			entity[1] = instr[ipos++];
			sscanf(entity, "%2x", &dec);
			str[bpos++] = (char)dec;
		} else {
			str[bpos++] = instr[ipos++];
		}
	}
	str[bpos] = '\0';

	/* free extra alloc'ed mem. */
	len = strlen(str);
	str = y_renew(char, str, len + 1);

	return (str);
}

char *yahoo_xmldecode(const char *instr)
{
	int ipos = 0, bpos = 0, epos = 0;
	char *str = NULL;
	char entity[4] = { 0, 0, 0, 0 };
	char *entitymap[5][2] = {
		{"amp;", "&"},
		{"quot;", "\""},
		{"lt;", "<"},
		{"gt;", "<"},
		{"nbsp;", " "}
	};
	unsigned dec;
	int len = strlen(instr);

	if (!(str = y_new(char, len + 1)))
		 return "";

	while (instr[ipos]) {
		while (instr[ipos] && instr[ipos] != '&')
			if (instr[ipos] == '+') {
				str[bpos++] = ' ';
				ipos++;
			} else
				str[bpos++] = instr[ipos++];
		if (!instr[ipos] || !instr[ipos + 1])
			break;
		ipos++;

		if (instr[ipos] == '#') {
			ipos++;
			epos = 0;
			while (instr[ipos] != ';')
				entity[epos++] = instr[ipos++];
			sscanf(entity, "%u", &dec);
			str[bpos++] = (char)dec;
			ipos++;
		} else {
			int i;
			for (i = 0; i < 5; i++)
				if (!strncmp(instr + ipos, entitymap[i][0],
						strlen(entitymap[i][0]))) {
					str[bpos++] = entitymap[i][1][0];
					ipos += strlen(entitymap[i][0]);
					break;
				}
		}
	}
	str[bpos] = '\0';

	/* free extra alloc'ed mem. */
	len = strlen(str);
	str = y_renew(char, str, len + 1);

	return (str);
}
#endif

typedef void (*http_connected) (int id, void *fd, int error);

struct callback_data {
	int id;
	yahoo_get_fd_callback callback;
	char *request;
	void *user_data;
};

static void connect_complete(void *fd, int error, void *data)
{
	struct callback_data *ccd = data;
	if (error == 0)
		YAHOO_CALLBACK(ext_yahoo_write) (fd, ccd->request,
			strlen(ccd->request));
	free(ccd->request);
	ccd->callback(ccd->id, fd, error, ccd->user_data);
	FREE(ccd);
}

static void yahoo_send_http_request(int id, char *host, int port, char *request,
	yahoo_get_fd_callback callback, void *data, int use_ssl)
{
	struct callback_data *ccd = y_new0(struct callback_data, 1);
	ccd->callback = callback;
	ccd->id = id;
	ccd->request = strdup(request);
	ccd->user_data = data;

	YAHOO_CALLBACK(ext_yahoo_connect_async) (id, host, port,
		connect_complete, ccd, use_ssl);
}

void yahoo_http_post(int id, const char *url, const char *cookies,
	long content_length, yahoo_get_fd_callback callback, void *data)
{
	char host[255];
	int port = 80;
	char path[255];
	char buff[1024];
	int ssl = 0;

	if (!url_to_host_port_path(url, host, &port, path, &ssl))
		return;

	/* thanks to kopete dumpcap */
	snprintf(buff, sizeof(buff),
		"POST %s HTTP/1.1\r\n"
		"Cookie: %s\r\n"
		"User-Agent: Mozilla/5.0\r\n"
		"Host: %s\r\n"
		"Content-Length: %ld\r\n"
		"Cache-Control: no-cache\r\n"
		"\r\n", path, cookies, host, content_length);

	yahoo_send_http_request(id, host, port, buff, callback, data, ssl);
}

void yahoo_http_get(int id, const char *url, const char *cookies, int http11,
	int keepalive, yahoo_get_fd_callback callback, void *data)
{
	char host[255];
	int port = 80;
	char path[255];
	char buff[2048];
	char cookiebuff[1024];
	int ssl = 0;

	if (!url_to_host_port_path(url, host, &port, path, &ssl))
		return;

	/* Allow cases when we don't need to send a cookie */
	if (cookies)
		snprintf(cookiebuff, sizeof(cookiebuff), "Cookie: %s\r\n",
			cookies);
	else
		cookiebuff[0] = '\0';

	snprintf(buff, sizeof(buff),
		"GET %s HTTP/1.%s\r\n"
		"%sHost: %s\r\n"
		"User-Agent: Mozilla/4.5 [en] (" PACKAGE "/" VERSION ")\r\n"
		"Accept: */*\r\n"
		"%s" "\r\n", path, http11?"1":"0", cookiebuff, host,
		keepalive? "Connection: Keep-Alive\r\n":"Connection: close\r\n");

	yahoo_send_http_request(id, host, port, buff, callback, data, ssl);
}

void yahoo_http_head(int id, const char *url, const char *cookies, int len,
	char *payload, yahoo_get_fd_callback callback, void *data)
{
	char host[255];
	int port = 80;
	char path[255];
	char buff[2048];
	char cookiebuff[1024];
	int ssl = 0;

	if (!url_to_host_port_path(url, host, &port, path, &ssl))
		return;

	/* Allow cases when we don't need to send a cookie */
	if (cookies)
		snprintf(cookiebuff, sizeof(cookiebuff), "Cookie: %s\r\n",
			cookies);
	else
		cookiebuff[0] = '\0';

	snprintf(buff, sizeof(buff),
		"HEAD %s HTTP/1.0\r\n"
		"Accept: */*\r\n"
		"Host: %s:%d\r\n"
		"User-Agent: Mozilla/4.5 [en] (" PACKAGE "/" VERSION ")\r\n"
		"%s"
		"Content-Length: %d\r\n"
		"Cache-Control: no-cache\r\n"
		"\r\n%s", path, host, port, cookiebuff, len,
		payload?payload:"");

	yahoo_send_http_request(id, host, port, buff, callback, data, ssl);
}

