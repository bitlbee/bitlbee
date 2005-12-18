  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2004 Wilmer van der Gaast and others                *
  \********************************************************************/

/* SSL module - SSPI backend */

/* Copyright (C) 2005 Jelmer Vernooij <jelmer@samba.org> */

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

#include "ssl_client.h"
#include <windows.h>
#define SECURITY_WIN32
#include <security.h>
#include <sspi.h>
#include <schannel.h>

static gboolean initialized = FALSE;
int ssl_errno;

struct scd
{
	int fd;
	SslInputFunction func;
	gpointer data;
	gboolean established;
	int inpa;
  	CredHandle cred;		/* SSL credentials */
	CtxtHandle context;		/* SSL context */
	SecPkgContext_StreamSizes sizes;
};

static void ssl_connected( gpointer data, gint source, GaimInputCondition cond );

void sspi_global_init( void )
{
	/* FIXME */
}

void sspi_global_deinit( void )
{
	/* FIXME */
}

void *ssl_connect( char *host, int port, SslInputFunction func, gpointer data )
{
	struct scd *conn = g_new0( struct scd, 1 );
	SCHANNEL_CRED ssl_cred;
	TimeStamp timestamp;
	
	conn->fd = proxy_connect( host, port, ssl_connected, conn );
	conn->func = func;
	conn->data = data;
	conn->inpa = -1;
	
	if( conn->fd < 0 )
	{
		g_free( conn );
		return( NULL );
	}
	
	if( !initialized )
	{
		sspi_global_init();
		initialized = TRUE;
		atexit( sspi_global_deinit );
	}

	conn->context = NULL;

	memset(&ssl_cred, 0, sizeof(SCHANNEL_CRED));
	ssl_cred.dwVersion = SCHANNEL_CRED_VERSION;
	ssl_cred.grbitEnabledProtocols = SP_PROT_SSL3_CLIENT;

	SECURITY_STATUS st = AcquireCredentialsHandle(NULL, UNISP_NAME, SECPKG_CRED_OUTBOUND, NULL, &ssl_cred, NULL, NULL, &conn->cred, &timestamp);
	
	InitializeSecurityContext(&conn->cred, &conn->context, host, FIXME, 1, FIXME);  

	QueryContextAttributes(&conn->context, SECPKG_ATTR_STREAM_SIZES, &conn->sizes);

	
	return( conn );
}

int ssl_read( void *conn, char *retdata, int len )
{
	struct scd *scd = conn;
	SecBufferDesc msg;
	SecBuffer buf[4];
	int ret = -1, i;
	char *data = g_malloc(scd->sizes.cbHeader + scd->sizes.cbMaximumMessage + scd->sizes.cbTrailer);

	/* FIXME: Try to read some data */

  	msg.ulVersion = SECBUFFER_VERSION;
	msg.cBuffers = 4;
	msg.pBuffers = buf;
	
	buf[0].BufferType = SECBUFFER_DATA;
	buf[0].cbBuffer = len;
	buf[0].pvBuffer = data;

	buf[1].BufferType = SECBUFFER_EMPTY;
	buf[2].BufferType = SECBUFFER_EMPTY;
	buf[3].BufferType = SECBUFFER_EMPTY;

	SECURITY_STATUS st = DecryptMessage(&scd->context, &msg, 0, NULL);

	for (i = 0; i < 4; i++) {
		if (buf[i].BufferType == SECBUFFER_DATA) {
			memcpy(retdata, buf[i].pvBuffer, len);
			ret = len;
		}	
	}

	g_free(data);
	return( -1 );
}

int ssl_write( void *conn, const char *userdata, int len )
{
	struct scd *scd = conn;
	SecBuffer buf[4];
	SecBufferDesc msg;
	char *data;
	int ret;

	msg.ulVersion = SECBUFFER_VERSION;
	msg.cBuffers = 4;
	msg.pBuffers = buf;

	data = g_malloc(scd->sizes.cbHeader + scd->sizes.cbMaximumMessage + scd->sizes.cbTrailer);
	memcpy(data + scd->sizes.cbHeader, userdata, len);

	buf[0].BufferType = SECBUFFER_STREAM_HEADER;
	buf[0].cbBuffer = scd->sizes.cbHeader;
	buf[0].pvBuffer = data;

	buf[1].BufferType = SECBUFFER_DATA;
	buf[1].cbBuffer = len;
	buf[1].pvBuffer = data + scd->sizes.cbHeader;

	buf[2].BufferType = SECBUFFER_STREAM_TRAILER;
	buf[2].cbBuffer = scd->sizes.cbTrailer;
	buf[2].pvBuffer = data + scd->sizes.cbHeader + len;
	buf[3].BufferType = SECBUFFER_EMPTY;

	SECURITY_STATUS st = EncryptMessage(&scd->context, 0, &msg, 0);

	ret = send(scd->fd, data, 
				buf[0].cbBuffer + buf[1].cbBuffer + buf[2].cbBuffer, 0);

	g_free(data);

	return ret;
}

void ssl_disconnect( void *conn )
{
	struct scd *scd = conn;

	SecBufferDesc msg;
	SecBuffer buf;
	DWORD dw;

	dw = SCHANNEL_SHUTDOWN;
	buf.cbBuffer = sizeof(dw);
	buf.BufferType = SECBUFFER_TOKEN;
	buf.pvBuffer = &dw;
	
	msg.ulVersion = SECBUFFER_VERSION;
	msg.cBuffers = 1;
	msg.pBuffers = &buf;

	SECURITY_STATUS st = ApplyControlToken(&scd->context, &msg);

	if (st != SEC_E_OK) {
		/* FIXME */
	}
	
	/* FIXME: call InitializeSecurityContext(Schannel), passing 
	 * in empty buffers*/

	DeleteSecurityContext(&scd->context);

	closesocket( scd->fd );
	g_free(scd);
}

int ssl_getfd( void *conn )
{
	return( ((struct scd*)conn)->fd );
}
