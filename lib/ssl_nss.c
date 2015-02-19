/********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2012 Wilmer van der Gaast and others                *
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
  if not, write to the Free Software Foundation, Inc., 51 Franklin St.,
  Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "bitlbee.h"
#include "proxy.h"
#include "ssl_client.h"
#include "sock.h"
#include <nspr.h>
#include <prio.h>
#include <sslproto.h>
#include <nss.h>
#include <pk11pub.h>
#include <private/pprio.h>
#include <ssl.h>
#include <seccomon.h>
#include <secerr.h>
#include <sslerr.h>
#include <assert.h>
#include <unistd.h>

int ssl_errno = 0;

static gboolean initialized = FALSE;

#define SSLDEBUG 0

struct scd {
	ssl_input_function func;
	gpointer data;
	int fd;
	char *hostname;
	PRFileDesc *prfd;
	gboolean established;
	gboolean verify;
};

static gboolean ssl_connected(gpointer data, gint source,
                              b_input_condition cond);
static gboolean ssl_starttls_real(gpointer data, gint source,
                                  b_input_condition cond);

static SECStatus nss_auth_cert(void *arg, PRFileDesc * socket, PRBool checksig,
                               PRBool isserver)
{
	return SECSuccess;
}

static SECStatus nss_bad_cert(void *arg, PRFileDesc * socket)
{
	PRErrorCode err;

	if (!arg) {
		return SECFailure;
	}

	*(PRErrorCode *) arg = err = PORT_GetError();

	switch (err) {
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

void ssl_init(void)
{
	PR_Init(PR_SYSTEM_THREAD, PR_PRIORITY_NORMAL, 1);
	// https://www.mozilla.org/projects/security/pki/nss/ref/ssl/sslfnc.html#1234224
	// This NSS function is not intended for use with SSL, which
	// requires that the certificate and key database files be
	// opened. Relates to whole non-verification of servers for now.
	NSS_NoDB_Init(NULL);
	NSS_SetDomesticPolicy();
	initialized = TRUE;
}

void *ssl_connect(char *host, int port, gboolean verify,
                  ssl_input_function func, gpointer data)
{
	struct scd *conn = g_new0(struct scd, 1);

	conn->fd = proxy_connect(host, port, ssl_connected, conn);
	conn->func = func;
	conn->data = data;
	conn->hostname = g_strdup(host);

	if (conn->fd < 0) {
		g_free(conn->hostname);
		g_free(conn);
		return (NULL);
	}

	if (!initialized) {
		ssl_init();
	}

	return (conn);
}

static gboolean ssl_starttls_real(gpointer data, gint source,
                                  b_input_condition cond)
{
	struct scd *conn = data;

	return ssl_connected(conn, conn->fd, B_EV_IO_WRITE);
}

void *ssl_starttls(int fd, char *hostname, gboolean verify,
                   ssl_input_function func, gpointer data)
{
	struct scd *conn = g_new0(struct scd, 1);

	conn->fd = fd;
	conn->func = func;
	conn->data = data;
	conn->hostname = g_strdup(hostname);

	/* For now, SSL verification is globally enabled by setting the cafile
	   setting in bitlbee.conf. Commented out by default because probably
	   not everyone has this file in the same place and plenty of folks
	   may not have the cert of their private Jabber server in it. */
	conn->verify = verify && global.conf->cafile;

	/* This function should be called via a (short) timeout instead of
	   directly from here, because these SSL calls are *supposed* to be
	   *completely* asynchronous and not ready yet when this function
	   (or *_connect, for examle) returns. Also, errors are reported via
	   the callback function, not via this function's return value.

	   In short, doing things like this makes the rest of the code a lot
	   simpler. */

	b_timeout_add(1, ssl_starttls_real, conn);

	return conn;
}

static gboolean ssl_connected(gpointer data, gint source,
                              b_input_condition cond)
{
	struct scd *conn = data;

	/* Right now we don't have any verification functionality for NSS. */

	if (conn->verify) {
		conn->func(conn->data, 1, NULL, cond);
		if (source >= 0) {
			closesocket(source);
		}
		g_free(conn->hostname);
		g_free(conn);

		return FALSE;
	}

	if (source == -1) {
		goto ssl_connected_failure;
	}

	/* Until we find out how to handle non-blocking I/O with NSS... */
	sock_make_blocking(conn->fd);

	conn->prfd = SSL_ImportFD(NULL, PR_ImportTCPSocket(source));
	if (!conn->prfd) {
		goto ssl_connected_failure;
	}
	SSL_OptionSet(conn->prfd, SSL_SECURITY, PR_TRUE);
	SSL_OptionSet(conn->prfd, SSL_HANDSHAKE_AS_CLIENT, PR_TRUE);
	SSL_BadCertHook(conn->prfd, (SSLBadCertHandler) nss_bad_cert, NULL);
	SSL_AuthCertificateHook(conn->prfd, (SSLAuthCertificate) nss_auth_cert,
	                        (void *) CERT_GetDefaultCertDB());
	SSL_SetURL(conn->prfd, conn->hostname);
	SSL_ResetHandshake(conn->prfd, PR_FALSE);

	if (SSL_ForceHandshake(conn->prfd)) {
		goto ssl_connected_failure;
	}

	conn->established = TRUE;
	conn->func(conn->data, 0, conn, cond);
	return FALSE;

ssl_connected_failure:

	conn->func(conn->data, 0, NULL, cond);

	if (conn->prfd) {
		PR_Close(conn->prfd);
	}
	if (source >= 0) {
		closesocket(source);
	}
	g_free(conn->hostname);
	g_free(conn);

	return FALSE;
}

int ssl_read(void *conn, char *buf, int len)
{
	int st;
	PRErrorCode PR_err;

	if (!((struct scd *) conn)->established) {
		ssl_errno = SSL_NOHANDSHAKE;
		return -1;
	}

	st = PR_Read(((struct scd *) conn)->prfd, buf, len);
	PR_err = PR_GetError();

	ssl_errno = SSL_OK;
	if (PR_err == PR_WOULD_BLOCK_ERROR) {
		ssl_errno = SSL_AGAIN;
	}

	if (SSLDEBUG && getenv("BITLBEE_DEBUG") && st > 0) {
		len = write(STDERR_FILENO, buf, st);
	}

	return st;
}

int ssl_write(void *conn, const char *buf, int len)
{
	int st;
	PRErrorCode PR_err;

	if (!((struct scd *) conn)->established) {
		ssl_errno = SSL_NOHANDSHAKE;
		return -1;
	}
	st = PR_Write(((struct scd *) conn)->prfd, buf, len);
	PR_err = PR_GetError();

	ssl_errno = SSL_OK;
	if (PR_err == PR_WOULD_BLOCK_ERROR) {
		ssl_errno = SSL_AGAIN;
	}

	if (SSLDEBUG && getenv("BITLBEE_DEBUG") && st > 0) {
		len = write(2, buf, st);
	}

	return st;
}

int ssl_pending(void *conn)
{
	struct scd *c = (struct scd *) conn;

	if (c == NULL) {
		return 0;
	}

	return (c->established && SSL_DataPending(c->prfd) > 0);
}

void ssl_disconnect(void *conn_)
{
	struct scd *conn = conn_;

	// When we swich to NSS_Init, we should have here
	// NSS_Shutdown();

	if (conn->prfd) {
		PR_Close(conn->prfd);
	}

	g_free(conn->hostname);
	g_free(conn);
}

int ssl_getfd(void *conn)
{
	return (((struct scd *) conn)->fd);
}

b_input_condition ssl_getdirection(void *conn)
{
	/* Just in case someone calls us, let's return the most likely case: */
	return B_EV_IO_READ;
}

char *ssl_verify_strerror(int code)
{
	return
	        g_strdup
	                ("SSL certificate verification not supported by BitlBee NSS code.");
}

size_t ssl_des3_encrypt(const unsigned char *key, size_t key_len,
                        const unsigned char *input, size_t input_len,
                        const unsigned char *iv, unsigned char **res)
{
#define CIPHER_MECH CKM_DES3_CBC
#define MAX_OUTPUT_LEN 72

	int len1;
	unsigned int len2;

	PK11Context *ctx = NULL;
	PK11SlotInfo *slot = NULL;
	SECItem keyItem;
	SECItem ivItem;
	SECItem *secParam = NULL;
	PK11SymKey *symKey = NULL;

	size_t rc;
	SECStatus rv;

	if (!initialized) {
		ssl_init();
	}

	keyItem.data = (unsigned char *) key;
	keyItem.len = key_len;

	slot = PK11_GetBestSlot(CIPHER_MECH, NULL);
	if (slot == NULL) {
		fprintf(stderr, "PK11_GetBestSlot failed (err %d)\n",
		        PR_GetError());
		rc = 0;
		goto out;
	}

	symKey =
	        PK11_ImportSymKey(slot, CIPHER_MECH, PK11_OriginUnwrap, CKA_ENCRYPT,
	                          &keyItem, NULL);
	if (symKey == NULL) {
		fprintf(stderr, "PK11_ImportSymKey failed (err %d)\n",
		        PR_GetError());
		rc = 0;
		goto out;
	}

	ivItem.data = (unsigned char *) iv;
	/* See msn_soap_passport_sso_handle_response in protocols/msn/soap.c */
	ivItem.len = 8;

	secParam = PK11_ParamFromIV(CIPHER_MECH, &ivItem);
	if (secParam == NULL) {
		fprintf(stderr, "PK11_ParamFromIV failed (err %d)\n",
		        PR_GetError());
		rc = 0;
		goto out;
	}

	ctx =
	        PK11_CreateContextBySymKey(CIPHER_MECH, CKA_ENCRYPT, symKey,
	                                   secParam);
	if (ctx == NULL) {
		fprintf(stderr, "PK11_CreateContextBySymKey failed (err %d)\n",
		        PR_GetError());
		rc = 0;
		goto out;
	}

	*res = g_new0(unsigned char, MAX_OUTPUT_LEN);

	rv = PK11_CipherOp(ctx, *res, &len1, MAX_OUTPUT_LEN,
	                   (unsigned char *) input, input_len);
	if (rv != SECSuccess) {
		fprintf(stderr, "PK11_CipherOp failed (err %d)\n",
		        PR_GetError());
		rc = 0;
		goto out;
	}

	assert(len1 <= MAX_OUTPUT_LEN);

	rv = PK11_DigestFinal(ctx, *res + len1, &len2,
	                      (unsigned int) MAX_OUTPUT_LEN - len1);
	if (rv != SECSuccess) {
		fprintf(stderr, "PK11_DigestFinal failed (err %d)\n",
		        PR_GetError());
		rc = 0;
		goto out;
	}

	rc = len1 + len2;

out:
	if (ctx) {
		PK11_DestroyContext(ctx, PR_TRUE);
	}
	if (symKey) {
		PK11_FreeSymKey(symKey);
	}
	if (secParam) {
		SECITEM_FreeItem(secParam, PR_TRUE);
	}
	if (slot) {
		PK11_FreeSlot(slot);
	}

	return rc;
}
