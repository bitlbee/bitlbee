/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Simple OAuth client (consumer) implementation.                           *
*                                                                           *
*  Copyright 2010 Wilmer van der Gaast <wilmer@gaast.net>                   *
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

#include <glib.h>
#include <gmodule.h>
#include <stdlib.h>
#include <string.h>
#include "http_client.h"
#include "base64.h"
#include "misc.h"
#include "sha1.h"
#include "url.h"
#include "oauth.h"

#define HMAC_BLOCK_SIZE 64

static char *oauth_sign(const char *method, const char *url,
                        const char *params, struct oauth_info *oi)
{
	uint8_t hash[SHA1_HASH_SIZE];
	GString *payload = g_string_new("");
	char *key;
	char *s;

	key = g_strdup_printf("%s&%s", oi->sp->consumer_secret, oi->token_secret ? oi->token_secret : "");

	g_string_append_printf(payload, "%s&", method);

	s = g_new0(char, strlen(url) * 3 + 1);
	strcpy(s, url);
	http_encode(s);
	g_string_append_printf(payload, "%s&", s);
	g_free(s);

	s = g_new0(char, strlen(params) * 3 + 1);
	strcpy(s, params);
	http_encode(s);
	g_string_append(payload, s);
	g_free(s);

	sha1_hmac(key, 0, payload->str, 0, hash);

	g_free(key);
	g_string_free(payload, TRUE);

	/* base64_encode + HTTP escape it (both consumers
	   need it that away) and we're done. */
	s = base64_encode(hash, SHA1_HASH_SIZE);
	s = g_realloc(s, strlen(s) * 3 + 1);
	http_encode(s);

	return s;
}

static char *oauth_nonce()
{
	unsigned char bytes[21];

	random_bytes(bytes, sizeof(bytes));
	return base64_encode(bytes, sizeof(bytes));
}

void oauth_params_add(GSList **params, const char *key, const char *value)
{
	char *item;

	if (!key || !value) {
		return;
	}

	item = g_strdup_printf("%s=%s", key, value);
	*params = g_slist_insert_sorted(*params, item, (GCompareFunc) strcmp);
}

void oauth_params_del(GSList **params, const char *key)
{
	int key_len = strlen(key);
	GSList *l, *n;

	if (!params) {
		return;
	}

	for (l = *params; l; l = n) {
		n = l->next;
		char *data = l->data;

		if (strncmp(data, key, key_len) == 0 && data[key_len] == '=') {
			*params = g_slist_remove(*params, data);
			g_free(data);
		}
	}
}

void oauth_params_set(GSList **params, const char *key, const char *value)
{
	oauth_params_del(params, key);
	oauth_params_add(params, key, value);
}

const char *oauth_params_get(GSList **params, const char *key)
{
	int key_len = strlen(key);
	GSList *l;

	if (params == NULL) {
		return NULL;
	}

	for (l = *params; l; l = l->next) {
		if (strncmp((char *) l->data, key, key_len) == 0 &&
		    ((char *) l->data)[key_len] == '=') {
			return (const char *) l->data + key_len + 1;
		}
	}

	return NULL;
}

void oauth_params_parse(GSList **params, char *in)
{
	char *amp, *eq, *s;

	while (in && *in) {
		eq = strchr(in, '=');
		if (!eq) {
			break;
		}

		*eq = '\0';
		if ((amp = strchr(eq + 1, '&'))) {
			*amp = '\0';
		}

		s = g_strdup(eq + 1);
		http_decode(s);
		oauth_params_add(params, in, s);
		g_free(s);

		*eq = '=';
		if (amp == NULL) {
			break;
		}

		*amp = '&';
		in = amp + 1;
	}
}

void oauth_params_free(GSList **params)
{
	while (params && *params) {
		g_free((*params)->data);
		*params = g_slist_remove(*params, (*params)->data);
	}
}

char *oauth_params_string(GSList *params)
{
	GSList *l;
	GString *str = g_string_new("");

	for (l = params; l; l = l->next) {
		char *s, *eq;

		s = g_malloc(strlen(l->data) * 3 + 1);
		strcpy(s, l->data);
		if ((eq = strchr(s, '='))) {
			http_encode(eq + 1);
		}
		g_string_append(str, s);
		g_free(s);

		if (l->next) {
			g_string_append_c(str, '&');
		}
	}

	return g_string_free(str, FALSE);
}

void oauth_info_free(struct oauth_info *info)
{
	if (info) {
		g_free(info->auth_url);
		g_free(info->request_token);
		g_free(info->token);
		g_free(info->token_secret);
		oauth_params_free(&info->params);
		g_free(info);
	}
}

static void oauth_add_default_params(GSList **params, const struct oauth_service *sp)
{
	char *s;

	oauth_params_set(params, "oauth_consumer_key", sp->consumer_key);
	oauth_params_set(params, "oauth_signature_method", "HMAC-SHA1");

	s = g_strdup_printf("%d", (int) time(NULL));
	oauth_params_set(params, "oauth_timestamp", s);
	g_free(s);

	s = oauth_nonce();
	oauth_params_set(params, "oauth_nonce", s);
	g_free(s);

	oauth_params_set(params, "oauth_version", "1.0");
}

static void *oauth_post_request(const char *url, GSList **params_, http_input_function func, struct oauth_info *oi)
{
	GSList *params = NULL;
	char *s, *params_s, *post;
	void *req;
	url_t url_p;

	if (!url_set(&url_p, url)) {
		oauth_params_free(params_);
		return NULL;
	}

	if (params_) {
		params = *params_;
	}

	oauth_add_default_params(&params, oi->sp);

	params_s = oauth_params_string(params);
	oauth_params_free(&params);

	s = oauth_sign("POST", url, params_s, oi);
	post = g_strdup_printf("%s&oauth_signature=%s", params_s, s);
	g_free(params_s);
	g_free(s);

	s = g_strdup_printf("POST %s HTTP/1.0\r\n"
	                    "Host: %s\r\n"
	                    "Content-Type: application/x-www-form-urlencoded\r\n"
	                    "Content-Length: %zd\r\n"
	                    "\r\n"
	                    "%s", url_p.file, url_p.host, strlen(post), post);
	g_free(post);

	req = http_dorequest(url_p.host, url_p.port, url_p.proto == PROTO_HTTPS,
	                     s, func, oi);
	g_free(s);

	return req;
}

static void oauth_request_token_done(struct http_request *req);

struct oauth_info *oauth_request_token(const struct oauth_service *sp, oauth_cb func, void *data)
{
	struct oauth_info *st = g_new0(struct oauth_info, 1);
	GSList *params = NULL;

	st->func = func;
	st->data = data;
	st->sp = sp;

	oauth_params_add(&params, "oauth_callback", "oob");

	if (!oauth_post_request(sp->url_request_token, &params, oauth_request_token_done, st)) {
		oauth_info_free(st);
		return NULL;
	}

	return st;
}

static void oauth_request_token_done(struct http_request *req)
{
	struct oauth_info *st = req->data;

	st->http = req;

	if (req->status_code == 200) {
		GSList *params = NULL;

		st->auth_url = g_strdup_printf("%s?%s", st->sp->url_authorize, req->reply_body);
		oauth_params_parse(&params, req->reply_body);
		st->request_token = g_strdup(oauth_params_get(&params, "oauth_token"));
		st->token_secret = g_strdup(oauth_params_get(&params, "oauth_token_secret"));
		oauth_params_free(&params);
	}

	st->stage = OAUTH_REQUEST_TOKEN;
	st->func(st);
}

static void oauth_access_token_done(struct http_request *req);

gboolean oauth_access_token(const char *pin, struct oauth_info *st)
{
	GSList *params = NULL;

	oauth_params_add(&params, "oauth_token", st->request_token);
	oauth_params_add(&params, "oauth_verifier", pin);

	return oauth_post_request(st->sp->url_access_token, &params, oauth_access_token_done, st) != NULL;
}

static void oauth_access_token_done(struct http_request *req)
{
	struct oauth_info *st = req->data;

	st->http = req;

	if (req->status_code == 200) {
		oauth_params_parse(&st->params, req->reply_body);
		st->token = g_strdup(oauth_params_get(&st->params, "oauth_token"));
		g_free(st->token_secret);
		st->token_secret = g_strdup(oauth_params_get(&st->params, "oauth_token_secret"));
	}

	st->stage = OAUTH_ACCESS_TOKEN;
	if (st->func(st)) {
		/* Don't need these anymore, but keep the rest. */
		g_free(st->auth_url);
		st->auth_url = NULL;
		g_free(st->request_token);
		st->request_token = NULL;
		oauth_params_free(&st->params);
	}
}

char *oauth_http_header(struct oauth_info *oi, const char *method, const char *url, char *args)
{
	GSList *params = NULL, *l;
	char *sig = NULL, *params_s, *s;
	GString *ret = NULL;

	oauth_params_add(&params, "oauth_token", oi->token);
	oauth_add_default_params(&params, oi->sp);

	/* Start building the OAuth header. 'key="value", '... */
	ret = g_string_new("OAuth ");
	for (l = params; l; l = l->next) {
		char *kv = l->data;
		char *eq = strchr(kv, '=');
		char esc[strlen(kv) * 3 + 1];

		if (eq == NULL) {
			break; /* WTF */

		}
		strcpy(esc, eq + 1);
		http_encode(esc);

		g_string_append_len(ret, kv, eq - kv + 1);
		g_string_append_c(ret, '"');
		g_string_append(ret, esc);
		g_string_append(ret, "\", ");
	}

	/* Now, before generating the signature, add GET/POST arguments to params
	   since they should be included in the base signature string (but not in
	   the HTTP header). */
	if (args) {
		oauth_params_parse(&params, args);
	}
	if ((s = strchr(url, '?'))) {
		s = g_strdup(s + 1);
		oauth_params_parse(&params, s);
		g_free(s);
	}

	/* Append the signature and we're done! */
	params_s = oauth_params_string(params);
	sig = oauth_sign(method, url, params_s, oi);
	g_string_append_printf(ret, "oauth_signature=\"%s\"", sig);
	g_free(params_s);

	oauth_params_free(&params);
	g_free(sig);

	return ret ? g_string_free(ret, FALSE) : NULL;
}

char *oauth_to_string(struct oauth_info *oi)
{
	GSList *params = NULL;
	char *ret;

	oauth_params_add(&params, "oauth_token", oi->token);
	oauth_params_add(&params, "oauth_token_secret", oi->token_secret);
	ret = oauth_params_string(params);
	oauth_params_free(&params);

	return ret;
}

struct oauth_info *oauth_from_string(char *in, const struct oauth_service *sp)
{
	struct oauth_info *oi = g_new0(struct oauth_info, 1);
	GSList *params = NULL;

	oauth_params_parse(&params, in);
	oi->token = g_strdup(oauth_params_get(&params, "oauth_token"));
	oi->token_secret = g_strdup(oauth_params_get(&params, "oauth_token_secret"));
	oauth_params_free(&params);
	oi->sp = sp;

	return oi;
}
