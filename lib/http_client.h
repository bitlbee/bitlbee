/********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2012 Wilmer van der Gaast and others                *
  \********************************************************************/

/* HTTP(S) module                                                       */

/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License with
  the Debian GNU/Linux distribution in /usr/share/common-licenses/GPL;
  if not, write to the Free Software Foundation, Inc., 51 Franklin St.,
  Fifth Floor, Boston, MA  02110-1301  USA
*/

/* http_client allows you to talk (asynchronously, again) to HTTP servers.
   In the "background" it will send the whole query and wait for a complete
   response to come back. Initially written for MS Passport authentication,
   but used for many other things now like OAuth and Twitter.

   It's very useful for doing quick requests without blocking the whole
   program. Unfortunately it doesn't support fancy stuff like HTTP keep-
   alives. */

#include <glib.h>
#include "ssl_client.h"

struct http_request;

typedef enum http_client_flags {
	HTTPC_STREAMING = 1,
	HTTPC_EOF = 2,
	HTTPC_CHUNKED = 4,

	/* Let's reserve 0x1000000+ for lib users. */
} http_client_flags_t;

/* Your callback function should look like this: */
typedef void (*http_input_function)(struct http_request *);

/* This structure will be filled in by the http_dorequest* functions, and
   it will be passed to the callback function. Use the data field to add
   your own data. */
struct http_request {
	char *request;          /* The request to send to the server. */
	int request_length;     /* Its size. */
	short status_code;      /* The numeric HTTP status code. (Or -1
	                           if something really went wrong) */
	char *status_string;    /* The error text. */
	char *reply_headers;
	char *reply_body;
	int body_size;          /* The number of bytes in reply_body. */
	short redir_ttl;        /* You can set it to 0 if you don't want
	                           http_client to follow them. */

	http_client_flags_t flags;

	http_input_function func;
	gpointer data;

	/* Please don't touch the things down here, you shouldn't need them. */
	void *ssl;
	int fd;

	int inpa;
	int bytes_written;
	int bytes_read;
	int content_length;     /* "Content-Length:" header or -1 */

	/* Used in streaming mode. Caller should read from reply_body. */
	char *sbuf;
	size_t sblen;

	/* Chunked encoding only. Raw chunked stream is decoded from here. */
	char *cbuf;
	size_t cblen;
};

/* The _url variant is probably more useful than the raw version. The raw
   version is probably only useful if you want to do POST requests or if
   you want to add some extra headers. As you can see, HTTPS connections
   are also supported (using ssl_client). */
struct http_request *http_dorequest(char *host, int port, int ssl, char *request, http_input_function func,
                                    gpointer data);
struct http_request *http_dorequest_url(char *url_string, http_input_function func, gpointer data);

/* For streaming connections only; flushes len bytes at the start of the buffer. */
void http_flush_bytes(struct http_request *req, size_t len);
void http_close(struct http_request *req);
