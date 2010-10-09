  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2005 Wilmer van der Gaast and others                *
  \********************************************************************/

/* SSL module - NSS version                                             */

/* Copyright 2005 Jelmer Vernooij                                       */

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

#include "bitlbee.h"
#include "proxy.h"
#include "ssl_client.h"
#include "sock.h"
#include <nspr.h>
#include <prio.h>
#include <sslproto.h>
#include <nss.h>
#include <private/pprio.h>
#include <ssl.h>
#include <secerr.h>
#include <sslerr.h>

int ssl_errno = 0;

static gboolean initialized = FALSE;

struct scd
{
	ssl_input_function func;
	gpointer data;
	int fd;
	PRFileDesc *prfd;
	gboolean established;
};

static gboolean ssl_connected( gpointer data, gint source, b_input_condition cond );


static SECStatus nss_auth_cert (void *arg, PRFileDesc *socket, PRBool checksig, PRBool isserver)
{
	return SECSuccess;
}

static SECStatus nss_bad_cert (void *arg, PRFileDesc *socket) 
{
	PRErrorCode err;

	if(!arg) return SECFailure;

	*(PRErrorCode *)arg = err = PORT_GetError();

	switch(err) {
	case SEC_ERROR_INVALID_AVA:
	case SEC_ERROR_INVALID_TIME:
	case SEC_ERROR_BAD_SIGNATURE:
	case SEC_ERROR_EXPIRED_CERTIFICATE:
	case SEC_ERROR_UNKNOWN_ISSUER:
	case SEC_ERROR_UNTRUSTED_CERT:
	case SEC_ERROR_CERT_VALID:
	case SEC_ERROR_EXPIRED_ISSUER_CERTIFICATE:
	case SEC_ERROR_CRL_EXPIRED:
	case SEC_ERROR_CRL_BAD_SIGNATURE:
	case SEC_ERROR_EXTENSION_VALUE_INVALID:
	case SEC_ERROR_CA_CERT_INVALID:
	case SEC_ERROR_CERT_USAGES_INVALID:
	case SEC_ERROR_UNKNOWN_CRITICAL_EXTENSION:
		return SECSuccess;

	default:
		return SECFailure;
	}
}


void ssl_init( void )
{
	PR_Init( PR_SYSTEM_THREAD, PR_PRIORITY_NORMAL, 1);
	NSS_NoDB_Init(NULL);
	NSS_SetDomesticPolicy();
	initialized = TRUE;
}

void *ssl_connect( char *host, int port, ssl_input_function func, gpointer data )
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
		ssl_init();
	}

	
	return( conn );
}

static gboolean ssl_connected( gpointer data, gint source, b_input_condition cond )
{
	struct scd *conn = data;
	
	if( source == -1 )
		goto ssl_connected_failure;
	
	/* Until we find out how to handle non-blocking I/O with NSS... */
	sock_make_blocking( conn->fd );
	
	conn->prfd = SSL_ImportFD(NULL, PR_ImportTCPSocket(source));
	SSL_OptionSet(conn->prfd, SSL_SECURITY, PR_TRUE);
	SSL_OptionSet(conn->prfd, SSL_HANDSHAKE_AS_CLIENT, PR_TRUE);
	SSL_BadCertHook(conn->prfd, (SSLBadCertHandler)nss_bad_cert, NULL);
	SSL_AuthCertificateHook(conn->prfd, (SSLAuthCertificate)nss_auth_cert, (void *)CERT_GetDefaultCertDB());
	SSL_ResetHandshake(conn->prfd, PR_FALSE);

	if (SSL_ForceHandshake(conn->prfd)) {
		goto ssl_connected_failure;
	}
	
	
	conn->established = TRUE;
	conn->func( conn->data, conn, cond );
	return FALSE;
	
	ssl_connected_failure:
	
	conn->func( conn->data, NULL, cond );
	
	PR_Close( conn -> prfd );
	if( source >= 0 ) closesocket( source );
	g_free( conn );
	
	return FALSE;
}

int ssl_read( void *conn, char *buf, int len )
{
	if( !((struct scd*)conn)->established )
		return( 0 );
	
	return( PR_Read( ((struct scd*)conn)->prfd, buf, len ) );
}

int ssl_write( void *conn, const char *buf, int len )
{
	if( !((struct scd*)conn)->established )
		return( 0 );
	
	return( PR_Write ( ((struct scd*)conn)->prfd, buf, len ) );
}

/* See ssl_openssl.c for an explanation. */
int ssl_pending( void *conn )
{
	return 0;
}

void ssl_disconnect( void *conn_ )
{
	struct scd *conn = conn_;
	
	PR_Close( conn->prfd );
	closesocket( conn->fd );
	
	g_free( conn );
}

int ssl_getfd( void *conn )
{
	return( ((struct scd*)conn)->fd );
}

b_input_condition ssl_getdirection( void *conn )
{
	/* Just in case someone calls us, let's return the most likely case: */
	return B_EV_IO_READ;
}
