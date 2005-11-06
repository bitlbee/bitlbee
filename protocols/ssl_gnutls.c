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
#include "proxy.h"
#include "ssl_client.h"
#include "sock.h"
#include "stdlib.h"

static gboolean initialized = FALSE;

struct scd
{
	SslInputFunction func;
	gpointer data;
	int fd;
	gboolean established;
	
	gnutls_session session;
	gnutls_certificate_credentials xcred;
};

static void ssl_connected( gpointer data, gint source, GaimInputCondition cond );



void *ssl_connect( char *host, int port, SslInputFunction func, gpointer data )
{
	struct scd *conn = g_new0( struct scd, 1 );
	
	conn->fd = proxy_connect( host, port, ssl_connected, conn );
	conn->func = func;
	conn->data = data;
	
	if( conn->fd < 0 )
	{
		g_free( conn );
		return( NULL );
	}
	
	if( !initialized )
	{
		gnutls_global_init();
		initialized = TRUE;
		atexit( gnutls_global_deinit );
	}
	
	gnutls_certificate_allocate_credentials( &conn->xcred );
	gnutls_init( &conn->session, GNUTLS_CLIENT );
	gnutls_set_default_priority( conn->session );
	gnutls_credentials_set( conn->session, GNUTLS_CRD_CERTIFICATE, conn->xcred );
	
	return( conn );
}

static void ssl_connected( gpointer data, gint source, GaimInputCondition cond )
{
	struct scd *conn = data;
	
	if( source == -1 )
		goto ssl_connected_failure;
	
	gnutls_transport_set_ptr( conn->session, (gnutls_transport_ptr) conn->fd );
	
	if( gnutls_handshake( conn->session ) < 0 )
		goto ssl_connected_failure;
	
	conn->established = TRUE;
	conn->func( conn->data, conn, cond );
	return;
	
ssl_connected_failure:
	conn->func( conn->data, NULL, cond );
	
	gnutls_deinit( conn->session );
	gnutls_certificate_free_credentials( conn->xcred );
	if( source >= 0 ) closesocket( source );
	g_free( conn );
}

int ssl_read( void *conn, char *buf, int len )
{
	if( !((struct scd*)conn)->established )
		return( 0 );
	
	return( gnutls_record_recv( ((struct scd*)conn)->session, buf, len ) );
}

int ssl_write( void *conn, const char *buf, int len )
{
	if( !((struct scd*)conn)->established )
		return( 0 );
	
	return( gnutls_record_send( ((struct scd*)conn)->session, buf, len ) );
}

void ssl_disconnect( void *conn_ )
{
	struct scd *conn = conn_;
	
	if( conn->established )
		gnutls_bye( conn->session, GNUTLS_SHUT_WR );
	
	closesocket( conn->fd );
	
	gnutls_deinit( conn->session );
	gnutls_certificate_free_credentials( conn->xcred );
	g_free( conn );
}

int ssl_getfd( void *conn )
{
	return( ((struct scd*)conn)->fd );
}
