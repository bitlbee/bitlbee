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

#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>

#if STDC_HEADERS
# include <string.h>
#else
# if !HAVE_STRCHR
#  define strchr index
#  define strrchr rindex
# endif
char *strchr (), *strrchr ();
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
# define write(a,b,c) send(a,b,c,0)
# define read(a,b,c)  recv(a,b,c,0)
# define snprintf _snprintf
#endif

#ifdef USE_STRUCT_CALLBACKS
extern struct yahoo_callbacks *yc;
#define YAHOO_CALLBACK(x)	yc->x
#else
#define YAHOO_CALLBACK(x)	x
#endif

extern enum yahoo_log_level log_level;

int yahoo_tcp_readline(char *ptr, int maxlen, int fd)
{
	int n, rc;
	char c;

	for (n = 1; n < maxlen; n++) {

		do {
			rc = read(fd, &c, 1);
		} while(rc == -1 && (errno == EINTR || errno == EAGAIN)); /* this is bad - it should be done asynchronously */

		if (rc == 1) {
			if(c == '\r')			/* get rid of \r */
				continue;
			*ptr = c;
			if (c == '\n')
				break;
			ptr++;
		} else if (rc == 0) {
			if (n == 1)
				return (0);		/* EOF, no data */
			else
				break;			/* EOF, w/ data */
		} else {
			return -1;
		}
	}

	*ptr = 0;
	return (n);
}

static int url_to_host_port_path(const char *url,
		char *host, int *port, char *path)
{
	char *urlcopy=NULL;
	char *slash=NULL;
	char *colon=NULL;
	
	/*
	 * http://hostname
	 * http://hostname/
	 * http://hostname/path
	 * http://hostname/path:foo
	 * http://hostname:port
	 * http://hostname:port/
	 * http://hostname:port/path
	 * http://hostname:port/path:foo
	 */

	if(strstr(url, "http://") == url) {
		urlcopy = strdup(url+7);
	} else {
		WARNING(("Weird url - unknown protocol: %s", url));
		return 0;
	}

	slash = strchr(urlcopy, '/');
	colon = strchr(urlcopy, ':');

	if(!colon || (slash && slash < colon)) {
		*port = 80;
	} else {
		*colon = 0;
		*port = atoi(colon+1);
	}

	if(!slash) {
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
	return (isalnum(c) || '-' == c || '_' == c);
}

char *yahoo_urlencode(const char *instr)
{
	int ipos=0, bpos=0;
	char *str = NULL;
	int len = strlen(instr);

	if(!(str = y_new(char, 3*len + 1) ))
		return "";

	while(instr[ipos]) {
		while(isurlchar(instr[ipos]))
			str[bpos++] = instr[ipos++];
		if(!instr[ipos])
			break;
		
		snprintf(&str[bpos], 4, "%%%.2x", instr[ipos]);
		bpos+=3;
		ipos++;
	}
	str[bpos]='\0';

	/* free extra alloc'ed mem. */
	len = strlen(str);
	str = y_renew(char, str, len+1);

	return (str);
}

char *yahoo_urldecode(const char *instr)
{
	int ipos=0, bpos=0;
	char *str = NULL;
	char entity[3]={0,0,0};
	unsigned dec;
	int len = strlen(instr);

	if(!(str = y_new(char, len+1) ))
		return "";

	while(instr[ipos]) {
		while(instr[ipos] && instr[ipos]!='%')
			if(instr[ipos]=='+') {
				str[bpos++]=' ';
				ipos++;
			} else
				str[bpos++] = instr[ipos++];
		if(!instr[ipos])
			break;
		
		if(instr[ipos+1] && instr[ipos+2]) {
			ipos++;
			entity[0]=instr[ipos++];
			entity[1]=instr[ipos++];
			sscanf(entity, "%2x", &dec);
			str[bpos++] = (char)dec;
		} else {
			str[bpos++] = instr[ipos++];
		}
	}
	str[bpos]='\0';

	/* free extra alloc'ed mem. */
	len = strlen(str);
	str = y_renew(char, str, len+1);

	return (str);
}

char *yahoo_xmldecode(const char *instr)
{
	int ipos=0, bpos=0, epos=0;
	char *str = NULL;
	char entity[4]={0,0,0,0};
	char *entitymap[5][2]={
		{"amp;",  "&"}, 
		{"quot;", "\""},
		{"lt;",   "<"}, 
		{"gt;",   "<"}, 
		{"nbsp;", " "}
	};
	unsigned dec;
	int len = strlen(instr);

	if(!(str = y_new(char, len+1) ))
		return "";

	while(instr[ipos]) {
		while(instr[ipos] && instr[ipos]!='&')
			if(instr[ipos]=='+') {
				str[bpos++]=' ';
				ipos++;
			} else
				str[bpos++] = instr[ipos++];
		if(!instr[ipos] || !instr[ipos+1])
			break;
		ipos++;

		if(instr[ipos] == '#') {
			ipos++;
			epos=0;
			while(instr[ipos] != ';')
				entity[epos++]=instr[ipos++];
			sscanf(entity, "%u", &dec);
			str[bpos++] = (char)dec;
			ipos++;
		} else {
			int i;
			for (i=0; i<5; i++) 
				if(!strncmp(instr+ipos, entitymap[i][0], 
					       strlen(entitymap[i][0]))) {
				       	str[bpos++] = entitymap[i][1][0];
					ipos += strlen(entitymap[i][0]);
					break;
				}
		}
	}
	str[bpos]='\0';

	/* free extra alloc'ed mem. */
	len = strlen(str);
	str = y_renew(char, str, len+1);

	return (str);
}

typedef void (*http_connected)(int id, int fd, int error);

struct callback_data {
	int id;
	yahoo_get_fd_callback callback;
	char *request;
	void *user_data;
};

static void connect_complete(int fd, int error, void *data)
{
	struct callback_data *ccd = data;
	if(error == 0 && fd > 0)
		write(fd, ccd->request, strlen(ccd->request));
	FREE(ccd->request);
	ccd->callback(ccd->id, fd, error, ccd->user_data);
	FREE(ccd);
}

static void yahoo_send_http_request(int id, char *host, int port, char *request, 
		yahoo_get_fd_callback callback, void *data)
{
	struct callback_data *ccd=y_new0(struct callback_data, 1);
	ccd->callback = callback;
	ccd->id = id;
	ccd->request = strdup(request);
	ccd->user_data = data;
	
	YAHOO_CALLBACK(ext_yahoo_connect_async)(id, host, port, connect_complete, ccd);
}

void yahoo_http_post(int id, const char *url, const char *cookies, long content_length,
		yahoo_get_fd_callback callback, void *data)
{
	char host[255];
	int port = 80;
	char path[255];
	char buff[1024];
	
	if(!url_to_host_port_path(url, host, &port, path))
		return;

	snprintf(buff, sizeof(buff), 
			"POST %s HTTP/1.0\r\n"
			"Content-length: %ld\r\n"
			"User-Agent: Mozilla/4.5 [en] (" PACKAGE "/" VERSION ")\r\n"
			"Host: %s:%d\r\n"
			"Cookie: %s\r\n"
			"\r\n",
			path, content_length, 
			host, port,
			cookies);

	yahoo_send_http_request(id, host, port, buff, callback, data);
}

void yahoo_http_get(int id, const char *url, const char *cookies,
		yahoo_get_fd_callback callback, void *data)
{
	char host[255];
	int port = 80;
	char path[255];
	char buff[1024];
	
	if(!url_to_host_port_path(url, host, &port, path))
		return;

	snprintf(buff, sizeof(buff), 
			"GET %s HTTP/1.0\r\n"
			"Host: %s:%d\r\n"
			"User-Agent: Mozilla/4.5 [en] (" PACKAGE "/" VERSION ")\r\n"
			"Cookie: %s\r\n"
			"\r\n",
			path, host, port, cookies);

	yahoo_send_http_request(id, host, port, buff, callback, data);
}

struct url_data {
	yahoo_get_url_handle_callback callback;
	void *user_data;
};

static void yahoo_got_url_fd(int id, int fd, int error, void *data)
{
	char *tmp=NULL;
	char buff[1024];
	unsigned long filesize=0;
	char *filename=NULL;
	int n;

	struct url_data *ud = data;

	if(error || fd < 0) {
		ud->callback(id, fd, error, filename, filesize, ud->user_data);
		FREE(ud);
		return;
	}

	while((n=yahoo_tcp_readline(buff, sizeof(buff), fd)) > 0) {
		LOG(("Read:%s:\n", buff));
		if(!strcmp(buff, ""))
			break;

		if( !strncasecmp(buff, "Content-length:", 
				strlen("Content-length:")) ) {
			tmp = strrchr(buff, ' ');
			if(tmp)
				filesize = atol(tmp);
		}

		if( !strncasecmp(buff, "Content-disposition:", 
				strlen("Content-disposition:")) ) {
			tmp = strstr(buff, "name=");
			if(tmp) {
				tmp+=strlen("name=");
				if(tmp[0] == '"') {
					char *tmp2;
					tmp++;
					tmp2 = strchr(tmp, '"');
					if(tmp2)
						*tmp2 = '\0';
				} else {
					char *tmp2;
					tmp2 = strchr(tmp, ';');
					if(!tmp2)
						tmp2 = strchr(tmp, '\r');
					if(!tmp2)
						tmp2 = strchr(tmp, '\n');
					if(tmp2)
						*tmp2 = '\0';
				}

				filename = strdup(tmp);
			}
		}
	}

	LOG(("n == %d\n", n));
	LOG(("Calling callback, filename:%s, size: %ld\n", filename, filesize));
	ud->callback(id, fd, error, filename, filesize, ud->user_data);
	FREE(ud);
	FREE(filename);
}

void yahoo_get_url_fd(int id, const char *url, const struct yahoo_data *yd,
		yahoo_get_url_handle_callback callback, void *data)
{
	char buff[1024];
	struct url_data *ud = y_new0(struct url_data, 1);
	snprintf(buff, sizeof(buff), "Y=%s; T=%s", yd->cookie_y, yd->cookie_t);
	ud->callback = callback;
	ud->user_data = data;
	yahoo_http_get(id, url, buff, yahoo_got_url_fd, ud);
}

