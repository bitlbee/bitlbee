  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2004 Wilmer van der Gaast and others                *
  \********************************************************************/

/* SSL module - GnuTLS version                                          */

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

#include <gnutls/gnutls.h>
#include <fcntl.h>
#include <unistd.h>
#include "proxy.h"
#include "ssl_client.h"
#include "sock.h"
#include "stdlib.h"

int ssl_errno = 0;

static gboolean initialized = FALSE;

#include <limits.h>

#if defined(ULONG_MAX) && ULONG_MAX > 4294967295UL
#define GNUTLS_STUPID_CAST (long)
#else
#define GNUTLS_STUPID_CAST (int)
#endif

struct scd
{
	ssl_input_function func;
	gpointer data;
	int fd;
	gboolean established;
	int inpa;
	
	gnutls_session session;
	gnutls_certificate_credentials xcred;
};

static gboolean ssl_connected( gpointer data, gint source, b_input_condition cond );
static gboolean ssl_starttls_real( gpointer data, gint source, b_input_condition cond );
static gboolean ssl_handshake( gpointer data, gint source, b_input_condition cond );


void ssl_init( void )
{
	gnutls_global_init();
	initialized = TRUE;
	atexit( gnutls_global_deinit );
}

void *ssl_connect( char *host, int port, ssl_input_function func, gpointer data )
{
	struct scd *conn = g_new0( struct scd, 1 );
	
	conn->fd = proxy_connect( host, port, ssl_connected, conn );
	conn->func = func;
	conn->data = data;
	conn->inpa = -1;
	
	if( conn->fd < 0 )
	{
		g_free( conn );
		return NULL;
	}
	
	return conn;
}

void *ssl_starttls( int fd, ssl_input_function func, gpointer data )
{
	struct scd *conn = g_new0( struct scd, 1 );
	
	conn->fd = fd;
	conn->func = func;
	conn->data = data;
	conn->inpa = -1;
	
	/* This function should be called via a (short) timeout instead of
	   directly from here, because these SSL calls are *supposed* to be
	   *completely* asynchronous and not ready yet when this function
	   (or *_connect, for examle) returns. Also, errors are reported via
	   the callback function, not via this function's return value.
	   
	   In short, doing things like this makes the rest of the code a lot
	   simpler. */
	
	b_timeout_add( 1, ssl_starttls_real, conn );
	
	return conn;
}

static gboolean ssl_starttls_real( gpointer data, gint source, b_input_condition cond )
{
	struct scd *conn = data;
	
	return ssl_connected( conn, conn->fd, GAIM_INPUT_WRITE );
}

static gboolean ssl_connected( gpointer data, gint source, b_input_condition cond )
{
	struct scd *conn = data;
	
	if( source == -1 )
	{
		conn->func( conn->data, NULL, cond );
		g_free( conn );
		return FALSE;
	}
	
	if( !initialized )
	{
		ssl_init();
	}
	
	gnutls_certificate_allocate_credentials( &conn->xcred );
	gnutls_init( &conn->session, GNUTLS_CLIENT );
	gnutls_set_default_priority( conn->session );
	gnutls_credentials_set( conn->session, GNUTLS_CRD_CERTIFICATE, conn->xcred );
	
	sock_make_nonblocking( conn->fd );
	gnutls_transport_set_ptr( conn->session, (gnutls_transport_ptr) GNUTLS_STUPID_CAST conn->fd );
	
	return ssl_handshake( data, source, cond );
}

static gboolean ssl_handshake( gpointer data, gint source, b_input_condition cond )
{
	struct scd *conn = data;
	int st;
	
	if( ( st = gnutls_handshake( conn->session ) ) < 0 )
	{
		if( st == GNUTLS_E_AGAIN || st == GNUTLS_E_INTERRUPTED )
		{
			conn->inpa = b_input_add( conn->fd, ssl_getdirection( conn ),
			                          ssl_handshake, data );
		}
		else
		{
			conn->func( conn->data, NULL, cond );
			
			gnutls_deinit( conn->session );
			gnutls_certificate_free_credentials( conn->xcred );
			closesocket( conn->fd );
			
			g_free( conn );
		}
	}
	else
	{
		/* For now we can't handle non-blocking perfectly everywhere... */
		sock_make_blocking( conn->fd );
		
		conn->established = TRUE;
		conn->func( conn->data, conn, cond );
	}
	
	return FALSE;
}

int ssl_read( void *conn, char *buf, int len )
{
	int st;
	
	if( !((struct scd*)conn)->established )
	{
		ssl_errno = SSL_NOHANDSHAKE;
		return( -1 );
	}
	
	st = gnutls_record_recv( ((struct scd*)conn)->session, buf, len );
	
	ssl_errno = SSL_OK;
	if( st == GNUTLS_E_AGAIN || st == GNUTLS_E_INTERRUPTED )
		ssl_errno = SSL_AGAIN;
	
	return st;
}

int ssl_write( void *conn, const char *buf, int len )
{
	int st;
	
	if( !((struct scd*)conn)->established )
	{
		ssl_errno = SSL_NOHANDSHAKE;
		return( -1 );
	}
	
	st = gnutls_record_send( ((struct scd*)conn)->session, buf, len );
	
	ssl_errno = SSL_OK;
	if( st == GNUTLS_E_AGAIN || st == GNUTLS_E_INTERRUPTED )
		ssl_errno = SSL_AGAIN;
	
	return st;
}

/* See ssl_openssl.c for an explanation. */
int ssl_pending( void *conn )
{
	return 0;
}

void ssl_disconnect( void *conn_ )
{
	struct scd *conn = conn_;
	
	if( conn->inpa != -1 )
		b_event_remove( conn->inpa );
	
	if( conn->established )
		gnutls_bye( conn->session, GNUTLS_SHUT_WR );
	
	closesocket( conn->fd );
	
	if( conn->session )
		gnutls_deinit( conn->session );
	if( conn->xcred )
		gnutls_certificate_free_credentials( conn->xcred );
	g_free( conn );
}

int ssl_getfd( void *conn )
{
	return( ((struct scd*)conn)->fd );
}

b_input_condition ssl_getdirection( void *conn )
{
	return( gnutls_record_get_direction( ((struct scd*)conn)->session ) ?
	        GAIM_INPUT_WRITE : GAIM_INPUT_READ );
}
