/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Simple module to facilitate mastodon functionality.                       *
*                                                                           *
*  Copyright 2009-2010 Geert Mulders <g.c.w.m.mulders@gmail.com>            *
*  Copyright 2010-2013 Wilmer van der Gaast <wilmer@gaast.net>              *
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

/**
 * Main loop function
 */
gboolean mastodon_main_loop(gpointer data, gint fd, b_input_condition cond)
{
	struct im_connection *ic = data;

	// Check if we are still logged in...
	if (!g_slist_find(mastodon_connections, ic)) {
		return FALSE;
	}

	// Do stuff..
	return mastodon_get_timeline(ic, -1) &&
	       ((ic->flags & OPT_LOGGED_IN) == OPT_LOGGED_IN);
}

static void mastodon_main_loop_start(struct im_connection *ic)
{
	struct mastodon_data *md = ic->proto_data;

	char *last_tweet = set_getstr(&ic->acc->set, "_last_tweet");

	if (last_tweet) {
		md->timeline_id = g_ascii_strtoull(last_tweet, NULL, 0);
	}

	/* Create the room now that we "logged in". */
	if (md->flags & MASTODON_MODE_CHAT) {
		mastodon_groupchat_init(ic);
	}

	// Run this once and open the stream
	mastodon_main_loop(ic, -1, 0);

        mastodon_open_stream(ic);
	ic->flags |= OPT_PONGS;
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

static struct oauth2_service *get_oauth2_service(struct im_connection *ic)
{
	struct mastodon_data *md = ic->proto_data;

	struct oauth2_service *os = g_new0(struct oauth2_service, 1);
	os->auth_url = g_strconcat("https://", md->url_host, "/oauth/authorize", NULL);
	os->token_url = g_strconcat("https://", md->url_host, "/oauth/token", NULL);
	os->redirect_url = "urn:ietf:wg:oauth:2.0:oob";
	os->scope = MASTODON_SCOPE;

	os->consumer_key = set_getstr(&ic->acc->set, "consumer_key");
	os->consumer_secret = set_getstr(&ic->acc->set, "consumer_secret");

	// if we did not have these stored, register the app and try again
	if (!os->consumer_key || !os->consumer_secret ||
	    strlen(os->consumer_key) == 0 || strlen(os->consumer_secret) == 0) {
		mastodon_register_app(ic);
		os->consumer_key = set_getstr(&ic->acc->set, "consumer_key");
		os->consumer_secret = set_getstr(&ic->acc->set, "consumer_secret");
	}

	return os;
}

static gboolean mastodon_length_check(struct im_connection *ic, gchar * msg)
{
	int max = set_getint(&ic->acc->set, "message_length");
	int len = g_utf8_strlen(msg, -1);

	if (max == 0 || len <= max) {
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

	s = set_add(&acc->set, "fetch_interval", "60", set_eval_int, acc);
	s->flags |= ACC_SET_OFFLINE_ONLY;

	s = set_add(&acc->set, "message_length", "140", set_eval_int, acc);

	s = set_add(&acc->set, "mode", "chat", set_eval_mode, acc);
	s->flags |= ACC_SET_OFFLINE_ONLY;

	s = set_add(&acc->set, "oauth", "true", set_eval_oauth, acc);

	s = set_add(&acc->set, "show_ids", "true", set_eval_bool, acc);

	s = set_add(&acc->set, "show_old_mentions", "0", set_eval_int, acc);

	s = set_add(&acc->set, "strip_newlines", "false", set_eval_bool, acc);

	s = set_add(&acc->set, "app_id", "0", set_eval_int, acc);
	s->flags |= SET_HIDDEN;

	s = set_add(&acc->set, "account_id", "0", set_eval_int, acc);
	s->flags |= SET_HIDDEN;
	
	s = set_add(&acc->set, "consumer_key", "", NULL, acc);
	s->flags |= SET_HIDDEN;

	s = set_add(&acc->set, "consumer_secret", "", NULL, acc);
	s->flags |= SET_HIDDEN;

	s = set_add(&acc->set, "_last_tweet", "0", NULL, acc);
	s->flags |= SET_HIDDEN | SET_NOSAVE;
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
	} else {
		md->url_path = g_strdup("");
		if (g_str_has_suffix(url.host, "mastodon.com")) {
			/* May fire for people who turned on HTTPS. */
			imcb_error(ic, "Warning: Mastodon requires a version number in API calls "
			           "now. Try resetting the base_url account setting.");
		}
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
		mastodon_following(ic, -1);
		// mastodon_get_mutes_ids(ic, -1);
		// mastodon_get_noretweets_ids(ic, -1);
	}
	mastodon_main_loop_start(ic);
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

	/* First see if we have a refresh token, in which case any
	   access token we *might* have has probably expired already
	   anyway. */
	if ((tok = oauth_params_get(&p_in, "refresh_token"))) {
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
		ic->flags |= OPT_SLOW_LOGIN;
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

		g_slist_foreach(md->mutes_ids, (GFunc) g_free, NULL);
		g_slist_free(md->mutes_ids);

		g_slist_foreach(md->noretweets_ids, (GFunc) g_free, NULL);
		g_slist_free(md->noretweets_ids);

		http_close(md->stream);
		mastodon_filter_remove_all(ic);
		g_free(md->oauth2_service->auth_url);
		g_free(md->oauth2_service->token_url);
		g_free(md->oauth2_service);
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
 *
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
		mastodon_direct_messages_new(ic, who, message);
	}
	return 0;
}

static void mastodon_get_info(struct im_connection *ic, char *who)
{
}

static void mastodon_add_buddy(struct im_connection *ic, char *who, char *group)
{
	mastodon_friendships_create_destroy(ic, who, 1);
}

static void mastodon_remove_buddy(struct im_connection *ic, char *who, char *group)
{
	mastodon_friendships_create_destroy(ic, who, 0);
}

static void mastodon_chat_msg(struct groupchat *c, char *message, int flags)
{
	if (c && message) {
		mastodon_handle_command(c->ic, message);
	}
}

static void mastodon_chat_invite(struct groupchat *c, char *who, char *message)
{
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
	   tweets come in. */
	imcb_chat_free(md->timeline_gc);
	md->timeline_gc = NULL;
}

static void mastodon_keepalive(struct im_connection *ic)
{
}

static void mastodon_add_permit(struct im_connection *ic, char *who)
{
}

static void mastodon_rem_permit(struct im_connection *ic, char *who)
{
}

static void mastodon_add_deny(struct im_connection *ic, char *who)
{
}

static void mastodon_rem_deny(struct im_connection *ic, char *who)
{
}

//static char *mastodon_set_display_name( set_t *set, char *value )
//{
//      return value;
//}

static void mastodon_buddy_data_add(struct bee_user *bu)
{
	bu->data = g_new0(struct mastodon_user_data, 1);
}

static void mastodon_buddy_data_free(struct bee_user *bu)
{
	g_free(bu->data);
}

bee_user_t mastodon_log_local_user;

/** Convert the given bitlbee tweet ID, bitlbee username, or mastodon tweet ID
 *  into a mastodon tweet ID.
 *
 *  Returns 0 if the user provides garbage.
 */
static guint64 mastodon_message_id_from_command_arg(struct im_connection *ic, char *arg, bee_user_t **bu_)
{
	struct mastodon_data *md = ic->proto_data;
	struct mastodon_user_data *tud;
	bee_user_t *bu = NULL;
	guint64 id = 0;

	if (bu_) {
		*bu_ = NULL;
	}
	if (!arg || !arg[0]) {
		return 0;
	}

	if (arg[0] != '#' && (bu = bee_user_by_handle(ic->bee, ic, arg))) {
		if ((tud = bu->data)) {
			id = tud->last_id;
		}
	} else {
		if (arg[0] == '#') {
			arg++;
		}
		if (parse_int64(arg, 16, &id) && id < MASTODON_LOG_LENGTH) {
			bu = md->log[id].bu;
			id = md->log[id].id;
		} else if (parse_int64(arg, 10, &id)) {
			/* Allow normal tweet IDs as well; not a very useful
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

static void mastodon_handle_command(struct im_connection *ic, char *message)
{
	struct mastodon_data *md = ic->proto_data;
	char *cmds, **cmd, *new = NULL;
	guint64 in_reply_to = 0, id;
	gboolean allow_post =
	        g_strcasecmp(set_getstr(&ic->acc->set, "commands"), "strict") != 0;
	bee_user_t *bu = NULL;

	cmds = g_strdup(message);
	cmd = split_command_parts(cmds, 2);

	if (cmd[0] == NULL) {
		goto eof;
	} else if (!set_getbool(&ic->acc->set, "commands") && allow_post) {
		/* Not supporting commands if "commands" is set to true/strict. */
	} else if (g_strcasecmp(cmd[0], "undo") == 0) {
		if (cmd[1] == NULL) {
			mastodon_status_destroy(ic, md->last_status_id);
		} else if ((id = mastodon_message_id_from_command_arg(ic, cmd[1], NULL))) {
			mastodon_status_destroy(ic, id);
		} else {
			mastodon_log(ic, "Could not undo last action");
		}

		goto eof;
	} else if ((g_strcasecmp(cmd[0], "favourite") == 0 ||
	            g_strcasecmp(cmd[0], "favorite") == 0 ||
	            g_strcasecmp(cmd[0], "fav") == 0 ||
	            g_strcasecmp(cmd[0], "like") == 0) && cmd[1]) {
		if ((id = mastodon_message_id_from_command_arg(ic, cmd[1], NULL))) {
			mastodon_favourite_tweet(ic, id);
		} else {
			mastodon_log(ic, "Please provide a message ID or username.");
		}
		goto eof;
	} else if (g_strcasecmp(cmd[0], "follow") == 0 && cmd[1]) {
		mastodon_add_buddy(ic, cmd[1], NULL);
		goto eof;
	} else if (g_strcasecmp(cmd[0], "unfollow") == 0 && cmd[1]) {
		mastodon_remove_buddy(ic, cmd[1], NULL);
		goto eof;
	} else if (g_strcasecmp(cmd[0], "mute") == 0 && cmd[1]) {
		mastodon_mute_create_destroy(ic, cmd[1], 1);
		goto eof;
	} else if (g_strcasecmp(cmd[0], "unmute") == 0 && cmd[1]) {
		mastodon_mute_create_destroy(ic, cmd[1], 0);
		goto eof;
	} else if ((g_strcasecmp(cmd[0], "report") == 0 ||
	            g_strcasecmp(cmd[0], "spam") == 0) && cmd[1]) {
		char *screen_name;

		/* Report nominally works on users but look up the user who
		   posted the given ID if the user wants to do it that way */
		mastodon_message_id_from_command_arg(ic, cmd[1], &bu);
		if (bu) {
			screen_name = bu->handle;
		} else {
			screen_name = cmd[1];
		}

		mastodon_report_spam(ic, screen_name);
		goto eof;
	} else if (g_strcasecmp(cmd[0], "rt") == 0 && cmd[1]) {
		id = mastodon_message_id_from_command_arg(ic, cmd[1], NULL);

		md->last_status_id = 0;
		if (id) {
			mastodon_status_retweet(ic, id);
		} else {
			mastodon_log(ic, "User `%s' does not exist or didn't "
			            "post any statuses recently", cmd[1]);
		}

		goto eof;
	} else if (g_strcasecmp(cmd[0], "reply") == 0 && cmd[1] && cmd[2]) {
		id = mastodon_message_id_from_command_arg(ic, cmd[1], &bu);
		if (!id || !bu) {
			mastodon_log(ic, "User `%s' does not exist or didn't "
			            "post any statuses recently", cmd[1]);
			goto eof;
		}
		message = new = g_strdup_printf("@%s %s", bu->handle, cmd[2]);
		in_reply_to = id;
		allow_post = TRUE;
	} else if (g_strcasecmp(cmd[0], "rawreply") == 0 && cmd[1] && cmd[2]) {
		id = mastodon_message_id_from_command_arg(ic, cmd[1], NULL);
		if (!id) {
			mastodon_log(ic, "Tweet `%s' does not exist", cmd[1]);
			goto eof;
		}
		message = cmd[2];
		in_reply_to = id;
		allow_post = TRUE;
	} else if (g_strcasecmp(cmd[0], "url") == 0) {
		id = mastodon_message_id_from_command_arg(ic, cmd[1], &bu);
		if (!id) {
			mastodon_log(ic, "Tweet `%s' does not exist", cmd[1]);
		} else {
			mastodon_status_show_url(ic, id);
		}
		goto eof;

	} else if (g_strcasecmp(cmd[0], "post") == 0) {
		message += 5;
		allow_post = TRUE;
	}

	if (allow_post) {
		char *s;

		if (!mastodon_length_check(ic, message)) {
			goto eof;
		}

		s = cmd[0] + strlen(cmd[0]) - 1;
		if (!new && s > cmd[0] && (*s == ':' || *s == ',')) {
			*s = '\0';

			if ((bu = bee_user_by_handle(ic->bee, ic, cmd[0]))) {
				struct mastodon_user_data *tud = bu->data;

				new = g_strdup_printf("@%s %s", bu->handle,
				                      message + (s - cmd[0]) + 2);
				message = new;

				if (time(NULL) < tud->last_time +
				    set_getint(&ic->acc->set, "auto_reply_timeout")) {
					in_reply_to = tud->last_id;
				}
			}
		}

		/* If the user runs undo between this request and its response
		   this would delete the second-last Tweet. Prevent that. */
		md->last_status_id = 0;
		mastodon_post_status(ic, message, in_reply_to);
	} else {
		mastodon_log(ic, "Unknown command: %s", cmd[0]);
	}
eof:
	g_free(new);
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
	ret->chat_invite = mastodon_chat_invite;
	ret->chat_join = mastodon_chat_join;
	ret->chat_leave = mastodon_chat_leave;
	ret->keepalive = mastodon_keepalive;
	ret->add_permit = mastodon_add_permit;
	ret->rem_permit = mastodon_rem_permit;
	ret->add_deny = mastodon_add_deny;
	ret->rem_deny = mastodon_rem_deny;
	ret->buddy_data_add = mastodon_buddy_data_add;
	ret->buddy_data_free = mastodon_buddy_data_free;
	ret->handle_cmp = g_strcasecmp;

	register_protocol(ret);
}