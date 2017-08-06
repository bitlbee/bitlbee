/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Simple module to facilitate Mastodon functionality.                      *
*                                                                           *
*  Copyright 2009-2010 Geert Mulders <g.c.w.m.mulders@gmail.com>            *
*  Copyright 2010-2013 Wilmer van der Gaast <wilmer@gaast.net>              *
*  Copyright 2017      Alex Schroeder <alex@gnu.org>                        *
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

#include "nogaim.h"
#include "oauth.h"
#include "oauth2.h"
#include "mastodon.h"
#include "mastodon_http.h"
#include "mastodon_lib.h"
#include "url.h"

GSList *mastodon_connections = NULL;

static int mastodon_filter_cmp(struct mastodon_filter *tf1,
                              struct mastodon_filter *tf2)
{
	int i1 = 0;
	int i2 = 0;
	int i;

	static const mastodon_filter_type_t types[] = {
		/* Order of the types */
		MASTODON_FILTER_TYPE_FOLLOW,
		MASTODON_FILTER_TYPE_TRACK
	};

	for (i = 0; i < G_N_ELEMENTS(types); i++) {
		if (types[i] == tf1->type) {
			i1 = i + 1;
			break;
		}
	}

	for (i = 0; i < G_N_ELEMENTS(types); i++) {
		if (types[i] == tf2->type) {
			i2 = i + 1;
			break;
		}
	}

	if (i1 != i2) {
		/* With different types, return their difference */
		return i1 - i2;
	}

	/* With the same type, return the text comparison */
	return g_strcasecmp(tf1->text, tf2->text);
}

static gboolean mastodon_filter_update(gpointer data, gint fd,
                                      b_input_condition cond)
{
	struct im_connection *ic = data;
	struct mastodon_data *md = ic->proto_data;

	if (md->filters) {
		mastodon_open_filter_stream(ic);
	} else if (md->filter_stream) {
		http_close(md->filter_stream);
		md->filter_stream = NULL;
	}

	md->filter_update_id = 0;
	return FALSE;
}

static struct mastodon_filter *mastodon_filter_get(struct groupchat *c,
                                                 mastodon_filter_type_t type,
                                                 const char *text)
{
	struct mastodon_data *md = c->ic->proto_data;
	struct mastodon_filter *tf = NULL;
	struct mastodon_filter tfc = { type, (char *) text };
	GSList *l;

	for (l = md->filters; l; l = g_slist_next(l)) {
		tf = l->data;

		if (mastodon_filter_cmp(tf, &tfc) == 0) {
			break;
		}

		tf = NULL;
	}

	if (!tf) {
		tf = g_new0(struct mastodon_filter, 1);
		tf->type = type;
		tf->text = g_strdup(text);
		md->filters = g_slist_prepend(md->filters, tf);
	}

	if (!g_slist_find(tf->groupchats, c)) {
		tf->groupchats = g_slist_prepend(tf->groupchats, c);
	}

	if (md->filter_update_id > 0) {
		b_event_remove(md->filter_update_id);
	}

	/* Wait for other possible filter changes to avoid request spam */
	md->filter_update_id = b_timeout_add(MASTODON_FILTER_UPDATE_WAIT,
	                                     mastodon_filter_update, c->ic);
	return tf;
}

static void mastodon_filter_free(struct mastodon_filter *tf)
{
	g_slist_free(tf->groupchats);
	g_free(tf->text);
	g_free(tf);
}

static void mastodon_filter_remove(struct groupchat *c)
{
	struct mastodon_data *md = c->ic->proto_data;
	struct mastodon_filter *tf;
	GSList *l = md->filters;
	GSList *p;

	while (l != NULL) {
		tf = l->data;
		tf->groupchats = g_slist_remove(tf->groupchats, c);

		p = l;
		l = g_slist_next(l);

		if (!tf->groupchats) {
			mastodon_filter_free(tf);
			md->filters = g_slist_delete_link(md->filters, p);
		}
	}

	if (md->filter_update_id > 0) {
		b_event_remove(md->filter_update_id);
	}

	/* Wait for other possible filter changes to avoid request spam */
	md->filter_update_id = b_timeout_add(MASTODON_FILTER_UPDATE_WAIT,
	                                     mastodon_filter_update, c->ic);
}

static void mastodon_filter_remove_all(struct im_connection *ic)
{
	struct mastodon_data *md = ic->proto_data;
	GSList *chats = NULL;
	struct mastodon_filter *tf;
	GSList *l = md->filters;
	GSList *p;

	while (l != NULL) {
		tf = l->data;

		/* Build up a list of groupchats to be freed */
		for (p = tf->groupchats; p; p = g_slist_next(p)) {
			if (!g_slist_find(chats, p->data)) {
				chats = g_slist_prepend(chats, p->data);
			}
		}

		p = l;
		l = g_slist_next(l);
		mastodon_filter_free(p->data);
		md->filters = g_slist_delete_link(md->filters, p);
	}

	l = chats;

	while (l != NULL) {
		p = l;
		l = g_slist_next(l);

		/* Freed each remaining groupchat */
		imcb_chat_free(p->data);
		chats = g_slist_delete_link(chats, p);
	}

	if (md->filter_stream) {
		http_close(md->filter_stream);
		md->filter_stream = NULL;
	}
}

static GSList *mastodon_filter_parse(struct groupchat *c, const char *text)
{
	char **fs = g_strsplit(text, ";", 0);
	GSList *ret = NULL;
	struct mastodon_filter *tf;
	char **f;
	char *v;
	int i;
	int t;

	static const mastodon_filter_type_t types[] = {
		MASTODON_FILTER_TYPE_FOLLOW,
		MASTODON_FILTER_TYPE_TRACK
	};

	static const char *typestrs[] = {
		"follow",
		"track"
	};

	for (f = fs; *f; f++) {
		if ((v = strchr(*f, ':')) == NULL) {
			continue;
		}

		*(v++) = 0;

		for (t = -1, i = 0; i < G_N_ELEMENTS(types); i++) {
			if (g_strcasecmp(typestrs[i], *f) == 0) {
				t = i;
				break;
			}
		}

		if (t < 0 || strlen(v) == 0) {
			continue;
		}

		tf = mastodon_filter_get(c, types[t], v);
		ret = g_slist_prepend(ret, tf);
	}

	g_strfreev(fs);
	return ret;
}

struct groupchat *mastodon_groupchat_init(struct im_connection *ic)
{
	char *name_hint;
	struct groupchat *gc;
	struct mastodon_data *md = ic->proto_data;
	GSList *l;

	if (md->timeline_gc) {
		return md->timeline_gc;
	}

	md->timeline_gc = gc = imcb_chat_new(ic, "mastodon/timeline");

	name_hint = g_strdup_printf("%s_%s", md->prefix, ic->acc->user);
	imcb_chat_name_hint(gc, name_hint);
	g_free(name_hint);

	for (l = ic->bee->users; l; l = l->next) {
		bee_user_t *bu = l->data;
		if (bu->ic == ic) {
			imcb_chat_add_buddy(gc, bu->handle);
		}
	}
	imcb_chat_add_buddy(gc, ic->acc->user);

	return gc;
}

/**
 * Free the oauth2_service struct.
 */
static void os_free(struct oauth2_service *os) {

	if (os == NULL) {
		return;
	}

	g_free(os->auth_url);
	g_free(os->token_url);
	g_free(os);
}

/**
 * Create a new oauth2_service struct. If we haven never connected to
 * the server, we'll be missing our key and secret.
 */
static struct oauth2_service *get_oauth2_service(struct im_connection *ic)
{
	struct mastodon_data *md = ic->proto_data;

	struct oauth2_service *os = g_new0(struct oauth2_service, 1);
	os->auth_url = g_strconcat("https://", md->url_host, "/oauth/authorize", NULL);
	os->token_url = g_strconcat("https://", md->url_host, "/oauth/token", NULL);
	os->redirect_url = "urn:ietf:wg:oauth:2.0:oob";
	os->scope = MASTODON_SCOPE;

	// possibly empty strings if the client is not registered
	os->consumer_key = set_getstr(&ic->acc->set, "consumer_key");
	os->consumer_secret = set_getstr(&ic->acc->set, "consumer_secret");

	return os;
}

/**
 * Check message length by comparing it to the appropriate setting.
 * Note this issue: "Count all URLs in text as 23 characters flat, do
 * not count domain part of usernames."
 * https://github.com/tootsuite/mastodon/pull/4427
 **/
static gboolean mastodon_length_check(struct im_connection *ic, gchar *msg)
{
	int max = set_getint(&ic->acc->set, "message_length");

	if (max == 0) {
		return TRUE;
	}

	int len = g_utf8_strlen(msg, -1);

	GRegex *regex = g_regex_new (MASTODON_URL_REGEX, 0, 0, NULL);
	GMatchInfo *match_info;

	g_regex_match (regex, msg, 0, &match_info);
	while (g_match_info_matches (match_info))
	{
	    gchar *url = g_match_info_fetch (match_info, 0);
	    len = len - g_utf8_strlen(url, -1) + 23;
	    g_free (url);
	    g_match_info_next (match_info, NULL);
	}
	g_regex_unref (regex);

	regex = g_regex_new (MASTODON_MENTION_REGEX, 0, 0, NULL);
	g_regex_match (regex, msg, 0, &match_info);
	while (g_match_info_matches (match_info))
	{
	    gchar *mention = g_match_info_fetch (match_info, 0);
	    gchar *nick = g_match_info_fetch (match_info, 2);
	    len = len - g_utf8_strlen(mention, -1) + g_utf8_strlen(nick, -1);
	    g_free (mention);
	    g_free (nick);
	    g_match_info_next (match_info, NULL);
	}
	g_regex_unref (regex);

	g_match_info_free (match_info);

	if (len <= max) {
		return TRUE;
	}

	mastodon_log(ic, "Maximum message length exceeded: %d > %d", len, max);

	return FALSE;
}

static char *set_eval_commands(set_t * set, char *value)
{
	if (g_strcasecmp(value, "strict") == 0) {
		return value;
	} else {
		return set_eval_bool(set, value);
	}
}

static char *set_eval_mode(set_t * set, char *value)
{
	if (g_strcasecmp(value, "one") == 0 ||
	    g_strcasecmp(value, "many") == 0 || g_strcasecmp(value, "chat") == 0) {
		return value;
	} else {
		return NULL;
	}
}

static void mastodon_init(account_t * acc)
{
	set_t *s;
	char *def_url;

	if (strcmp(acc->prpl->name, "mastodon") == 0) {
		def_url = MASTODON_API_URL;
	}

	s = set_add(&acc->set, "auto_reply_timeout", "10800", set_eval_int, acc);

	s = set_add(&acc->set, "base_url", def_url, NULL, acc);
	s->flags |= ACC_SET_OFFLINE_ONLY;

	s = set_add(&acc->set, "commands", "true", set_eval_commands, acc);

	s = set_add(&acc->set, "message_length", "500", set_eval_int, acc);

	s = set_add(&acc->set, "mode", "chat", set_eval_mode, acc);
	s->flags |= ACC_SET_OFFLINE_ONLY;

	s = set_add(&acc->set, "show_ids", "true", set_eval_bool, acc);

	s = set_add(&acc->set, "strip_newlines", "false", set_eval_bool, acc);

	s = set_add(&acc->set, "app_id", "0", set_eval_int, acc);
	s->flags |= SET_HIDDEN;

	s = set_add(&acc->set, "account_id", "0", set_eval_int, acc);
	s->flags |= SET_HIDDEN;

	s = set_add(&acc->set, "consumer_key", "", NULL, acc);
	s->flags |= SET_HIDDEN;

	s = set_add(&acc->set, "consumer_secret", "", NULL, acc);
	s->flags |= SET_HIDDEN;
}

/**
 * Connect to Mastodon server, using the data we saved in the account.
 */
static void mastodon_connect(struct im_connection *ic)
{
	struct mastodon_data *md = ic->proto_data;
	char name[strlen(ic->acc->user) + 9];
	url_t url;
	char *s;

	imcb_log(ic, "Connecting");

	if (!url_set(&url, set_getstr(&ic->acc->set, "base_url")) ||
	    url.proto != PROTO_HTTPS) {
		imcb_error(ic, "Incorrect API base URL: %s", set_getstr(&ic->acc->set, "base_url"));
		imc_logout(ic, FALSE);
		return;
	}

	md->url_ssl = url.proto == PROTO_HTTPS; // always
	md->url_port = url.port;
	md->url_host = g_strdup(url.host);
	if (strcmp(url.file, "/") != 0) {
		md->url_path = g_strdup(url.file);
	}

	md->prefix = g_strdup(url.host);

	sprintf(name, "%s_%s", md->prefix, ic->acc->user);
	imcb_add_buddy(ic, name, NULL);
	imcb_buddy_status(ic, name, OPT_LOGGED_IN, NULL, NULL);

	md->log = g_new0(struct mastodon_log_data, MASTODON_LOG_LENGTH);
	md->log_id = -1;

	s = set_getstr(&ic->acc->set, "mode");
	if (g_strcasecmp(s, "one") == 0) {
		md->flags |= MASTODON_MODE_ONE;
	} else if (g_strcasecmp(s, "many") == 0) {
		md->flags |= MASTODON_MODE_MANY;
	} else {
		md->flags |= MASTODON_MODE_CHAT;
	}

	md->flags &= ~MASTODON_DOING_TIMELINE;

	if (!(md->flags & MASTODON_MODE_ONE) &&
	    !(md->flags & MASTODON_HAVE_FRIENDS)) {
		// find our id
		mastodon_verify_credentials(ic);
		// add buddies for this id
		mastodon_following(ic, 0);
	}

	/* Create the room. */
	if (md->flags & MASTODON_MODE_CHAT) {
		mastodon_groupchat_init(ic);
	}

	mastodon_initial_timeline(ic);
        mastodon_open_stream(ic);
	ic->flags |= OPT_PONGS;
}

void oauth2_init(struct im_connection *ic)
{
	struct mastodon_data *md = ic->proto_data;

	imcb_log(ic, "Starting OAuth authentication");

	/* Temporary contact, just used to receive the OAuth response. */
	imcb_add_buddy(ic, MASTODON_OAUTH_HANDLE, NULL);

	char *url = oauth2_url(md->oauth2_service);
	char *msg = g_strdup_printf("Open this URL in your browser to authenticate: %s", url);
	imcb_buddy_msg(ic, MASTODON_OAUTH_HANDLE, msg, 0, 0);

	g_free(msg);
	g_free(url);

	imcb_buddy_msg(ic, MASTODON_OAUTH_HANDLE, "Respond to this message with the returned "
	               "authorization token.", 0, 0);

	ic->flags |= OPT_SLOW_LOGIN;
}

int oauth2_refresh(struct im_connection *ic, const char *refresh_token);

static void mastodon_login(account_t * acc)
{
	struct im_connection *ic = imcb_new(acc);
	struct mastodon_data *md = g_new0(struct mastodon_data, 1);
	url_t url;

	imcb_log(ic, "Login");

	mastodon_connections = g_slist_append(mastodon_connections, ic);
	ic->proto_data = md;
	md->user = g_strdup(acc->user);

	if (!url_set(&url, set_getstr(&ic->acc->set, "base_url")) ||
	    (url.proto != PROTO_HTTPS)) {
		imcb_error(ic, "Incorrect API base URL: %s", set_getstr(&ic->acc->set, "base_url"));
		imc_logout(ic, FALSE);
		return;
	}
	md->url_ssl = 1;
	md->url_port = url.port;
	md->url_host = g_strdup(url.host);
	md->prefix = g_strdup(url.host);
	if (strcmp(url.file, "/") != 0) {
		md->url_path = g_strdup(url.file);
	} else {
		md->url_path = g_strdup("");
	}

	GSList *p_in = NULL;
	const char *tok;

	md->oauth2_service = get_oauth2_service(ic);

	oauth_params_parse(&p_in, ic->acc->pass);

	/* If we did not have these stored, register the app and try
	 * again. We'll call oauth2_init from the callback in order to
	 * connect, eventually. */
	if (!md->oauth2_service->consumer_key || !md->oauth2_service->consumer_secret ||
	    strlen(md->oauth2_service->consumer_key) == 0 || strlen(md->oauth2_service->consumer_secret) == 0) {
		mastodon_register_app(ic);
	}
        /* If we have a refresh token, in which case any access token
	   we *might* have has probably expired already anyway.
	   Refresh and connect. */
	else if ((tok = oauth_params_get(&p_in, "refresh_token"))) {
		oauth2_refresh(ic, tok);
	}
	/* If we don't have a refresh token, let's hope the access
	   token is still usable. */
	else if ((tok = oauth_params_get(&p_in, "access_token"))) {
		md->oauth2_access_token = g_strdup(tok);
		mastodon_connect(ic);
	}
	/* If we don't have any, start the OAuth process now. */
	else {
		oauth2_init(ic);
	}
	/* All of the above will end up calling mastodon_connect() in
	   the end. */

	oauth_params_free(&p_in);
}

/**
 * Logout method. Just free the mastodon_data.
 */
static void mastodon_logout(struct im_connection *ic)
{
	struct mastodon_data *md = ic->proto_data;

	// Set the status to logged out.
	ic->flags &= ~OPT_LOGGED_IN;

	if (md) {
		// Remove the main_loop function from the function queue.
		b_event_remove(md->main_loop_id);

		if (md->timeline_gc) {
			imcb_chat_free(md->timeline_gc);
		}

		if (md->filter_update_id > 0) {
			b_event_remove(md->filter_update_id);
		}

		http_close(md->stream);
		mastodon_filter_remove_all(ic);
		os_free(md->oauth2_service);
		g_free(md->user);
		g_free(md->prefix);
		g_free(md->url_host);
		g_free(md->url_path);
		g_free(md->log);
		g_free(md);
	}

	mastodon_connections = g_slist_remove(mastodon_connections, ic);
}



void oauth2_got_token(gpointer data, const char *access_token, const char *refresh_token, const char *error)
{
	struct im_connection *ic = data;
	struct mastodon_data *md;
	GSList *auth = NULL;

	if (g_slist_find(mastodon_connections, ic) == NULL) {
		return;
	}

	md = ic->proto_data;

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

	g_free(md->oauth2_access_token);
	md->oauth2_access_token = g_strdup(access_token);

	mastodon_connect(ic);
}

static gboolean oauth2_remove_contact(gpointer data, gint fd, b_input_condition cond)
{
	struct im_connection *ic = data;

	if (g_slist_find(mastodon_connections, ic)) {
		imcb_remove_buddy(ic, MASTODON_OAUTH_HANDLE, NULL);
	}
	return FALSE;
}

int oauth2_get_refresh_token(struct im_connection *ic, const char *msg)
{
	struct mastodon_data *md = ic->proto_data;
	char *code;
	int ret;

	imcb_log(ic, "Requesting OAuth access token");

	/* Don't do it here because the caller may get confused if the contact
	   we're currently sending a message to is deleted. */
	b_timeout_add(1, oauth2_remove_contact, ic);

	code = g_strdup(msg);
	g_strstrip(code);
	ret = oauth2_access_token(md->oauth2_service, OAUTH2_AUTH_CODE,
	                          code, oauth2_got_token, ic);

	g_free(code);
	return ret;
}

int oauth2_refresh(struct im_connection *ic, const char *refresh_token)
{
	struct mastodon_data *md = ic->proto_data;

	return oauth2_access_token(md->oauth2_service, OAUTH2_AUTH_REFRESH,
	                           refresh_token, oauth2_got_token, ic);
}

static void mastodon_handle_command(struct im_connection *ic, char *message);

/**
 * Post a message. Make sure we get all the meta data for the status
 * right. We have to consider a few cases:
 *
 * "reply 1 msg" -> in_reply_to and who is set, change to "@who msg"
 * "reply nick msg" -> in_reply_to and who is set, change to "@who msg"
 * "post nick: msg" -> this is not a real mention, just post
 * "post @nick msg" -> starts a new conversation, not determine in_reply_to
 * "post msg" -> do nothing
 * /msg msg -> who is set, set in_reply_to, change to "@who msg" (visibility should be "direct")
 */
static void mastodon_post_message(struct im_connection *ic, char *message, guint64 in_reply_to, char *who, mastodon_visibility_t visibility)
{
	if (!mastodon_length_check(ic, message)) {
		return;
	}

	char *text = NULL;

	// If the message starts with who, trim that.
	if (who && strncmp(who, message, strlen(who)) == 0) {
		message += strlen(who);
		// we expect a space: "nick: foo" or "nick, foo"
		if (message[0] == ' ') {
			message++;
		}
	}
	
	// Trim ',' or ':' from who.
	char *s = who + strlen(who) - 1;
	if (who && s > who && (*s == ':' || *s == ',')) {
		*s = '\0';
	}

	bee_user_t *bu;
	if (who && who[0] != '@' && (bu = bee_user_by_handle(ic->bee, ic, who))) {
		struct mastodon_user_data *mud = bu->data;
		text = g_strdup_printf("@%s %s", bu->handle, message);

		if (!in_reply_to && time(NULL) < mud->last_time + set_getint(&ic->acc->set, "auto_reply_timeout")) {
			in_reply_to = mud->last_id;
		}
	}

	/* If the user runs undo between this request and its response
	   this would delete the second-last toot. Prevent that. */
	struct mastodon_data *md = ic->proto_data;
	md->last_id = 0;
	mastodon_post_status(ic, text ? text : message, in_reply_to, 0);

	g_free(text);
}

/**
 * Send a direct message. If this buddy is the magic mastodon oauth
 * handle, then treat the message as the refresh token. If this buddy
 * is me, then treat the message as a command.
 */
static int mastodon_buddy_msg(struct im_connection *ic, char *who, char *message, int away)
{
	struct mastodon_data *md = ic->proto_data;
	int plen = strlen(md->prefix);

	if (g_strcasecmp(who, MASTODON_OAUTH_HANDLE) == 0 &&
	    !(md->flags & OPT_LOGGED_IN)) {

		if (oauth2_get_refresh_token(ic, message)) {
			return 1;
		} else {
			imcb_error(ic, "OAuth failure");
			imc_logout(ic, TRUE);
			return 0;
		}
	}

	if (g_strncasecmp(who, md->prefix, plen) == 0 && who[plen] == '_' &&
	    g_strcasecmp(who + plen + 1, ic->acc->user) == 0) {
		mastodon_handle_command(ic, message);
	} else {
		mastodon_post_message(ic, message, 0, who, MASTODON_VISIBILITY_DIRECT);
	}
	return 0;
}

static void mastodon_user(struct im_connection *ic, char *who);

static void mastodon_get_info(struct im_connection *ic, char *who)
{
	struct mastodon_data *md = ic->proto_data;
	struct irc_channel *ch = md->timeline_gc->ui_data;
	int plen = strlen(md->prefix);

	imcb_log(ic, "Sending output to %s", ch->name);
	if (g_strncasecmp(who, md->prefix, plen) == 0 && who[plen] == '_' &&
	    g_strcasecmp(who + plen + 1, ic->acc->user) == 0) {
		mastodon_instance(ic);
	} else {
		mastodon_user(ic, who);
	}
}

static void mastodon_chat_msg(struct groupchat *c, char *message, int flags)
{
	if (c && message) {
		mastodon_handle_command(c->ic, message);
	}
}

static struct groupchat *mastodon_chat_join(struct im_connection *ic,
                                           const char *room, const char *nick,
                                           const char *password, set_t **sets)
{
	struct groupchat *c = imcb_chat_new(ic, room);
	GSList *fs = mastodon_filter_parse(c, room);
	GString *topic = g_string_new("");
	struct mastodon_filter *tf;
	GSList *l;

	fs = g_slist_sort(fs, (GCompareFunc) mastodon_filter_cmp);

	for (l = fs; l; l = g_slist_next(l)) {
		tf = l->data;

		if (topic->len > 0) {
			g_string_append(topic, ", ");
		}

		if (tf->type == MASTODON_FILTER_TYPE_FOLLOW) {
			g_string_append_c(topic, '@');
		}

		g_string_append(topic, tf->text);
	}

	if (topic->len > 0) {
		g_string_prepend(topic, "Mastodon Filter: ");
	}

	imcb_chat_topic(c, NULL, topic->str, 0);
	imcb_chat_add_buddy(c, ic->acc->user);

	if (topic->len == 0) {
		imcb_error(ic, "Failed to handle any filters");
		imcb_chat_free(c);
		c = NULL;
	}

	g_string_free(topic, TRUE);
	g_slist_free(fs);

	return c;
}

static void mastodon_chat_leave(struct groupchat *c)
{
	struct mastodon_data *md = c->ic->proto_data;

	if (c != md->timeline_gc) {
		mastodon_filter_remove(c);
		imcb_chat_free(c);
		return;
	}

	/* If the user leaves the channel: Fine. Rejoin him/her once new
	   toots come in. */
	imcb_chat_free(md->timeline_gc);
	md->timeline_gc = NULL;
}

static void mastodon_add_permit(struct im_connection *ic, char *who)
{
}

static void mastodon_rem_permit(struct im_connection *ic, char *who)
{
}

static void mastodon_buddy_data_add(bee_user_t *bu)
{
	bu->data = g_new0(struct mastodon_user_data, 1);
}

static void mastodon_buddy_data_free(bee_user_t *bu)
{
	g_free(bu->data);
}

bee_user_t mastodon_log_local_user;

/**
 * Find a user account based on their nick name.
 */
static bee_user_t *mastodon_user_by_nick(struct im_connection *ic, char *nick)
{
	for (GSList *l = ic->bee->users; l; l = l->next) {
		bee_user_t *bu = l->data;
		irc_user_t *iu = bu->ui_data;
		if (strcmp(iu->nick, nick) == 0) {
			return bu;
		}
	}
	return NULL;
}

/**
 * Convert the given bitlbee toot ID or bitlbee username into a
 * mastodon status ID and returns it. If provided with a pointer to a
 * bee_user_t, fills that as well. Provide NULL if you don't need it.
 *
 * Returns 0 if the user provides garbage.
 */
static guint64 mastodon_message_id_from_command_arg(struct im_connection *ic, char *arg, bee_user_t **bu_)
{
	struct mastodon_data *md = ic->proto_data;
	struct mastodon_user_data *mud;
	bee_user_t *bu = NULL;
	guint64 id = 0;

	if (bu_) {
		*bu_ = NULL;
	}
	if (!arg || !arg[0]) {
		return 0;
	}

	if (arg[0] != '#' && (bu = mastodon_user_by_nick(ic, arg))) {
		if ((mud = bu->data)) {
			id = mud->last_id;
		}
	} else {
		if (arg[0] == '#') {
			arg++;
		}
		if (parse_int64(arg, 16, &id) && id < MASTODON_LOG_LENGTH) {
			bu = md->log[id].bu;
			id = md->log[id].id;
		} else if (parse_int64(arg, 10, &id)) {
			/* Allow normal toot IDs as well; not a very useful
			   feature but it's always been there. Just ignore
			   very low IDs to avoid accidents. */
			if (id < 1000000) {
				id = 0;
			}
		}
	}
	if (bu_) {
		if (bu == &mastodon_log_local_user) {
			/* HACK alert. There's no bee_user object for the local
			 * user so just fake one for the few cmds that need it. */
			mastodon_log_local_user.handle = md->user;
		} else {
			/* Beware of dangling pointers! */
			if (!g_slist_find(ic->bee->users, bu)) {
				bu = NULL;
			}
		}
		*bu_ = bu;
	}
	return id;
}

static void mastodon_no_id_warning(struct im_connection *ic, char *what)
{
	mastodon_log(ic, "User or status '%s' is unknown", what);
}

static void mastodon_unknown_user_warning(struct im_connection *ic, char *who)
{
	mastodon_log(ic, "User '%s' is unknown", who);
}

static guint64 mastodon_message_id_or_warn(struct im_connection *ic, char *what, bee_user_t **bu)
{
	guint64 id = mastodon_message_id_from_command_arg(ic, what, bu);
	if (!id) {
		mastodon_no_id_warning(ic, what);
	} else if (bu && !*bu) {
		mastodon_unknown_user_warning(ic, what);
	}
	return id;
}

static guint64 mastodon_account_id(bee_user_t *bu) {
	struct mastodon_user_data *mud;
	if (bu != NULL && (mud = bu->data)) {
		return mud->account_id;
	}
	return 0;
}

static guint64 mastodon_user_id_or_warn(struct im_connection *ic, char *who)
{
	bee_user_t *bu;
	guint64 id;
	if ((bu = mastodon_user_by_nick(ic, who)) &&
	    (id = mastodon_account_id(bu))) {
		return id;
	}
	mastodon_unknown_user_warning(ic, who);
	return 0;
}

static void mastodon_user(struct im_connection *ic, char *who)
{
	bee_user_t *bu;
	guint64 id;
	if ((bu = mastodon_user_by_nick(ic, who)) &&
	    (id = mastodon_account_id(bu))) {
		mastodon_account(ic, id);
	} else {
		mastodon_search_account(ic, who);
	}
}

static void mastodon_add_buddy(struct im_connection *ic, char *who, char *group)
{
	bee_user_t *bu;
	guint64 id;
	if ((bu = mastodon_user_by_nick(ic, who)) &&
	    (id = mastodon_account_id(bu))) {
		// If the nick is already in the channel (when we just
		// unfollowed them, for example), we're taking a
		// shortcut. No fancy looking at the relationship and
		// all that. The nick is already here, after all.
		mastodon_post(ic, MASTODON_ACCOUNT_FOLLOW_URL, id);
	} else {
		// Alternatively, we're looking for an unknown user.
		// They must be searched, followed, and added to the
		// channel. It's going to get hairy.
		mastodon_follow(ic, who);
	}
}

static void mastodon_remove_buddy(struct im_connection *ic, char *who, char *group)
{
	guint64 id;
	if ((id = mastodon_user_id_or_warn(ic, who))) {
		mastodon_post(ic, MASTODON_ACCOUNT_UNFOLLOW_URL, id);
	}
}

static void mastodon_add_deny(struct im_connection *ic, char *who)
{
	guint64 id;
	if ((id = mastodon_user_id_or_warn(ic, who))) {
		mastodon_post(ic, MASTODON_ACCOUNT_BLOCK_URL, id);
	}
}

static void mastodon_rem_deny(struct im_connection *ic, char *who)
{
	guint64 id;
	if ((id = mastodon_user_id_or_warn(ic, who))) {
		mastodon_post(ic, MASTODON_ACCOUNT_UNBLOCK_URL, id);
	}
}

/**
 * Commands we understand. Changes should be documented in
 * commands.xml and on https://wiki.bitlbee.org/HowtoMastodon
 */
static void mastodon_handle_command(struct im_connection *ic, char *message)
{
	struct mastodon_data *md = ic->proto_data;
	gboolean allow_post = g_strcasecmp(set_getstr(&ic->acc->set, "commands"), "strict") != 0;
	bee_user_t *bu = NULL;
	guint64 id;

	char *cmds = g_strdup(message);
	char **cmd = split_command_parts(cmds, 2);

	if (cmd[0] == NULL) {
		/* Nothing to do */
	} else if (!set_getbool(&ic->acc->set, "commands") && allow_post) {
		/* Not supporting commands if "commands" is set to true/strict. */
	} else if (g_strcasecmp(cmd[0], "info") == 0) {
		if (!cmd[1]) {
			mastodon_log(ic, "Usage:\n"
				     "- info instance\n"
				     "- info [id|screenname]\n"
				     "- info user [nick|account]");
		} else if (g_strcasecmp(cmd[1], "instance") == 0) {
			mastodon_instance(ic);
		} else if (g_strcasecmp(cmd[1], "user") == 0 && cmd[2]) {
			mastodon_user(ic, cmd[2]);
		} else if ((id = mastodon_message_id_or_warn(ic, cmd[1], NULL))) {
			mastodon_status(ic, id);
		}
	} else if (g_strcasecmp(cmd[0], "undo") == 0 ||
		   g_strcasecmp(cmd[0], "del") == 0 ||
		   g_strcasecmp(cmd[0], "delete") == 0) {
		if (cmd[1] == NULL) {
			mastodon_status_delete(ic, md->last_id);
		} else if ((id = mastodon_message_id_from_command_arg(ic, cmd[1], NULL))) {
			mastodon_status_delete(ic, id);
		} else {
			mastodon_log(ic, "Could not undo last post");
		}
	} else if ((g_strcasecmp(cmd[0], "favourite") == 0 ||
	            g_strcasecmp(cmd[0], "favorite") == 0 ||
	            g_strcasecmp(cmd[0], "fav") == 0 ||
	            g_strcasecmp(cmd[0], "like") == 0) && cmd[1]) {
		if ((id = mastodon_message_id_or_warn(ic, cmd[1], NULL))) {
			mastodon_post(ic, MASTODON_STATUS_FAVOURITE_URL, id);
		}
	} else if ((g_strcasecmp(cmd[0], "unfavourite") == 0 ||
	            g_strcasecmp(cmd[0], "unfavorite") == 0 ||
	            g_strcasecmp(cmd[0], "unfav") == 0 ||
	            g_strcasecmp(cmd[0], "unlike") == 0 ||
	            g_strcasecmp(cmd[0], "dislike") == 0) && cmd[1]) {
		if ((id = mastodon_message_id_or_warn(ic, cmd[1], NULL))) {
			mastodon_post(ic, MASTODON_STATUS_UNFAVOURITE_URL, id);
		}
	} else if (g_strcasecmp(cmd[0], "follow") == 0 && cmd[1]) {
		mastodon_add_buddy(ic, cmd[1], NULL);
	} else if (g_strcasecmp(cmd[0], "unfollow") == 0 && cmd[1]) {
		mastodon_remove_buddy(ic, cmd[1], NULL);
	} else if (g_strcasecmp(cmd[0], "block") == 0 && cmd[1]) {
		mastodon_add_deny(ic, cmd[1]);
	} else if ((g_strcasecmp(cmd[0], "unblock") == 0 ||
		    g_strcasecmp(cmd[0], "allow") == 0) && cmd[1]) {
		mastodon_rem_deny(ic, cmd[1]);
	} else if (g_strcasecmp(cmd[0], "mute") == 0 &&
		   g_strcasecmp(cmd[1], "user") == 0 &&
		   cmd[2]) {
		if ((id = mastodon_user_id_or_warn(ic, cmd[2]))) {
			mastodon_post(ic, MASTODON_ACCOUNT_MUTE_URL, id);
		}
	} else if (g_strcasecmp(cmd[0], "unmute") == 0 &&
		   g_strcasecmp(cmd[1], "user") == 0 &&
		   cmd[2]) {
		if ((id = mastodon_user_id_or_warn(ic, cmd[2]))) {
			mastodon_post(ic, MASTODON_ACCOUNT_UNMUTE_URL, id);
		}
	} else if (g_strcasecmp(cmd[0], "mute") == 0 && cmd[1]) {
		if ((id = mastodon_message_id_or_warn(ic, cmd[1], NULL))) {
			mastodon_post(ic, MASTODON_STATUS_MUTE_URL, id);
		}
	} else if (g_strcasecmp(cmd[0], "unmute") == 0 && cmd[1]) {
		if ((id = mastodon_message_id_or_warn(ic, cmd[1], NULL))) {
			mastodon_post(ic, MASTODON_STATUS_UNMUTE_URL, id);
		}
	} else if (g_strcasecmp(cmd[0], "boost") == 0 && cmd[1]) {
		if ((id = mastodon_message_id_or_warn(ic, cmd[1], NULL))) {
			mastodon_post(ic, MASTODON_STATUS_BOOST_URL, id);
		}
	} else if (g_strcasecmp(cmd[0], "unboost") == 0 && cmd[1]) {
		if ((id = mastodon_message_id_or_warn(ic, cmd[1], NULL))) {
			mastodon_post(ic, MASTODON_STATUS_UNBOOST_URL, id);
		}
	} else if (g_strcasecmp(cmd[0], "url") == 0) {
		if ((id = mastodon_message_id_or_warn(ic, cmd[1], NULL))) {
			mastodon_status_show_url(ic, id);
		}
	} else if ((g_strcasecmp(cmd[0], "report") == 0 ||
	            g_strcasecmp(cmd[0], "spam") == 0) && cmd[1]) {
		if ((id = mastodon_message_id_or_warn(ic, cmd[1], NULL))) {
			if (!cmd[2] || strlen(cmd[2]) == 0) {
				mastodon_log(ic, "You must provide a comment with your report");
			} else {
				mastodon_report(ic, id, cmd[2]);
			}
		}
	} else if (g_strcasecmp(cmd[0], "reply") == 0 && cmd[1] && cmd[2]) {
		if ((id = mastodon_message_id_or_warn(ic, cmd[1], &bu))) {
			mastodon_post_message(ic, cmd[2], id, bu->handle, MASTODON_VISIBILITY_PUBLIC);
		}
	} else if (g_strcasecmp(cmd[0], "post") == 0) {
		mastodon_post_message(ic, message + 5, 0, cmd[1], MASTODON_VISIBILITY_PUBLIC);
	} else if (allow_post) {
		mastodon_post_message(ic, message, 0, cmd[0], MASTODON_VISIBILITY_PUBLIC);
	} else {
		mastodon_log(ic, "Unknown command: %s", cmd[0]);
	}

	g_free(cmds);
}

void mastodon_log(struct im_connection *ic, char *format, ...)
{
	struct mastodon_data *md = ic->proto_data;
	va_list params;
	char *text;

	va_start(params, format);
	text = g_strdup_vprintf(format, params);
	va_end(params);

	if (md->timeline_gc) {
		imcb_chat_log(md->timeline_gc, "%s", text);
	} else {
		imcb_log(ic, "%s", text);
	}

	g_free(text);
}


void mastodon_initmodule()
{
	struct prpl *ret = g_new0(struct prpl, 1);

	ret->options = PRPL_OPT_NOOTR | PRPL_OPT_NO_PASSWORD;
	ret->name = "mastodon";
	ret->login = mastodon_login;
	ret->init = mastodon_init;
	ret->logout = mastodon_logout;
	ret->buddy_msg = mastodon_buddy_msg;
	ret->get_info = mastodon_get_info;
	ret->add_buddy = mastodon_add_buddy;
	ret->remove_buddy = mastodon_remove_buddy;
	ret->chat_msg = mastodon_chat_msg;
	ret->chat_join = mastodon_chat_join;
	ret->chat_leave = mastodon_chat_leave;
	ret->add_permit = mastodon_add_permit;
	ret->rem_permit = mastodon_rem_permit;
	ret->add_deny = mastodon_add_deny;
	ret->rem_deny = mastodon_rem_deny;
	ret->buddy_data_add = mastodon_buddy_data_add;
	ret->buddy_data_free = mastodon_buddy_data_free;
	ret->handle_cmp = g_strcasecmp;

	register_protocol(ret);
}
