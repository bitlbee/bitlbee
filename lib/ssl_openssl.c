  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2004 Wilmer van der Gaast and others                *
  \********************************************************************/

/* SSL module - OpenSSL version                                         */

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

#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "proxy.h"
#include "ssl_client.h"
#include "sock.h"

int ssl_errno = 0;

static gboolean initialized = FALSE;

struct scd
{
	ssl_input_function func;
	gpointer data;
	int fd;
	gboolean established;
	
	int inpa;
	int lasterr;		/* Necessary for SSL_get_error */
	SSL *ssl;
	SSL_CTX *ssl_ctx;
};

static gboolean ssl_connected( gpointer data, gint source, b_input_condition cond );
static gboolean ssl_starttls_real( gpointer data, gint source, b_input_condition cond );
static gboolean ssl_handshake( gpointer data, gint source, b_input_condition cond );


void ssl_init( void )
{
	initialized = TRUE;
	SSLeay_add_ssl_algorithms();
}

void *ssl_connect( char *host, int port, ssl_input_function func, gpointer data )
{
	struct scd *conn = g_new0( struct scd, 1 );
	
	conn->fd = proxy_connect( host, port, ssl_connected, conn );
	if( conn->fd < 0 )
	{
		g_free( conn );
		return NULL;
	}
	
	conn->func = func;
	conn->data = data;
	conn->inpa = -1;
	
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
	SSL_METHOD *meth;
	
	if( source == -1 )
		goto ssl_connected_failure;
	
	if( !initialized )
	{
		ssl_init();
	}
	
	meth = TLSv1_client_method();
	conn->ssl_ctx = SSL_CTX_new( meth );
	if( conn->ssl_ctx == NULL )
		goto ssl_connected_failure;
	
	conn->ssl = SSL_new( conn->ssl_ctx );
	if( conn->ssl == NULL )
		goto ssl_connected_failure;
	
	/* We can do at least the handshake with non-blocking I/O */
	sock_make_nonblocking( conn->fd );
	SSL_set_fd( conn->ssl, conn->fd );
	
	return ssl_handshake( data, source, cond );

ssl_connected_failure:
	conn->func( conn->data, NULL, cond );
	
	if( conn->ssl )
	{
		SSL_shutdown( conn->ssl );
		SSL_free( conn->ssl );
	}
	if( conn->ssl_ctx )
	{
		SSL_CTX_free( conn->ssl_ctx );
	}
	if( source >= 0 ) closesocket( source );
	g_free( conn );
	
	return FALSE;

}	

static gboolean ssl_handshake( gpointer data, gint source, b_input_condition cond )
{
	struct scd *conn = data;
	int st;
	
	if( ( st = SSL_connect( conn->ssl ) ) < 0 )
	{
		conn->lasterr = SSL_get_error( conn->ssl, st );
		if( conn->lasterr != SSL_ERROR_WANT_READ && conn->lasterr != SSL_ERROR_WANT_WRITE )
		{
			conn->func( conn->data, NULL, cond );
			
			SSL_shutdown( conn->ssl );
			SSL_free( conn->ssl );
			SSL_CTX_free( conn->ssl_ctx );
			
			if( source >= 0 ) closesocket( source );
			g_free( conn );
			
			return FALSE;
		}
		
		conn->inpa = b_input_add( conn->fd, ssl_getdirection( conn ), ssl_handshake, data );
		return FALSE;
	}
	
	conn->established = TRUE;
	sock_make_blocking( conn->fd );		/* For now... */
	conn->func( conn->data, conn, cond );
	return FALSE;
}

int ssl_read( void *conn, char *buf, int len )
{
	int st;
	
	if( !((struct scd*)conn)->established )
	{
		ssl_errno = SSL_NOHANDSHAKE;
		return -1;
	}
	
	st = SSL_read( ((struct scd*)conn)->ssl, buf, len );
	
	ssl_errno = SSL_OK;
	if( st <= 0 )
	{
		((struct scd*)conn)->lasterr = SSL_get_error( ((struct scd*)conn)->ssl, st );
		if( ((struct scd*)conn)->lasterr == SSL_ERROR_WANT_READ || ((struct scd*)conn)->lasterr == SSL_ERROR_WANT_WRITE )
			ssl_errno = SSL_AGAIN;
	}
	
	return st;
}

int ssl_write( void *conn, const char *buf, int len )
{
	int st;
	
	if( !((struct scd*)conn)->established )
	{
		ssl_errno = SSL_NOHANDSHAKE;
		return -1;
	}
	
	st = SSL_write( ((struct scd*)conn)->ssl, buf, len );
	
	ssl_errno = SSL_OK;
	if( st <= 0 )
	{
		((struct scd*)conn)->lasterr = SSL_get_error( ((struct scd*)conn)->ssl, st );
		if( ((struct scd*)conn)->lasterr == SSL_ERROR_WANT_READ || ((struct scd*)conn)->lasterr == SSL_ERROR_WANT_WRITE )
			ssl_errno = SSL_AGAIN;
	}
	
	return st;
}

/* Only OpenSSL *really* needs this (and well, maybe NSS). See for more info:
   http://www.gnu.org/software/gnutls/manual/gnutls.html#index-gnutls_005frecord_005fcheck_005fpending-209
   http://www.openssl.org/docs/ssl/SSL_pending.html
   
   Required because OpenSSL empties the TCP buffer completely but doesn't
   necessarily give us all the unencrypted data.
   
   Returns 0 if there's nothing left or if we don't have to care (GnuTLS),
   1 if there's more data. */
int ssl_pending( void *conn )
{
	return ( ((struct scd*)conn) && ((struct scd*)conn)->established ) ?
	       SSL_pending( ((struct scd*)conn)->ssl ) > 0 : 0;
}

void ssl_disconnect( void *conn_ )
{
	struct scd *conn = conn_;
	
	if( conn->inpa != -1 )
		b_event_remove( conn->inpa );
	
	if( conn->established )
		SSL_shutdown( conn->ssl );
	
	closesocket( conn->fd );
	
	SSL_free( conn->ssl );
	SSL_CTX_free( conn->ssl_ctx );
	g_free( conn );
}

int ssl_getfd( void *conn )
{
	return( ((struct scd*)conn)->fd );
}

b_input_condition ssl_getdirection( void *conn )
{
	return( ((struct scd*)conn)->lasterr == SSL_ERROR_WANT_WRITE ? GAIM_INPUT_WRITE : GAIM_INPUT_READ );
}
