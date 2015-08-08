/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Simple module to facilitate twitter functionality.                       *
*                                                                           *
*  Copyright 2009 Geert Mulders <g.c.w.m.mulders@gmail.com>                 *
*                                                                           *
*  This library is free software; you can redistribute it and/or            *
*  modify it under the terms of the GNU Lesser General Public               *
*  License as published by the Free Software Foundation, version            *
*  2.1.                                                                     *
*                                                                           *
*  This library is distributed in the hope that it will be useful,          *
*  but WITHOUT ANY WARRANTY; without even the implied warranty of           *
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU        *
*  Lesser General Public License for more details.                          *
*                                                                           *
*  You should have received a copy of the GNU Lesser General Public License *
*  along with this library; if not, write to the Free Software Foundation,  *
*  Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA           *
*                                                                           *
****************************************************************************/

/***************************************************************************\
*                                                                           *
*  Some functions within this file have been copied from other files within  *
*  BitlBee.                                                                 *
*                                                                           *
****************************************************************************/

#include "twitter.h"
#include "bitlbee.h"
#include "url.h"
#include "misc.h"
#include "base64.h"
#include "oauth.h"
#include <ctype.h>
#include <errno.h>

#include "twitter_http.h"


static char *twitter_url_append(char *url, char *key, char *value);

/**
 * Do a request.
 * This is actually pretty generic function... Perhaps it should move to the lib/http_client.c
 */
struct http_request *twitter_http(struct im_connection *ic, char *url_string, http_input_function func,
                                  gpointer data, int is_post, char **arguments, int arguments_len)
{
	struct twitter_data *td = ic->proto_data;
	char *tmp;
	GString *request = g_string_new("");
	void *ret;
	char *url_arguments;
	url_t *base_url = NULL;

	url_arguments = g_strdup("");

	// Construct the url arguments.
	if (arguments_len != 0) {
		int i;
		for (i = 0; i < arguments_len; i += 2) {
			tmp = twitter_url_append(url_arguments, arguments[i], arguments[i + 1]);
			g_free(url_arguments);
			url_arguments = tmp;
		}
	}

	if (strstr(url_string, "://")) {
		base_url = g_new0(url_t, 1);
		if (!url_set(base_url, url_string)) {
			g_free(base_url);
			return NULL;
		}
	}

	// Make the request.
	g_string_printf(request, "%s %s%s%s%s HTTP/1.1\r\n"
	                "Host: %s\r\n"
	                "User-Agent: BitlBee " BITLBEE_VERSION " " ARCH "/" CPU "\r\n",
	                is_post ? "POST" : "GET",
	                base_url ? base_url->file : td->url_path,
	                base_url ? "" : url_string,
	                is_post ? "" : "?", is_post ? "" : url_arguments,
	                base_url ? base_url->host : td->url_host);

	// If a pass and user are given we append them to the request.
	if (td->oauth_info) {
		char *full_header;
		char *full_url;

		if (base_url) {
			full_url = g_strdup(url_string);
		} else {
			full_url = g_strconcat(set_getstr(&ic->acc->set, "base_url"), url_string, NULL);
		}
		full_header = oauth_http_header(td->oauth_info, is_post ? "POST" : "GET",
		                                full_url, url_arguments);

		g_string_append_printf(request, "Authorization: %s\r\n", full_header);
		g_free(full_header);
		g_free(full_url);
	} else {
		char userpass[strlen(ic->acc->user) + 2 + strlen(ic->acc->pass)];
		char *userpass_base64;

		g_snprintf(userpass, sizeof(userpass), "%s:%s", ic->acc->user, ic->acc->pass);
		userpass_base64 = base64_encode((unsigned char *) userpass, strlen(userpass));
		g_string_append_printf(request, "Authorization: Basic %s\r\n", userpass_base64);
		g_free(userpass_base64);
	}

	// Do POST stuff..
	if (is_post) {
		// Append the Content-Type and url-encoded arguments.
		g_string_append_printf(request,
		                       "Content-Type: application/x-www-form-urlencoded\r\n"
		                       "Content-Length: %zd\r\n\r\n%s",
		                       strlen(url_arguments), url_arguments);
	} else {
		// Append an extra \r\n to end the request...
		g_string_append(request, "\r\n");
	}

	if (base_url) {
		ret = http_dorequest(base_url->host, base_url->port, base_url->proto == PROTO_HTTPS, request->str, func,
		                     data);
	} else {
		ret = http_dorequest(td->url_host, td->url_port, td->url_ssl, request->str, func, data);
	}

	g_free(url_arguments);
	g_string_free(request, TRUE);
	g_free(base_url);
	return ret;
}

struct http_request *twitter_http_f(struct im_connection *ic, char *url_string, http_input_function func,
                                    gpointer data, int is_post, char **arguments, int arguments_len,
                                    twitter_http_flags_t flags)
{
	struct http_request *ret = twitter_http(ic, url_string, func, data, is_post, arguments, arguments_len);

	if (ret) {
		ret->flags |= flags;
	}
	return ret;
}

static char *twitter_url_append(char *url, char *key, char *value)
{
	char *key_encoded = g_strndup(key, 3 * strlen(key));

	http_encode(key_encoded);
	char *value_encoded = g_strndup(value, 3 * strlen(value));
	http_encode(value_encoded);

	char *retval;
	if (strlen(url) != 0) {
		retval = g_strdup_printf("%s&%s=%s", url, key_encoded, value_encoded);
	} else {
		retval = g_strdup_printf("%s=%s", key_encoded, value_encoded);
	}

	g_free(key_encoded);
	g_free(value_encoded);

	return retval;
}
