/********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2012 Wilmer van der Gaast and others                *
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
  if not, write to the Free Software Foundation, Inc., 51 Franklin St.,
  Fifth Floor, Boston, MA  02110-1301  USA
*/

#include <gnutls/gnutls.h>
#include <gnutls/x509.h>
#include <gcrypt.h>
#include <fcntl.h>
#include <unistd.h>
#include "proxy.h"
#include "ssl_client.h"
#include "sock.h"
#include "stdlib.h"
#include "bitlbee.h"

int ssl_errno = 0;

static gboolean initialized = FALSE;
gnutls_certificate_credentials_t xcred;

#include <limits.h>

#if defined(ULONG_MAX) && ULONG_MAX > 4294967295UL
#define GNUTLS_STUPID_CAST (long)
#else
#define GNUTLS_STUPID_CAST (int)
#endif

#define SSLDEBUG 0

struct scd {
	ssl_input_function func;
	gpointer data;
	int fd;
	gboolean established;
	int inpa;
	char *hostname;
	gboolean verify;

	gnutls_session_t session;
};

static GHashTable *session_cache;

static gboolean ssl_connected(gpointer data, gint source, b_input_condition cond);
static gboolean ssl_starttls_real(gpointer data, gint source, b_input_condition cond);
static gboolean ssl_handshake(gpointer data, gint source, b_input_condition cond);

static void ssl_deinit(void);

static void ssl_log(int level, const char *line)
{
	printf("%d %s", level, line);
}

void ssl_init(void)
{
	if (initialized) {
		return;
	}

	gnutls_global_init();
	gnutls_certificate_allocate_credentials(&xcred);
	if (global.conf->cafile) {
		gnutls_certificate_set_x509_trust_file(xcred, global.conf->cafile, GNUTLS_X509_FMT_PEM);

		/* Not needed in GnuTLS 2.11+ (enabled by default there) so
		   don't do it (resets possible other defaults). */
		if (!gnutls_check_version("2.11")) {
			gnutls_certificate_set_verify_flags(xcred, GNUTLS_VERIFY_ALLOW_X509_V1_CA_CRT);
		}
	}
	initialized = TRUE;

	gnutls_global_set_log_function(ssl_log);
	/*
	gnutls_global_set_log_level( 3 );
	*/

	session_cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

	atexit(ssl_deinit);
}

static void ssl_deinit(void)
{
	gnutls_global_deinit();
	gnutls_certificate_free_credentials(xcred);
	g_hash_table_destroy(session_cache);
	session_cache = NULL;
}

void *ssl_connect(char *host, int port, gboolean verify, ssl_input_function func, gpointer data)
{
	struct scd *conn = g_new0(struct scd, 1);

	conn->func = func;
	conn->data = data;
	conn->inpa = -1;
	conn->hostname = g_strdup(host);
	conn->verify = verify && global.conf->cafile;
	conn->fd = proxy_connect(host, port, ssl_connected, conn);

	if (conn->fd < 0) {
		g_free(conn->hostname);
		g_free(conn);
		return NULL;
	}

	return conn;
}

void *ssl_starttls(int fd, char *hostname, gboolean verify, ssl_input_function func, gpointer data)
{
	struct scd *conn = g_new0(struct scd, 1);

	conn->fd = fd;
	conn->func = func;
	conn->data = data;
	conn->inpa = -1;
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

static gboolean ssl_starttls_real(gpointer data, gint source, b_input_condition cond)
{
	struct scd *conn = data;

	return ssl_connected(conn, conn->fd, B_EV_IO_WRITE);
}

static int verify_certificate_callback(gnutls_session_t session)
{
	unsigned int status;
	const gnutls_datum_t *cert_list;
	unsigned int cert_list_size;
	int gnutlsret;
	int verifyret = 0;
	gnutls_x509_crt_t cert;
	struct scd *conn;

	conn = gnutls_session_get_ptr(session);

	gnutlsret = gnutls_certificate_verify_peers2(session, &status);
	if (gnutlsret < 0) {
		return VERIFY_CERT_ERROR;
	}

	if (status & GNUTLS_CERT_INVALID) {
		verifyret |= VERIFY_CERT_INVALID;
	}

	if (status & GNUTLS_CERT_REVOKED) {
		verifyret |= VERIFY_CERT_REVOKED;
	}

	if (status & GNUTLS_CERT_SIGNER_NOT_FOUND) {
		verifyret |= VERIFY_CERT_SIGNER_NOT_FOUND;
	}

	if (status & GNUTLS_CERT_SIGNER_NOT_CA) {
		verifyret |= VERIFY_CERT_SIGNER_NOT_CA;
	}

	if (status & GNUTLS_CERT_INSECURE_ALGORITHM) {
		verifyret |= VERIFY_CERT_INSECURE_ALGORITHM;
	}

#ifdef GNUTLS_CERT_NOT_ACTIVATED
	/* Amusingly, the GnuTLS function used above didn't check for expiry
	   until GnuTLS 2.8 or so. (See CVE-2009-1417) */
	if (status & GNUTLS_CERT_NOT_ACTIVATED) {
		verifyret |= VERIFY_CERT_NOT_ACTIVATED;
	}

	if (status & GNUTLS_CERT_EXPIRED) {
		verifyret |= VERIFY_CERT_EXPIRED;
	}
#endif

	if (gnutls_certificate_type_get(session) != GNUTLS_CRT_X509 || gnutls_x509_crt_init(&cert) < 0) {
		return VERIFY_CERT_ERROR;
	}

	cert_list = gnutls_certificate_get_peers(session, &cert_list_size);
	if (cert_list == NULL || gnutls_x509_crt_import(cert, &cert_list[0], GNUTLS_X509_FMT_DER) < 0) {
		return VERIFY_CERT_ERROR;
	}

	if (!gnutls_x509_crt_check_hostname(cert, conn->hostname)) {
		verifyret |= VERIFY_CERT_INVALID;
		verifyret |= VERIFY_CERT_WRONG_HOSTNAME;
	}

	gnutls_x509_crt_deinit(cert);

	return verifyret;
}

struct ssl_session {
	size_t size;
	char data[];
};

static void ssl_cache_add(struct scd *conn)
{
	size_t data_size = 0;
	struct ssl_session *data;
	char *hostname;

	if (!conn->hostname ||
	    gnutls_session_get_data(conn->session, NULL, &data_size) != 0) {
		return;
	}

	data = g_malloc(sizeof(struct ssl_session) + data_size);
	if (gnutls_session_get_data(conn->session, data->data, &data_size) != 0) {
		g_free(data);
		return;
	}

	hostname = g_strdup(conn->hostname);
	g_hash_table_insert(session_cache, hostname, data);
}

static void ssl_cache_resume(struct scd *conn)
{
	struct ssl_session *data;

	if (conn->hostname &&
	    (data = g_hash_table_lookup(session_cache, conn->hostname))) {
		gnutls_session_set_data(conn->session, data->data, data->size);
		g_hash_table_remove(session_cache, conn->hostname);
	}
}

char *ssl_verify_strerror(int code)
{
	GString *ret = g_string_new("");

	if (code & VERIFY_CERT_REVOKED) {
		g_string_append(ret, "certificate has been revoked, ");
	}
	if (code & VERIFY_CERT_SIGNER_NOT_FOUND) {
		g_string_append(ret, "certificate hasn't got a known issuer, ");
	}
	if (code & VERIFY_CERT_SIGNER_NOT_CA) {
		g_string_append(ret, "certificate's issuer is not a CA, ");
	}
	if (code & VERIFY_CERT_INSECURE_ALGORITHM) {
		g_string_append(ret, "certificate uses an insecure algorithm, ");
	}
	if (code & VERIFY_CERT_NOT_ACTIVATED) {
		g_string_append(ret, "certificate has not been activated, ");
	}
	if (code & VERIFY_CERT_EXPIRED) {
		g_string_append(ret, "certificate has expired, ");
	}
	if (code & VERIFY_CERT_WRONG_HOSTNAME) {
		g_string_append(ret, "certificate hostname mismatch, ");
	}

	if (ret->len == 0) {
		g_string_free(ret, TRUE);
		return NULL;
	} else {
		g_string_truncate(ret, ret->len - 2);
		return g_string_free(ret, FALSE);
	}
}

static gboolean ssl_connected(gpointer data, gint source, b_input_condition cond)
{
	struct scd *conn = data;

	if (source == -1) {
		conn->func(conn->data, 0, NULL, cond);
		g_free(conn->hostname);
		g_free(conn);
		return FALSE;
	}

	ssl_init();

	gnutls_init(&conn->session, GNUTLS_CLIENT);
	gnutls_session_set_ptr(conn->session, (void *) conn);
#if GNUTLS_VERSION_NUMBER < 0x020c00
	gnutls_transport_set_lowat(conn->session, 0);
#endif
	gnutls_set_default_priority(conn->session);
	gnutls_credentials_set(conn->session, GNUTLS_CRD_CERTIFICATE, xcred);
	if (conn->hostname && !g_ascii_isdigit(conn->hostname[0])) {
		gnutls_server_name_set(conn->session, GNUTLS_NAME_DNS,
		                       conn->hostname, strlen(conn->hostname));
	}

	sock_make_nonblocking(conn->fd);
	gnutls_transport_set_ptr(conn->session, (gnutls_transport_ptr_t) GNUTLS_STUPID_CAST conn->fd);

	ssl_cache_resume(conn);

	return ssl_handshake(data, source, cond);
}

static gboolean ssl_handshake(gpointer data, gint source, b_input_condition cond)
{
	struct scd *conn = data;
	int st, stver;

	/* This function returns false, so avoid calling b_event_remove again */
	conn->inpa = -1;

	if ((st = gnutls_handshake(conn->session)) < 0) {
		if (st == GNUTLS_E_AGAIN || st == GNUTLS_E_INTERRUPTED) {
			conn->inpa = b_input_add(conn->fd, ssl_getdirection(conn),
			                         ssl_handshake, data);
		} else {
			conn->func(conn->data, 0, NULL, cond);

			ssl_disconnect(conn);
		}
	} else {
		if (conn->verify && (stver = verify_certificate_callback(conn->session)) != 0) {
			conn->func(conn->data, stver, NULL, cond);

			ssl_disconnect(conn);
		} else {
			/* For now we can't handle non-blocking perfectly everywhere... */
			sock_make_blocking(conn->fd);

			ssl_cache_add(conn);
			conn->established = TRUE;
			conn->func(conn->data, 0, conn, cond);
		}
	}

	return FALSE;
}

int ssl_read(void *conn, char *buf, int len)
{
	int st;

	if (!((struct scd*) conn)->established) {
		ssl_errno = SSL_NOHANDSHAKE;
		return -1;
	}

	st = gnutls_record_recv(((struct scd*) conn)->session, buf, len);

	ssl_errno = SSL_OK;
	if (st == GNUTLS_E_AGAIN || st == GNUTLS_E_INTERRUPTED) {
		ssl_errno = SSL_AGAIN;
	}

	if (SSLDEBUG && getenv("BITLBEE_DEBUG") && st > 0) {
		len = write(2, buf, st);
	}

	return st;
}

int ssl_write(void *conn, const char *buf, int len)
{
	int st;

	if (!((struct scd*) conn)->established) {
		ssl_errno = SSL_NOHANDSHAKE;
		return -1;
	}

	st = gnutls_record_send(((struct scd*) conn)->session, buf, len);

	ssl_errno = SSL_OK;
	if (st == GNUTLS_E_AGAIN || st == GNUTLS_E_INTERRUPTED) {
		ssl_errno = SSL_AGAIN;
	}

	if (SSLDEBUG && getenv("BITLBEE_DEBUG") && st > 0) {
		len = write(2, buf, st);
	}

	return st;
}

int ssl_pending(void *conn)
{
	if (conn == NULL) {
		return 0;
	}

	if (!((struct scd*) conn)->established) {
		ssl_errno = SSL_NOHANDSHAKE;
		return 0;
	}

#if GNUTLS_VERSION_NUMBER >= 0x03000d && GNUTLS_VERSION_NUMBER <= 0x030012
	if (ssl_errno == SSL_AGAIN) {
		return 0;
	}
#endif

	return gnutls_record_check_pending(((struct scd*) conn)->session) != 0;
}

void ssl_disconnect(void *conn_)
{
	struct scd *conn = conn_;

	if (conn->inpa != -1) {
		b_event_remove(conn->inpa);
	}

	if (conn->established) {
		gnutls_bye(conn->session, GNUTLS_SHUT_WR);
	}

	closesocket(conn->fd);

	if (conn->session) {
		gnutls_deinit(conn->session);
	}
	g_free(conn->hostname);
	g_free(conn);
}

int ssl_getfd(void *conn)
{
	return(((struct scd*) conn)->fd);
}

b_input_condition ssl_getdirection(void *conn)
{
	return(gnutls_record_get_direction(((struct scd*) conn)->session) ?
	       B_EV_IO_WRITE : B_EV_IO_READ);
}

size_t ssl_des3_encrypt(const unsigned char *key, size_t key_len, const unsigned char *input,
                        size_t input_len, const unsigned char *iv, unsigned char **res)
{
	gcry_cipher_hd_t gcr;
	gcry_error_t st;

	ssl_init();

	*res = g_malloc(input_len);
	st = gcry_cipher_open(&gcr, GCRY_CIPHER_3DES, GCRY_CIPHER_MODE_CBC, 0) ||
	     gcry_cipher_setkey(gcr, key, key_len) ||
	     gcry_cipher_setiv(gcr, iv, 8) ||
	     gcry_cipher_encrypt(gcr, *res, input_len, input, input_len);

	gcry_cipher_close(gcr);

	if (st == 0) {
		return input_len;
	}

	g_free(*res);
	return 0;
}
