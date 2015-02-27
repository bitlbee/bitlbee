/********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2013 Wilmer van der Gaast and others                *
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

#include <string.h>
#include <stdio.h>

#include "http_client.h"
#include "url.h"
#include "sock.h"


static gboolean http_connected(gpointer data, int source, b_input_condition cond);
static gboolean http_ssl_connected(gpointer data, int returncode, void *source, b_input_condition cond);
static gboolean http_incoming_data(gpointer data, int source, b_input_condition cond);
static void http_free(struct http_request *req);


struct http_request *http_dorequest(char *host, int port, int ssl, char *request, http_input_function func,
                                    gpointer data)
{
	struct http_request *req;
	int error = 0;

	req = g_new0(struct http_request, 1);

	if (ssl) {
		req->ssl = ssl_connect(host, port, TRUE, http_ssl_connected, req);
		if (req->ssl == NULL) {
			error = 1;
		}
	} else {
		req->fd = proxy_connect(host, port, http_connected, req);
		if (req->fd < 0) {
			error = 1;
		}
	}

	if (error) {
		http_free(req);
		return NULL;
	}

	req->func = func;
	req->data = data;
	req->request = g_strdup(request);
	req->request_length = strlen(request);
	req->redir_ttl = 3;
	req->content_length = -1;

	if (getenv("BITLBEE_DEBUG")) {
		printf("About to send HTTP request:\n%s\n", req->request);
	}

	return req;
}

struct http_request *http_dorequest_url(char *url_string, http_input_function func, gpointer data)
{
	url_t *url = g_new0(url_t, 1);
	char *request;
	void *ret;

	if (!url_set(url, url_string)) {
		g_free(url);
		return NULL;
	}

	if (url->proto != PROTO_HTTP && url->proto != PROTO_HTTPS) {
		g_free(url);
		return NULL;
	}

	request = g_strdup_printf("GET %s HTTP/1.0\r\n"
	                          "Host: %s\r\n"
	                          "User-Agent: BitlBee " BITLBEE_VERSION " " ARCH "/" CPU "\r\n"
	                          "\r\n", url->file, url->host);

	ret = http_dorequest(url->host, url->port,
	                     url->proto == PROTO_HTTPS, request, func, data);

	g_free(url);
	g_free(request);
	return ret;
}

/* This one is actually pretty simple... Might get more calls if we can't write
   the whole request at once. */
static gboolean http_connected(gpointer data, int source, b_input_condition cond)
{
	struct http_request *req = data;
	int st;

	if (source < 0) {
		goto error;
	}

	if (req->inpa > 0) {
		b_event_remove(req->inpa);
	}

	sock_make_nonblocking(req->fd);

	if (req->ssl) {
		st = ssl_write(req->ssl, req->request + req->bytes_written,
		               req->request_length - req->bytes_written);
		if (st < 0) {
			if (ssl_errno != SSL_AGAIN) {
				ssl_disconnect(req->ssl);
				goto error;
			}
		}
	} else {
		st = write(source, req->request + req->bytes_written,
		           req->request_length - req->bytes_written);
		if (st < 0) {
			if (!sockerr_again()) {
				closesocket(req->fd);
				goto error;
			}
		}
	}

	if (st > 0) {
		req->bytes_written += st;
	}

	if (req->bytes_written < req->request_length) {
		req->inpa = b_input_add(source,
		                        req->ssl ? ssl_getdirection(req->ssl) : B_EV_IO_WRITE,
		                        http_connected, req);
	} else {
		req->inpa = b_input_add(source, B_EV_IO_READ, http_incoming_data, req);
	}

	return FALSE;

error:
	if (req->status_string == NULL) {
		req->status_string = g_strdup("Error while writing HTTP request");
	}

	req->func(req);
	http_free(req);
	return FALSE;
}

static gboolean http_ssl_connected(gpointer data, int returncode, void *source, b_input_condition cond)
{
	struct http_request *req = data;

	if (source == NULL) {
		if (returncode != 0) {
			char *err = ssl_verify_strerror(returncode);
			req->status_string = g_strdup_printf(
			        "Certificate verification problem 0x%x: %s",
			        returncode, err ? err : "Unknown");
			g_free(err);
		}
		return http_connected(data, -1, cond);
	}

	req->fd = ssl_getfd(source);

	return http_connected(data, req->fd, cond);
}

typedef enum {
	CR_OK,
	CR_EOF,
	CR_ERROR,
	CR_ABORT,
} http_ret_t;

static gboolean http_handle_headers(struct http_request *req);
static http_ret_t http_process_chunked_data(struct http_request *req, const char *buffer, int len);
static http_ret_t http_process_data(struct http_request *req, const char *buffer, int len);

static gboolean http_incoming_data(gpointer data, int source, b_input_condition cond)
{
	struct http_request *req = data;
	char buffer[4096];
	int st;

	if (req->inpa > 0) {
		b_event_remove(req->inpa);
		req->inpa = 0;
	}

	if (req->ssl) {
		st = ssl_read(req->ssl, buffer, sizeof(buffer));
		if (st < 0) {
			if (ssl_errno != SSL_AGAIN) {
				/* goto cleanup; */

				/* YAY! We have to deal with crappy Microsoft
				   servers that LOVE to send invalid TLS
				   packets that abort connections! \o/ */

				goto eof;
			}
		} else if (st == 0) {
			goto eof;
		}
	} else {
		st = read(req->fd, buffer, sizeof(buffer));
		if (st < 0) {
			if (!sockerr_again()) {
				req->status_string = g_strdup(strerror(errno));
				goto cleanup;
			}
		} else if (st == 0) {
			goto eof;
		}
	}

	if (st > 0) {
		http_ret_t c;

		if (req->flags & HTTPC_CHUNKED) {
			c = http_process_chunked_data(req, buffer, st);
		} else {
			c = http_process_data(req, buffer, st);
		}

		if (c == CR_EOF) {
			goto eof;
		} else if (c == CR_ERROR || c == CR_ABORT) {
			return FALSE;
		}
	}

	if (req->content_length != -1 &&
	    req->body_size >= req->content_length) {
		goto eof;
	}

	if (ssl_pending(req->ssl)) {
		return http_incoming_data(data, source, cond);
	}

	/* There will be more! */
	req->inpa = b_input_add(req->fd,
	                        req->ssl ? ssl_getdirection(req->ssl) : B_EV_IO_READ,
	                        http_incoming_data, req);

	return FALSE;

eof:
	req->flags |= HTTPC_EOF;

	/* Maybe if the webserver is overloaded, or when there's bad SSL
	   support... */
	if (req->bytes_read == 0) {
		req->status_string = g_strdup("Empty HTTP reply");
		goto cleanup;
	}

cleanup:
	/* Avoid g_source_remove warnings */
	req->inpa = 0;

	if (req->ssl) {
		ssl_disconnect(req->ssl);
	} else {
		closesocket(req->fd);
	}

	if (req->body_size < req->content_length) {
		req->status_code = -1;
		g_free(req->status_string);
		req->status_string = g_strdup("Response truncated");
	}

	if (getenv("BITLBEE_DEBUG") && req) {
		printf("Finishing HTTP request with status: %s\n",
		       req->status_string ? req->status_string : "NULL");
	}

	req->func(req);
	http_free(req);
	return FALSE;
}

static http_ret_t http_process_chunked_data(struct http_request *req, const char *buffer, int len)
{
	char *chunk, *eos, *s;

	if (len < 0) {
		return TRUE;
	}

	if (len > 0) {
		req->cbuf = g_realloc(req->cbuf, req->cblen + len + 1);
		memcpy(req->cbuf + req->cblen, buffer, len);
		req->cblen += len;
		req->cbuf[req->cblen] = '\0';
	}

	/* Turns out writing a proper chunked-encoding state machine is not
	   that simple. :-( I've tested this one feeding it byte by byte so
	   I hope it's solid now. */
	chunk = req->cbuf;
	eos = req->cbuf + req->cblen;
	while (TRUE) {
		int clen = 0;

		/* Might be a \r\n from the last chunk. */
		s = chunk;
		while (g_ascii_isspace(*s)) {
			s++;
		}
		/* Chunk length. Might be incomplete. */
		if (s < eos && sscanf(s, "%x", &clen) != 1) {
			return CR_ERROR;
		}
		while (g_ascii_isxdigit(*s)) {
			s++;
		}

		/* If we read anything here, it *must* be \r\n. */
		if (strncmp(s, "\r\n", MIN(2, eos - s)) != 0) {
			return CR_ERROR;
		}
		s += 2;

		if (s >= eos) {
			break;
		}

		/* 0-length chunk means end of response. */
		if (clen == 0) {
			return CR_EOF;
		}

		/* Wait for the whole chunk to arrive. */
		if (s + clen > eos) {
			break;
		}
		if (http_process_data(req, s, clen) != CR_OK) {
			return CR_ABORT;
		}

		chunk = s + clen;
	}

	if (chunk != req->cbuf) {
		req->cblen = eos - chunk;
		s = g_memdup(chunk, req->cblen + 1);
		g_free(req->cbuf);
		req->cbuf = s;
	}

	return CR_OK;
}

static http_ret_t http_process_data(struct http_request *req, const char *buffer, int len)
{
	if (len <= 0) {
		return CR_OK;
	}

	if (!req->reply_body) {
		req->reply_headers = g_realloc(req->reply_headers, req->bytes_read + len + 1);
		memcpy(req->reply_headers + req->bytes_read, buffer, len);
		req->bytes_read += len;
		req->reply_headers[req->bytes_read] = '\0';

		if (strstr(req->reply_headers, "\r\n\r\n") ||
		    strstr(req->reply_headers, "\n\n")) {
			/* We've now received all headers. Look for something
			   interesting. */
			if (!http_handle_headers(req)) {
				return CR_ABORT;
			}

			/* Start parsing the body as chunked if required. */
			if (req->flags & HTTPC_CHUNKED) {
				return http_process_chunked_data(req, NULL, 0);
			}
		}
	} else {
		int pos = req->reply_body - req->sbuf;
		req->sbuf = g_realloc(req->sbuf, req->sblen + len + 1);
		memcpy(req->sbuf + req->sblen, buffer, len);
		req->bytes_read += len;
		req->sblen += len;
		req->sbuf[req->sblen] = '\0';
		req->reply_body = req->sbuf + pos;
		req->body_size = req->sblen - pos;
	}

	if ((req->flags & HTTPC_STREAMING) && req->reply_body) {
		req->func(req);
	}

	return CR_OK;
}

/* Splits headers and body. Checks result code, in case of 300s it'll handle
   redirects. If this returns FALSE, don't call any callbacks! */
static gboolean http_handle_headers(struct http_request *req)
{
	char *end1, *end2, *s;
	int evil_server = 0;

	/* Zero termination is very convenient. */
	req->reply_headers[req->bytes_read] = '\0';

	/* Find the separation between headers and body, and keep stupid
	   webservers in mind. */
	end1 = strstr(req->reply_headers, "\r\n\r\n");
	end2 = strstr(req->reply_headers, "\n\n");

	if (end2 && end2 < end1) {
		end1 = end2 + 1;
		evil_server = 1;
	} else if (end1) {
		end1 += 2;
	} else {
		req->status_string = g_strdup("Malformed HTTP reply");
		return TRUE;
	}

	*end1 = '\0';

	if (getenv("BITLBEE_DEBUG")) {
		printf("HTTP response headers:\n%s\n", req->reply_headers);
	}

	if (evil_server) {
		req->reply_body = end1 + 1;
	} else {
		req->reply_body = end1 + 2;
	}

	/* Separately allocated space for headers and body. */
	req->sblen = req->body_size = req->reply_headers + req->bytes_read - req->reply_body;
	req->sbuf = req->reply_body = g_memdup(req->reply_body, req->body_size + 1);
	req->reply_headers = g_realloc(req->reply_headers, end1 - req->reply_headers + 1);

	if ((end1 = strchr(req->reply_headers, ' ')) != NULL) {
		if (sscanf(end1 + 1, "%hd", &req->status_code) != 1) {
			req->status_string = g_strdup("Can't parse status code");
			req->status_code = -1;
		} else {
			char *eol;

			if (evil_server) {
				eol = strchr(end1, '\n');
			} else {
				eol = strchr(end1, '\r');
			}

			req->status_string = g_strndup(end1 + 1, eol - end1 - 1);

			/* Just to be sure... */
			if ((eol = strchr(req->status_string, '\r'))) {
				*eol = 0;
			}
			if ((eol = strchr(req->status_string, '\n'))) {
				*eol = 0;
			}
		}
	} else {
		req->status_string = g_strdup("Can't locate status code");
		req->status_code = -1;
	}

	if (((req->status_code >= 301 && req->status_code <= 303) ||
	     req->status_code == 307) && req->redir_ttl-- > 0) {
		char *loc, *new_request, *new_host;
		int error = 0, new_port, new_proto;

		/* We might fill it again, so let's not leak any memory. */
		g_free(req->status_string);
		req->status_string = NULL;

		loc = strstr(req->reply_headers, "\nLocation: ");
		if (loc == NULL) { /* We can't handle this redirect... */
			req->status_string = g_strdup("Can't locate Location: header");
			return TRUE;
		}

		loc += 11;
		while (*loc == ' ') {
			loc++;
		}

		/* TODO/FIXME: Possibly have to handle relative redirections,
		   and rewrite Host: headers. Not necessary for now, it's
		   enough for passport authentication like this. */

		if (*loc == '/') {
			/* Just a different pathname... */

			/* Since we don't cache the servername, and since we
			   don't need this yet anyway, I won't implement it. */

			req->status_string = g_strdup("Can't handle relative redirects");

			return TRUE;
		} else {
			/* A whole URL */
			url_t *url;
			char *s, *version, *headers;
			const char *new_method;

			s = strstr(loc, "\r\n");
			if (s == NULL) {
				return TRUE;
			}

			url = g_new0(url_t, 1);
			*s = 0;

			if (!url_set(url, loc)) {
				req->status_string = g_strdup("Malformed redirect URL");
				g_free(url);
				return TRUE;
			}

			/* Find all headers and, if necessary, the POST request contents.
			   Skip the old Host: header though. This crappy code here means
			   anything using this http_client MUST put the Host: header at
			   the top. */
			if (!((s = strstr(req->request, "\r\nHost: ")) &&
			      (s = strstr(s + strlen("\r\nHost: "), "\r\n")))) {
				req->status_string = g_strdup("Error while rebuilding request string");
				g_free(url);
				return TRUE;
			}
			headers = s;

			/* More or less HTTP/1.0 compliant, from my reading of RFC 2616.
			   Always perform a GET request unless we received a 301. 303 was
			   meant for this but it's HTTP/1.1-only and we're specifically
			   speaking HTTP/1.0. ...

			   Well except someone at identi.ca's didn't bother reading any
			   RFCs and just return HTTP/1.1-specific status codes to HTTP/1.0
			   requests. Fuckers. So here we are, handle 301..303,307. */
			if (strncmp(req->request, "GET", 3) == 0) {
				/* GETs never become POSTs. */
				new_method = "GET";
			} else if (req->status_code == 302 || req->status_code == 303) {
				/* 302 de-facto becomes GET, 303 as specified by RFC 2616#10.3.3 */
				new_method = "GET";
			} else {
				/* 301 de-facto should stay POST, 307 specifally RFC 2616#10.3.8 */
				new_method = "POST";
			}

			if ((version = strstr(req->request, " HTTP/")) &&
			    (s = strstr(version, "\r\n"))) {
				version++;
				version = g_strndup(version, s - version);
			} else {
				version = g_strdup("HTTP/1.0");
			}

			/* Okay, this isn't fun! We have to rebuild the request... :-( */
			new_request = g_strdup_printf("%s %s %s\r\nHost: %s%s",
			                              new_method, url->file, version,
			                              url->host, headers);

			new_host = g_strdup(url->host);
			new_port = url->port;
			new_proto = url->proto;

			/* If we went from POST to GET, truncate the request content. */
			if (new_request[0] != req->request[0] && new_request[0] == 'G' &&
			    (s = strstr(new_request, "\r\n\r\n"))) {
				s[4] = '\0';
			}

			g_free(url);
			g_free(version);
		}

		if (req->ssl) {
			ssl_disconnect(req->ssl);
		} else {
			closesocket(req->fd);
		}

		req->fd = -1;
		req->ssl = NULL;

		if (getenv("BITLBEE_DEBUG")) {
			printf("New headers for redirected HTTP request:\n%s\n", new_request);
		}

		if (new_proto == PROTO_HTTPS) {
			req->ssl = ssl_connect(new_host, new_port, TRUE, http_ssl_connected, req);
			if (req->ssl == NULL) {
				error = 1;
			}
		} else {
			req->fd = proxy_connect(new_host, new_port, http_connected, req);
			if (req->fd < 0) {
				error = 1;
			}
		}
		g_free(new_host);

		if (error) {
			req->status_string = g_strdup("Connection problem during redirect");
			g_free(new_request);
			return TRUE;
		}

		g_free(req->request);
		g_free(req->reply_headers);
		g_free(req->sbuf);
		req->request = new_request;
		req->request_length = strlen(new_request);
		req->bytes_read = req->bytes_written = req->inpa = 0;
		req->reply_headers = req->reply_body = NULL;
		req->sbuf = req->cbuf = NULL;
		req->sblen = req->cblen = 0;

		return FALSE;
	}

	if ((s = get_rfc822_header(req->reply_headers, "Content-Length", 0)) &&
	    sscanf(s, "%d", &req->content_length) != 1) {
		req->content_length = -1;
	}
	g_free(s);

	if ((s = get_rfc822_header(req->reply_headers, "Transfer-Encoding", 0))) {
		if (strcasestr(s, "chunked")) {
			req->flags |= HTTPC_CHUNKED;
			req->cbuf = req->sbuf;
			req->cblen = req->sblen;

			req->reply_body = req->sbuf = g_strdup("");
			req->body_size = req->sblen = 0;
		}
		g_free(s);
	}

	return TRUE;
}

void http_flush_bytes(struct http_request *req, size_t len)
{
	if (len <= 0 || len > req->body_size || !(req->flags & HTTPC_STREAMING)) {
		return;
	}

	req->reply_body += len;
	req->body_size -= len;

	if (req->reply_body - req->sbuf >= 512) {
		char *new = g_memdup(req->reply_body, req->body_size + 1);
		g_free(req->sbuf);
		req->reply_body = req->sbuf = new;
		req->sblen = req->body_size;
	}
}

void http_close(struct http_request *req)
{
	if (!req) {
		return;
	}

	if (req->inpa > 0) {
		b_event_remove(req->inpa);
	}

	if (req->ssl) {
		ssl_disconnect(req->ssl);
	} else {
		closesocket(req->fd);
	}

	http_free(req);
}

static void http_free(struct http_request *req)
{
	g_free(req->request);
	g_free(req->reply_headers);
	g_free(req->status_string);
	g_free(req->sbuf);
	g_free(req->cbuf);
	g_free(req);
}
