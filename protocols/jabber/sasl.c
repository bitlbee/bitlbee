/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Jabber module - SASL authentication                                      *
*                                                                           *
*  Copyright 2006-2012 Wilmer van der Gaast <wilmer@gaast.net>              *
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

#include <ctype.h>

#include "jabber.h"
#include "base64.h"
#include "oauth2.h"
#include "oauth.h"

const struct oauth2_service oauth2_service_google =
{
	"https://accounts.google.com/o/oauth2/auth",
	"https://accounts.google.com/o/oauth2/token",
	"urn:ietf:wg:oauth:2.0:oob",
	"https://www.googleapis.com/auth/googletalk",
	"783993391592.apps.googleusercontent.com",
	"6C-Zgf7Tr7gEQTPlBhMUgo7R",
};
const struct oauth2_service oauth2_service_facebook =
{
	"https://www.facebook.com/dialog/oauth",
	"https://graph.facebook.com/oauth/access_token",
	"https://www.bitlbee.org/main.php/Facebook/oauth2.html",
	"offline_access,xmpp_login",
	"126828914005625",
	"4b100f0f244d620bf3f15f8b217d4c32",
};

xt_status sasl_pkt_mechanisms(struct xt_node *node, gpointer data)
{
	struct im_connection *ic = data;
	struct jabber_data *jd = ic->proto_data;
	struct xt_node *c, *reply;
	char *s;
	int sup_plain = 0, sup_digest = 0, sup_gtalk = 0, sup_fb = 0, sup_anonymous = 0;
	int want_oauth = FALSE, want_hipchat = FALSE, want_anonymous = FALSE;
	GString *mechs;

	if (!sasl_supported(ic)) {
		/* Should abort this now, since we should already be doing
		   IQ authentication. Strange things happen when you try
		   to do both... */
		imcb_log(ic,
		         "XMPP 1.0 non-compliant server seems to support SASL, please report this as a BitlBee bug!");
		return XT_HANDLED;
	}

	s = xt_find_attr(node, "xmlns");
	if (!s || strcmp(s, XMLNS_SASL) != 0) {
		imcb_log(ic, "Stream error while authenticating");
		imc_logout(ic, FALSE);
		return XT_ABORT;
	}

	want_anonymous = set_getbool(&ic->acc->set, "anonymous");
	want_oauth = set_getbool(&ic->acc->set, "oauth");
	want_hipchat = (jd->flags & JFLAG_HIPCHAT);

	mechs = g_string_new("");
	c = node->children;
	while ((c = xt_find_node(c, "mechanism"))) {
		if (c->text && g_strcasecmp(c->text, "PLAIN") == 0) {
			sup_plain = 1;
		} else if (c->text && g_strcasecmp(c->text, "DIGEST-MD5") == 0) {
			sup_digest = 1;
		} else if (c->text && g_strcasecmp(c->text, "ANONYMOUS") == 0) {
			sup_anonymous = 1;
		} else if (c->text && g_strcasecmp(c->text, "X-OAUTH2") == 0) {
			sup_gtalk = 1;
		} else if (c->text && g_strcasecmp(c->text, "X-FACEBOOK-PLATFORM") == 0) {
			sup_fb = 1;
		}

		if (c->text) {
			g_string_append_printf(mechs, " %s", c->text);
		}

		c = c->next;
	}

	if (!want_oauth && !sup_plain && !sup_digest) {
		if (!sup_gtalk && !sup_fb) {
			imcb_error(ic, "This server requires OAuth "
			           "(supported schemes:%s)", mechs->str);
		} else {
			imcb_error(ic, "BitlBee does not support any of the offered SASL "
			           "authentication schemes:%s", mechs->str);
		}
		imc_logout(ic, FALSE);
		g_string_free(mechs, TRUE);
		return XT_ABORT;
	}
	g_string_free(mechs, TRUE);

	reply = xt_new_node("auth", NULL, NULL);
	if (!want_hipchat) {
		xt_add_attr(reply, "xmlns", XMLNS_SASL);
	} else {
		xt_add_attr(reply, "xmlns", XMLNS_HIPCHAT);
	}

	if (sup_gtalk && want_oauth) {
		int len;

		/* X-OAUTH2 is, not *the* standard OAuth2 SASL/XMPP implementation.
		   It's currently used by GTalk and vaguely documented on
		   http://code.google.com/apis/cloudprint/docs/rawxmpp.html . */
		xt_add_attr(reply, "mechanism", "X-OAUTH2");

		len = strlen(jd->username) + strlen(jd->oauth2_access_token) + 2;
		s = g_malloc(len + 1);
		s[0] = 0;
		strcpy(s + 1, jd->username);
		strcpy(s + 2 + strlen(jd->username), jd->oauth2_access_token);
		reply->text = base64_encode((unsigned char *) s, len);
		reply->text_len = strlen(reply->text);
		g_free(s);
	} else if (sup_fb && want_oauth) {
		xt_add_attr(reply, "mechanism", "X-FACEBOOK-PLATFORM");
		jd->flags |= JFLAG_SASL_FB;
	} else if (want_oauth) {
		imcb_error(ic, "OAuth requested, but not supported by server");
		imc_logout(ic, FALSE);
		xt_free_node(reply);
		return XT_ABORT;
	} else if (want_anonymous && sup_anonymous) {
		xt_add_attr(reply, "mechanism", "ANONYMOUS");

		/* Well, that was easy. */
	} else if (want_anonymous) {
		imcb_error(ic, "Anonymous login requested, but not supported by server");
		imc_logout(ic, FALSE);
		xt_free_node(reply);
		return XT_ABORT;
	} else if (sup_digest && !(jd->ssl && sup_plain)) {
		/* Only try DIGEST-MD5 if there's no SSL/TLS or if PLAIN isn't supported.
		 * Which in practice means "don't bother with DIGEST-MD5 most of the time".
		 * It's weak, pointless over TLS, and often breaks with some servers (hi openfire) */

		xt_add_attr(reply, "mechanism", "DIGEST-MD5");

		/* The rest will be done later, when we receive a <challenge/>. */
	} else if (sup_plain) {
		int len;
		GString *gs;
		char *username;

		if (!want_hipchat) {
			xt_add_attr(reply, "mechanism", "PLAIN");
			username = jd->username;
		} else {
			username = jd->me;
		}

		/* set an arbitrary initial size to avoid reallocations */
		gs = g_string_sized_new(128);

		/* With SASL PLAIN in XMPP, the text should be b64(\0user\0pass) */
		g_string_append_c(gs, '\0');
		g_string_append(gs, username);
		g_string_append_c(gs, '\0');
		g_string_append(gs, ic->acc->pass);
		if (want_hipchat) {
			/* Hipchat's variation adds \0resource at the end */
			g_string_append_c(gs, '\0');
			g_string_append(gs, set_getstr(&ic->acc->set, "resource"));
		}

		len = gs->len;
		s = g_string_free(gs, FALSE);

		reply->text = base64_encode((unsigned char *) s, len);
		reply->text_len = strlen(reply->text);
		g_free(s);
	}

	if (reply && !jabber_write_packet(ic, reply)) {
		xt_free_node(reply);
		return XT_ABORT;
	}
	xt_free_node(reply);

	/* To prevent classic authentication from happening. */
	jd->flags |= JFLAG_STREAM_STARTED;

	return XT_HANDLED;
}

/* Non-static function, but not mentioned in jabber.h because it's for internal
   use, just that the unittest should be able to reach it... */
char *sasl_get_part(char *data, char *field)
{
	int i, len;

	len = strlen(field);

	while (g_ascii_isspace(*data) || *data == ',') {
		data++;
	}

	if (g_strncasecmp(data, field, len) == 0 && data[len] == '=') {
		i = strlen(field) + 1;
	} else {
		for (i = 0; data[i]; i++) {
			/* If we have a ", skip until it's closed again. */
			if (data[i] == '"') {
				i++;
				while (data[i] != '"' || data[i - 1] == '\\') {
					i++;
				}
			}

			/* If we got a comma, we got a new field. Check it,
			   find the next key after it. */
			if (data[i] == ',') {
				while (g_ascii_isspace(data[i]) || data[i] == ',') {
					i++;
				}

				if (g_strncasecmp(data + i, field, len) == 0 &&
				    data[i + len] == '=') {
					i += len + 1;
					break;
				}
			}
		}
	}

	if (data[i] == '"') {
		int j;
		char *ret;

		i++;
		len = 0;
		while (data[i + len] != '"' || data[i + len - 1] == '\\') {
			len++;
		}

		ret = g_strndup(data + i, len);
		for (i = j = 0; ret[i]; i++) {
			if (ret[i] == '\\') {
				ret[j++] = ret[++i];
			} else {
				ret[j++] = ret[i];
			}
		}
		ret[j] = 0;

		return ret;
	} else if (data[i]) {
		len = 0;
		while (data[i + len] && data[i + len] != ',') {
			len++;
		}

		return g_strndup(data + i, len);
	} else {
		return NULL;
	}
}

xt_status sasl_pkt_challenge(struct xt_node *node, gpointer data)
{
	struct im_connection *ic = data;
	struct jabber_data *jd = ic->proto_data;
	struct xt_node *reply_pkt = NULL;
	char *nonce = NULL, *realm = NULL, *cnonce = NULL;
	unsigned char cnonce_bin[30];
	char *digest_uri = NULL;
	char *dec = NULL;
	char *s = NULL, *reply = NULL;
	xt_status ret = XT_ABORT;

	if (node->text_len == 0) {
		goto error;
	}

	dec = frombase64(node->text);

	if (jd->flags & JFLAG_SASL_FB) {
		/* New-style Facebook OAauth2 support. Instead of sending a refresh
		   token, they just send an access token that should never expire. */
		GSList *p_in = NULL, *p_out = NULL;
		char time[33];

		oauth_params_parse(&p_in, dec);
		oauth_params_add(&p_out, "nonce", oauth_params_get(&p_in, "nonce"));
		oauth_params_add(&p_out, "method", oauth_params_get(&p_in, "method"));
		oauth_params_free(&p_in);

		g_snprintf(time, sizeof(time), "%lld", (long long) (gettime() * 1000));
		oauth_params_add(&p_out, "call_id", time);
		oauth_params_add(&p_out, "api_key", oauth2_service_facebook.consumer_key);
		oauth_params_add(&p_out, "v", "1.0");
		oauth_params_add(&p_out, "format", "XML");
		oauth_params_add(&p_out, "access_token", jd->oauth2_access_token);

		reply = oauth_params_string(p_out);
		oauth_params_free(&p_out);
	} else if (!(s = sasl_get_part(dec, "rspauth"))) {
		/* See RFC 2831 for for information. */
		md5_state_t A1, A2, H;
		md5_byte_t A1r[16], A2r[16], Hr[16];
		char A1h[33], A2h[33], Hh[33];
		int i;

		nonce = sasl_get_part(dec, "nonce");
		realm = sasl_get_part(dec, "realm");

		if (!nonce) {
			goto error;
		}

		/* Jabber.Org considers the realm part optional and doesn't
		   specify one. Oh well, actually they're right, but still,
		   don't know if this is right... */
		if (!realm) {
			realm = g_strdup(jd->server);
		}

		random_bytes(cnonce_bin, sizeof(cnonce_bin));
		cnonce = base64_encode(cnonce_bin, sizeof(cnonce_bin));
		digest_uri = g_strdup_printf("%s/%s", "xmpp", jd->server);

		/* Generate the MD5 hash of username:realm:password,
		   I decided to call it H. */
		md5_init(&H);
		s = g_strdup_printf("%s:%s:%s", jd->username, realm, ic->acc->pass);
		md5_append(&H, (unsigned char *) s, strlen(s));
		g_free(s);
		md5_finish(&H, Hr);

		/* Now generate the hex. MD5 hash of H:nonce:cnonce, called A1. */
		md5_init(&A1);
		s = g_strdup_printf(":%s:%s", nonce, cnonce);
		md5_append(&A1, Hr, 16);
		md5_append(&A1, (unsigned char *) s, strlen(s));
		g_free(s);
		md5_finish(&A1, A1r);
		for (i = 0; i < 16; i++) {
			sprintf(A1h + i * 2, "%02x", A1r[i]);
		}

		/* A2... */
		md5_init(&A2);
		s = g_strdup_printf("%s:%s", "AUTHENTICATE", digest_uri);
		md5_append(&A2, (unsigned char *) s, strlen(s));
		g_free(s);
		md5_finish(&A2, A2r);
		for (i = 0; i < 16; i++) {
			sprintf(A2h + i * 2, "%02x", A2r[i]);
		}

		/* Final result: A1:nonce:00000001:cnonce:auth:A2. Let's reuse H for it. */
		md5_init(&H);
		s = g_strdup_printf("%s:%s:%s:%s:%s:%s", A1h, nonce, "00000001", cnonce, "auth", A2h);
		md5_append(&H, (unsigned char *) s, strlen(s));
		g_free(s);
		md5_finish(&H, Hr);
		for (i = 0; i < 16; i++) {
			sprintf(Hh + i * 2, "%02x", Hr[i]);
		}

		/* Now build the SASL response string: */
		reply = g_strdup_printf("username=\"%s\",realm=\"%s\",nonce=\"%s\",cnonce=\"%s\","
		                        "nc=%08x,qop=auth,digest-uri=\"%s\",response=%s,charset=%s",
		                        jd->username, realm, nonce, cnonce, 1, digest_uri, Hh, "utf-8");
	} else {
		/* We found rspauth, but don't really care... */
		g_free(s);
	}

	s = reply ? tobase64(reply) : NULL;
	reply_pkt = xt_new_node("response", s, NULL);
	xt_add_attr(reply_pkt, "xmlns", XMLNS_SASL);

	if (!jabber_write_packet(ic, reply_pkt)) {
		goto silent_error;
	}

	ret = XT_HANDLED;
	goto silent_error;

error:
	imcb_error(ic, "Incorrect SASL challenge received");
	imc_logout(ic, FALSE);

silent_error:
	g_free(digest_uri);
	g_free(cnonce);
	g_free(nonce);
	g_free(reply);
	g_free(realm);
	g_free(dec);
	g_free(s);
	xt_free_node(reply_pkt);

	return ret;
}

xt_status sasl_pkt_result(struct xt_node *node, gpointer data)
{
	struct im_connection *ic = data;
	struct jabber_data *jd = ic->proto_data;
	char *s;

	s = xt_find_attr(node, "xmlns");
	if (!s || strcmp(s, XMLNS_SASL) != 0) {
		imcb_log(ic, "Stream error while authenticating");
		imc_logout(ic, FALSE);
		return XT_ABORT;
	}

	if (strcmp(node->name, "success") == 0) {
		imcb_log(ic, "Authentication finished");
		jd->flags |= JFLAG_AUTHENTICATED | JFLAG_STREAM_RESTART;

		if (jd->flags & JFLAG_HIPCHAT) {
			return hipchat_handle_success(ic, node);
		}
	} else if (strcmp(node->name, "failure") == 0) {
		imcb_error(ic, "Authentication failure");
		imc_logout(ic, FALSE);
		return XT_ABORT;
	}

	return XT_HANDLED;
}

/* This one is needed to judge if we'll do authentication using IQ or SASL.
   It's done by checking if the <stream:stream> from the server has a
   version attribute. I don't know if this is the right way though... */
gboolean sasl_supported(struct im_connection *ic)
{
	struct jabber_data *jd = ic->proto_data;

	return (jd->xt && jd->xt->root && xt_find_attr(jd->xt->root, "version")) != 0;
}

void sasl_oauth2_init(struct im_connection *ic)
{
	struct jabber_data *jd = ic->proto_data;
	char *msg, *url;

	imcb_log(ic, "Starting OAuth authentication");

	/* Temporary contact, just used to receive the OAuth response. */
	imcb_add_buddy(ic, JABBER_OAUTH_HANDLE, NULL);
	url = oauth2_url(jd->oauth2_service);
	msg = g_strdup_printf("Open this URL in your browser to authenticate: %s", url);
	imcb_buddy_msg(ic, JABBER_OAUTH_HANDLE, msg, 0, 0);
	imcb_buddy_msg(ic, JABBER_OAUTH_HANDLE, "Respond to this message with the returned "
	               "authorization token.", 0, 0);

	g_free(msg);
	g_free(url);
}

static gboolean sasl_oauth2_remove_contact(gpointer data, gint fd, b_input_condition cond)
{
	struct im_connection *ic = data;

	if (g_slist_find(jabber_connections, ic)) {
		imcb_remove_buddy(ic, JABBER_OAUTH_HANDLE, NULL);
	}
	return FALSE;
}

static void sasl_oauth2_got_token(gpointer data, const char *access_token, const char *refresh_token,
                                  const char *error);

int sasl_oauth2_get_refresh_token(struct im_connection *ic, const char *msg)
{
	struct jabber_data *jd = ic->proto_data;
	char *code;
	int ret;

	imcb_log(ic, "Requesting OAuth access token");

	/* Don't do it here because the caller may get confused if the contact
	   we're currently sending a message to is deleted. */
	b_timeout_add(1, sasl_oauth2_remove_contact, ic);

	code = g_strdup(msg);
	g_strstrip(code);
	ret = oauth2_access_token(jd->oauth2_service, OAUTH2_AUTH_CODE,
	                          code, sasl_oauth2_got_token, ic);

	g_free(code);
	return ret;
}

int sasl_oauth2_refresh(struct im_connection *ic, const char *refresh_token)
{
	struct jabber_data *jd = ic->proto_data;

	return oauth2_access_token(jd->oauth2_service, OAUTH2_AUTH_REFRESH,
	                           refresh_token, sasl_oauth2_got_token, ic);
}

static void sasl_oauth2_got_token(gpointer data, const char *access_token, const char *refresh_token, const char *error)
{
	struct im_connection *ic = data;
	struct jabber_data *jd;
	GSList *auth = NULL;

	if (g_slist_find(jabber_connections, ic) == NULL) {
		return;
	}

	jd = ic->proto_data;

	if (access_token == NULL) {
		imcb_error(ic, "OAuth failure (%s)", error);
		imc_logout(ic, TRUE);
		return;
	}

	oauth_params_parse(&auth, ic->acc->pass);
	if (refresh_token) {
		oauth_params_set(&auth, "refresh_token", refresh_token);
	}
	if (access_token) {
		oauth_params_set(&auth, "access_token", access_token);
	}

	g_free(ic->acc->pass);
	ic->acc->pass = oauth_params_string(auth);
	oauth_params_free(&auth);

	g_free(jd->oauth2_access_token);
	jd->oauth2_access_token = g_strdup(access_token);

	jabber_connect(ic);
}
