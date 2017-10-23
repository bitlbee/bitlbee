/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Simple module to facilitate Mastodon functionality.                      *
*                                                                           *
*  Copyright 2009 Geert Mulders <g.c.w.m.mulders@gmail.com>                 *
*  Copyright 2017 Alex Schroeder <alex@gnu.org>                             *
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

#include "mastodon.h"
#include "bitlbee.h"
#include "url.h"
#include "misc.h"
#include "base64.h"
#include "oauth.h"
#include <ctype.h>
#include <errno.h>

#include "mastodon_http.h"


static char *mastodon_url_append(char *url, char *key, char *value)
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

/**
 * Do a request.
 * This is actually pretty generic function... Perhaps it should move to the lib/http_client.c
 */
struct http_request *mastodon_http(struct im_connection *ic, char *url_string, http_input_function func,
                                  gpointer data, http_method_t method, char **arguments, int arguments_len)
{
	struct mastodon_data *md = ic->proto_data;
	void *ret = NULL;

	char *url_arguments = g_strdup("");

	char *request_method;
	switch (method) {
	case HTTP_GET:
		request_method = "GET";
		break;
	case HTTP_POST:
		request_method = "POST";
		break;
	case HTTP_DELETE:
		request_method = "DELETE";
		break;
	}
	
	// Construct the url arguments.
	if (arguments_len != 0) {
		int i;
		for (i = 0; i < arguments_len; i += 2) {
			char *tmp = mastodon_url_append(url_arguments, arguments[i], arguments[i + 1]);
			g_free(url_arguments);
			url_arguments = tmp;
		}
	}

	url_t *base_url = NULL;
	if (strstr(url_string, "://")) {
		base_url = g_new0(url_t, 1);
		if (!url_set(base_url, url_string)) {
			goto error;
		}
	}

	// Make the request.
	GString *request = g_string_new("");
	g_string_printf(request, "%s %s%s%s%s HTTP/1.1\r\n"
	                "Host: %s\r\n"
	                "User-Agent: BitlBee " BITLBEE_VERSION "\r\n"
			"Authorization: Bearer %s\r\n",
	                request_method,
	                base_url ? base_url->file : md->url_path,
	                base_url ? "" : url_string,
	                method == HTTP_GET && url_arguments[0] ? "?" : "",
			method == HTTP_GET && url_arguments[0] ? url_arguments : "",
	                base_url ? base_url->host : md->url_host,
			md->oauth2_access_token);

	// Do POST stuff..
	if (method != HTTP_GET) {
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
		ret = http_dorequest(md->url_host, md->url_port, md->url_ssl, request->str, func, data);
	}

	g_string_free(request, TRUE);
error:
	g_free(url_arguments);
	g_free(base_url);
	return ret;
}
