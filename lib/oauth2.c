/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Simple OAuth2 client (consumer) implementation.                          *
*                                                                           *
*  Copyright 2010-2013 Wilmer van der Gaast <wilmer@gaast.net>              *
*                                                                           *
*  This program is free software; you can redistribute it and/or modify     *
*  it under the terms of the GNU General Public License as published by     *
*  the Free Software Foundation; either version 2 of the License, or        *
*  (at your option) any later version.                                      *
*                                                                           *
*  This program is distributed in the hope that it will be useful,          *
*  but WITHOUT ANY WARRANTY; without even the implied warranty of           *
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
*  GNU General Public License for more details.                             *
*                                                                           *
*  You should have received a copy of the GNU General Public License along  *
*  with this program; if not, write to the Free Software Foundation, Inc.,  *
*  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.              *
*                                                                           *
\***************************************************************************/

/* Out of protest, I should rename this file. OAuth2 is a pathetic joke, and
   of all things, DEFINITELY NOT A STANDARD. The only thing various OAuth2
   implementations have in common is that name, wrongfully stolen from
   a pretty nice standard called OAuth 1.0a. That, and the fact that they
   use JSON. Wait, no, Facebook's version doesn't use JSON. For some of its
   responses.

   Apparently too many people were too retarded to comprehend the elementary
   bits of crypto in OAuth 1.0a (took me one afternoon to implement) so
   the standard was replaced with what comes down to a complicated scheme
   around what's really just application-specific passwords.

   And then a bunch of mostly incompatible implementations. Great work, guys.

   http://hueniverse.com/2012/07/oauth-2-0-and-the-road-to-hell/ */

#include <glib.h>
#include "http_client.h"
#include "oauth2.h"
#include "oauth.h"
#include "json.h"
#include "json_util.h"
#include "url.h"

char *oauth2_url(const struct oauth2_service *sp)
{
	return g_strconcat(sp->auth_url,
	                   "?scope=", sp->scope,
	                   "&response_type=code"
	                   "&redirect_uri=", sp->redirect_url,
	                   "&client_id=", sp->consumer_key,
	                   NULL);
}

struct oauth2_access_token_data {
	oauth2_token_callback func;
	gpointer data;
};

static void oauth2_access_token_done(struct http_request *req);

int oauth2_access_token(const struct oauth2_service *sp,
                        const char *auth_type, const char *auth,
                        oauth2_token_callback func, gpointer data)
{
	GSList *args = NULL;
	char *args_s, *s;
	url_t url_p;
	struct http_request *req;
	struct oauth2_access_token_data *cb_data;

	if (!url_set(&url_p, sp->token_url)) {
		return 0;
	}

	oauth_params_add(&args, "client_id", sp->consumer_key);
	oauth_params_add(&args, "client_secret", sp->consumer_secret);
	oauth_params_add(&args, "grant_type", auth_type);
	if (strcmp(auth_type, OAUTH2_AUTH_CODE) == 0) {
		oauth_params_add(&args, "redirect_uri", sp->redirect_url);
		oauth_params_add(&args, "code", auth);
	} else {
		oauth_params_add(&args, "refresh_token", auth);
	}
	args_s = oauth_params_string(args);
	oauth_params_free(&args);

	s = g_strdup_printf("POST %s HTTP/1.0\r\n"
	                    "Host: %s\r\n"
	                    "Content-Type: application/x-www-form-urlencoded\r\n"
	                    "Content-Length: %zd\r\n"
	                    "\r\n"
	                    "%s", url_p.file, url_p.host, strlen(args_s), args_s);
	g_free(args_s);

	cb_data = g_new0(struct oauth2_access_token_data, 1);
	cb_data->func = func;
	cb_data->data = data;

	req = http_dorequest(url_p.host, url_p.port, url_p.proto == PROTO_HTTPS,
	                     s, oauth2_access_token_done, cb_data);

	g_free(s);

	if (req == NULL) {
		g_free(cb_data);
	}

	return req != NULL;
}

static char* oauth2_parse_error(json_value *e)
{
	/* This does a reasonable job with some of the flavours of error
	   responses I've seen. Because apparently it's not standardised. */

	if (e->type == json_object) {
		/* Facebook style */
		const char *msg = json_o_str(e, "message");
		const char *type = json_o_str(e, "type");
		json_value *code_o = json_o_get(e, "code");
		int code = 0;

		if (code_o && code_o->type == json_integer) {
			code = code_o->u.integer;
		}

		return g_strdup_printf("Error %d: %s", code, msg ? msg : type ? type : "Unknown error");
	} else if (e->type == json_string) {
		return g_strdup(e->u.string.ptr);
	}
	return NULL;
}

static void oauth2_access_token_done(struct http_request *req)
{
	struct oauth2_access_token_data *cb_data = req->data;
	char *atoken = NULL, *rtoken = NULL, *error = NULL;
	char *content_type = NULL;

	if (req->status_code <= 0 && !req->reply_body) {
		cb_data->func(cb_data->data, NULL, NULL, req->status_string);
		g_free(cb_data);
		return;
	}

	if (getenv("BITLBEE_DEBUG")) {
		printf("%s\n", req->reply_body);
	}

	content_type = get_rfc822_header(req->reply_headers, "Content-Type", 0);

	if (content_type && (strstr(content_type, "application/json") ||
	                     strstr(content_type, "text/javascript"))) {
		json_value *js = json_parse(req->reply_body, req->body_size);
		if (js && js->type == json_object) {
			JSON_O_FOREACH(js, k, v){
				if (strcmp(k, "error") == 0) {
					error = oauth2_parse_error(v);
				}
				if (v->type != json_string) {
					continue;
				}
				if (strcmp(k, "access_token") == 0) {
					atoken = g_strdup(v->u.string.ptr);
				}
				if (strcmp(k, "refresh_token") == 0) {
					rtoken = g_strdup(v->u.string.ptr);
				}
			}
		}
		json_value_free(js);
	} else {
		/* Facebook use their own odd format here, seems to be URL-encoded. */
		GSList *p_in = NULL;

		oauth_params_parse(&p_in, req->reply_body);
		atoken = g_strdup(oauth_params_get(&p_in, "access_token"));
		rtoken = g_strdup(oauth_params_get(&p_in, "refresh_token"));
		oauth_params_free(&p_in);
	}
	if (getenv("BITLBEE_DEBUG")) {
		printf("Extracted atoken=%s rtoken=%s\n", atoken, rtoken);
	}
	if (!atoken && !rtoken && !error) {
		error = g_strdup("Unusable response");
	}

	cb_data->func(cb_data->data, atoken, rtoken, error);
	g_free(content_type);
	g_free(atoken);
	g_free(rtoken);
	g_free(error);
	g_free(cb_data);
}
