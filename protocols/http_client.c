  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2005 Wilmer van der Gaast and others                *
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
  if not, write to the Free Software Foundation, Inc., 59 Temple Place,
  Suite 330, Boston, MA  02111-1307  USA
*/

#include <string.h>
#include <stdio.h>

#include "http_client.h"
#include "url.h"
#include "sock.h"


static void http_connected( gpointer data, int source, GaimInputCondition cond );
static void http_ssl_connected( gpointer data, void *source, GaimInputCondition cond );
static void http_incoming_data( gpointer data, int source, GaimInputCondition cond );


void *http_dorequest( char *host, int port, int ssl, char *request, http_input_function func, gpointer data )
{
	struct http_request *req;
	int error = 0;
	
	req = g_new0( struct http_request, 1 );
	
	if( ssl )
	{
		req->ssl = ssl_connect( host, port, http_ssl_connected, req );
		if( req->ssl == NULL )
			error = 1;
	}
	else
	{
		req->fd = proxy_connect( host, port, http_connected, req );
		if( req->fd < 0 )
			error = 1;
	}
	
	if( error )
	{
		g_free( req );
		return( NULL );
	}
	
	req->func = func;
	req->data = data;
	req->request = g_strdup( request );
	req->request_length = strlen( request );
	
	return( req );
}

/* This one is actually pretty simple... Might get more calls if we can't write 
   the whole request at once. */
static void http_connected( gpointer data, int source, GaimInputCondition cond )
{
	struct http_request *req = data;
	int st;
	
	if( source < 0 )
		goto error;
	
	if( req->inpa > 0 )
		gaim_input_remove( req->inpa );
	
	sock_make_nonblocking( req->fd );
	
	if( req->ssl )
	{
		st = ssl_write( req->ssl, req->request + req->bytes_written,
		                req->request_length - req->bytes_written );
		if( st < 0 )
		{
			if( ssl_errno != SSL_AGAIN )
			{
				ssl_disconnect( req->ssl );
				goto error;
			}
		}
	}
	else
	{
		st = write( source, req->request + req->bytes_written,
		                    req->request_length - req->bytes_written );
		if( st < 0 )
		{
			if( !sockerr_again() )
			{
				closesocket( req->fd );
				goto error;
			}
		}
	}
	
	if( st > 0 )
		req->bytes_written += st;
	
	if( req->bytes_written < req->request_length )
		req->inpa = gaim_input_add( source,
		                            req->ssl ? ssl_getdirection( req->ssl ) : GAIM_INPUT_WRITE,
	        	                    http_connected, req );
	else
		req->inpa = gaim_input_add( source, GAIM_INPUT_READ, http_incoming_data, req );
	
	return;
	
error:
	req->func( req );
	
	g_free( req->request );
	g_free( req );
	
	return;
}

static void http_ssl_connected( gpointer data, void *source, GaimInputCondition cond )
{
	struct http_request *req = data;
	
	if( source == NULL )
		return http_connected( data, -1, cond );
	
	req->fd = ssl_getfd( source );
	
	return http_connected( data, req->fd, cond );
}

static void http_incoming_data( gpointer data, int source, GaimInputCondition cond )
{
	struct http_request *req = data;
	int evil_server = 0;
	char buffer[2048];
	char *end1, *end2;
	int st;
	
	if( req->inpa > 0 )
		gaim_input_remove( req->inpa );
	
	if( req->ssl )
	{
		st = ssl_read( req->ssl, buffer, sizeof( buffer ) );
		if( st < 0 )
		{
			if( ssl_errno != SSL_AGAIN )
			{
				/* goto cleanup; */
				
				/* YAY! We have to deal with crappy Microsoft
				   servers that LOVE to send invalid TLS
				   packets that abort connections! \o/ */
				
				goto got_reply;
			}
		}
		else if( st == 0 )
		{
			goto got_reply;
		}
	}
	else
	{
		st = read( req->fd, buffer, sizeof( buffer ) );
		if( st < 0 )
		{
			if( !sockerr_again() )
			{
				goto cleanup;
			}
		}
		else if( st == 0 )
		{
			goto got_reply;
		}
	}
	
	if( st > 0 )
	{
		req->reply_headers = g_realloc( req->reply_headers, req->bytes_read + st + 1 );
		memcpy( req->reply_headers + req->bytes_read, buffer, st );
		req->bytes_read += st;
	}
	
	/* There will be more! */
	req->inpa = gaim_input_add( req->fd,
	                            req->ssl ? ssl_getdirection( req->ssl ) : GAIM_INPUT_READ,
	                            http_incoming_data, req );
	
	return;

got_reply:
	/* Zero termination is very convenient. */
	req->reply_headers[req->bytes_read] = 0;
	
	/* Find the separation between headers and body, and keep stupid
	   webservers in mind. */
	end1 = strstr( req->reply_headers, "\r\n\r\n" );
	end2 = strstr( req->reply_headers, "\n\n" );
	
	if( end2 && end2 < end1 )
	{
		end1 = end2 + 1;
		evil_server = 1;
	}
	else
	{
		end1 += 2;
	}
	
	if( end1 )
	{
		*end1 = 0;
		
		if( evil_server )
			req->reply_body = end1 + 1;
		else
			req->reply_body = end1 + 2;
	}
	
	if( ( end1 = strchr( req->reply_headers, ' ' ) ) != NULL )
	{
		if( sscanf( end1 + 1, "%d", &req->status_code ) != 1 )
			req->status_code = -1;
	}
	else
	{
		req->status_code = -1;
	}
	
	if( req->status_code == 301 || req->status_code == 302 )
	{
		char *loc, *new_request, *new_host;
		int error = 0, new_port, new_proto;
		
		loc = strstr( req->reply_headers, "\nLocation: " );
		if( loc == NULL ) /* We can't handle this redirect... */
			goto cleanup;
		
		loc += 11;
		while( *loc == ' ' )
			loc ++;
		
		/* TODO/FIXME: Possibly have to handle relative redirections,
		   and rewrite Host: headers. Not necessary for now, it's
		   enough for passport authentication like this. */
		
		if( *loc == '/' )
		{
			/* Just a different pathname... */
			
			/* Since we don't cache the servername, and since we
			   don't need this yet anyway, I won't implement it. */
			
			goto cleanup;
		}
		else
		{
			/* A whole URL */
			url_t *url;
			char *s;
			
			s = strstr( loc, "\r\n" );
			if( s == NULL )
				goto cleanup;
			
			url = g_new0( url_t, 1 );
			*s = 0;
			
			if( !url_set( url, loc ) )
			{
				g_free( url );
				goto cleanup;
			}
			
			/* Okay, this isn't fun! We have to rebuild the request... :-( */
			new_request = g_malloc( req->request_length + strlen( url->file ) );
			
			/* So, now I just allocated enough memory, so I'm
			   going to use strcat(), whether you like it or not. :-) */
			
			/* First, find the GET/POST/whatever from the original request. */
			s = strchr( req->request, ' ' );
			if( s == NULL )
			{
				g_free( new_request );
				g_free( url );
				goto cleanup;
			}
			
			*s = 0;
			sprintf( new_request, "%s %s HTTP/1.0\r\n", req->request, url->file );
			*s = ' ';
			
			s = strstr( req->request, "\r\n" );
			if( s == NULL )
			{
				g_free( new_request );
				g_free( url );
				goto cleanup;
			}
			
			strcat( new_request, s + 2 );
			new_host = g_strdup( url->host );
			new_port = url->port;
			new_proto = url->proto;
			
			g_free( url );
		}
		
		if( req->ssl )
			ssl_disconnect( req->ssl );
		else
			closesocket( req->fd );
		
		req->fd = -1;
		req->ssl = 0;
		
		if( new_proto == PROTO_HTTPS )
		{
			req->ssl = ssl_connect( new_host, new_port, http_ssl_connected, req );
			if( req->ssl == NULL )
				error = 1;
		}
		else
		{
			req->fd = proxy_connect( new_host, new_port, http_connected, req );
			if( req->fd < 0 )
				error = 1;
		}
		g_free( new_host );
		
		if( error )
		{
			g_free( new_request );
			goto cleanup;
		}
		
		g_free( req->request );
		g_free( req->reply_headers );
		req->request = new_request;
		req->request_length = strlen( new_request );
		req->bytes_read = req->bytes_written = req->inpa = 0;
		req->reply_headers = req->reply_body = NULL;
		
		return;
	}
	
	/* Assume that a closed connection means we're finished, this indeed
	   breaks with keep-alive connections and faulty connections. */
	req->finished = 1;

cleanup:
	if( req->ssl )
		ssl_disconnect( req->ssl );
	else
		closesocket( req->fd );
	
	req->func( req );
	
	g_free( req->request );
	g_free( req->reply_headers );
	g_free( req );
}
