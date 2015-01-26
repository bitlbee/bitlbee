/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Simple module to facilitate twitter functionality.                       *
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

/* For strptime(): */
#if(__sun)
#else
#define _XOPEN_SOURCE
#endif

#include "twitter_http.h"
#include "twitter.h"
#include "bitlbee.h"
#include "url.h"
#include "misc.h"
#include "base64.h"
#include "twitter_lib.h"
#include "json_util.h"
#include <ctype.h>
#include <errno.h>

#define TXL_STATUS 1
#define TXL_USER 2
#define TXL_ID 3

struct twitter_xml_list {
	int type;
	gint64 next_cursor;
	GSList *list;
};

struct twitter_xml_user {
	char *name;
	char *screen_name;
};

struct twitter_xml_status {
	time_t created_at;
	char *text;
	struct twitter_xml_user *user;
	guint64 id, rt_id; /* Usually equal, with RTs id == *original* id */
	guint64 reply_to;
	struct twitter_xml_status *rt;
};

/**
 * Frees a twitter_xml_user struct.
 */
static void txu_free(struct twitter_xml_user *txu)
{
	if (txu == NULL)
		return;

	g_free(txu->name);
	g_free(txu->screen_name);
	g_free(txu);
}

/**
 * Frees a twitter_xml_status struct.
 */
static void txs_free(struct twitter_xml_status *txs)
{
	if (txs == NULL)
		return;

	g_free(txs->text);
	txu_free(txs->user);
	txs_free(txs->rt);
	g_free(txs);
}

/**
 * Free a twitter_xml_list struct.
 * type is the type of list the struct holds.
 */
static void txl_free(struct twitter_xml_list *txl)
{
	GSList *l;
	if (txl == NULL)
		return;

	for (l = txl->list; l; l = g_slist_next(l)) {
		if (txl->type == TXL_STATUS) {
			txs_free((struct twitter_xml_status *) l->data);
		} else if (txl->type == TXL_ID) {
			g_free(l->data);
		} else if (txl->type == TXL_USER) {
			txu_free(l->data);
		}
	}

	g_slist_free(txl->list);
	g_free(txl);
}

/**
 * Compare status elements
 */
static gint twitter_compare_elements(gconstpointer a, gconstpointer b)
{
	struct twitter_xml_status *a_status = (struct twitter_xml_status *) a;
	struct twitter_xml_status *b_status = (struct twitter_xml_status *) b;

	if (a_status->created_at < b_status->created_at) {
		return -1;
	} else if (a_status->created_at > b_status->created_at) {
		return 1;
	} else {
		return 0;
	}
}

/**
 * Add a buddy if it is not already added, set the status to logged in.
 */
static void twitter_add_buddy(struct im_connection *ic, char *name, const char *fullname)
{
	struct twitter_data *td = ic->proto_data;

	// Check if the buddy is already in the buddy list.
	if (!bee_user_by_handle(ic->bee, ic, name)) {
		// The buddy is not in the list, add the buddy and set the status to logged in.
		imcb_add_buddy(ic, name, NULL);
		imcb_rename_buddy(ic, name, fullname);
		if (td->flags & TWITTER_MODE_CHAT) {
			/* Necessary so that nicks always get translated to the
			   exact Twitter username. */
			imcb_buddy_nick_hint(ic, name, name);
			if (td->timeline_gc)
				imcb_chat_add_buddy(td->timeline_gc, name);
		} else if (td->flags & TWITTER_MODE_MANY)
			imcb_buddy_status(ic, name, OPT_LOGGED_IN, NULL, NULL);
	}
}

/* Warning: May return a malloc()ed value, which will be free()d on the next
   call. Only for short-term use. NOT THREADSAFE!  */
char *twitter_parse_error(struct http_request *req)
{
	static char *ret = NULL;
	json_value *root, *err;

	g_free(ret);
	ret = NULL;

	if (req->body_size > 0) {
		root = json_parse(req->reply_body, req->body_size);
		err = json_o_get(root, "errors");
		if (err && err->type == json_array && (err = err->u.array.values[0]) &&
		    err->type == json_object) {
			const char *msg = json_o_str(err, "message");
			if (msg)
				ret = g_strdup_printf("%s (%s)", req->status_string, msg);
		}
		json_value_free(root);
	}

	return ret ? ret : req->status_string;
}

/* WATCH OUT: This function might or might not destroy your connection.
   Sub-optimal indeed, but just be careful when this returns NULL! */
static json_value *twitter_parse_response(struct im_connection *ic, struct http_request *req)
{
	gboolean logging_in = !(ic->flags & OPT_LOGGED_IN);
	gboolean periodic;
	struct twitter_data *td = ic->proto_data;
	json_value *ret;
	char path[64] = "", *s;
	
	if ((s = strchr(req->request, ' '))) {
		path[sizeof(path)-1] = '\0';
		strncpy(path, s + 1, sizeof(path) - 1);
		if ((s = strchr(path, '?')) || (s = strchr(path, ' ')))
			*s = '\0';
	}
	
	/* Kinda nasty. :-( Trying to suppress error messages, but only
	   for periodic (i.e. mentions/timeline) queries. */
	periodic = strstr(path, "timeline") || strstr(path, "mentions");
	
	if (req->status_code == 401 && logging_in) {
		/* IIRC Twitter once had an outage where they were randomly
		   throwing 401s so I'll keep treating this one as fatal
		   only during login. */
		imcb_error(ic, "Authentication failure (%s)",
		               twitter_parse_error(req));
		imc_logout(ic, FALSE);
		return NULL;
	} else if (req->status_code != 200) {
		// It didn't go well, output the error and return.
		if (!periodic || logging_in || ++td->http_fails >= 5)
			twitter_log(ic, "Error: Could not retrieve %s: %s",
				    path, twitter_parse_error(req));
		
		if (logging_in)
			imc_logout(ic, TRUE);
		return NULL;
	} else {
		td->http_fails = 0;
	}

	if ((ret = json_parse(req->reply_body, req->body_size)) == NULL) {
		imcb_error(ic, "Could not retrieve %s: %s",
			   path, "XML parse error");
	}
	return ret;
}

static void twitter_http_get_friends_ids(struct http_request *req);

/**
 * Get the friends ids.
 */
void twitter_get_friends_ids(struct im_connection *ic, gint64 next_cursor)
{
	// Primitive, but hey! It works...
	char *args[2];
	args[0] = "cursor";
	args[1] = g_strdup_printf("%" G_GINT64_FORMAT, next_cursor);
	twitter_http(ic, TWITTER_FRIENDS_IDS_URL, twitter_http_get_friends_ids, ic, 0, args, 2);

	g_free(args[1]);
}

/**
 * Fill a list of ids.
 */
static gboolean twitter_xt_get_friends_id_list(json_value *node, struct twitter_xml_list *txl)
{
	json_value *c;
	int i;

	// Set the list type.
	txl->type = TXL_ID;

	c = json_o_get(node, "ids");
	if (!c || c->type != json_array)
		return FALSE;

	for (i = 0; i < c->u.array.length; i ++) {
		if (c->u.array.values[i]->type != json_integer)
			continue;
		
		txl->list = g_slist_prepend(txl->list,
			g_strdup_printf("%" PRIu64, c->u.array.values[i]->u.integer));
	}
	
	c = json_o_get(node, "next_cursor");
	if (c && c->type == json_integer)
		txl->next_cursor = c->u.integer;
	else
		txl->next_cursor = -1;
	
	return TRUE;
}

static void twitter_get_users_lookup(struct im_connection *ic);

/**
 * Callback for getting the friends ids.
 */
static void twitter_http_get_friends_ids(struct http_request *req)
{
	struct im_connection *ic;
	json_value *parsed;
	struct twitter_xml_list *txl;
	struct twitter_data *td;

	ic = req->data;

	// Check if the connection is still active.
	if (!g_slist_find(twitter_connections, ic))
		return;

	td = ic->proto_data;

	txl = g_new0(struct twitter_xml_list, 1);
	txl->list = td->follow_ids;

	// Parse the data.
	if (!(parsed = twitter_parse_response(ic, req)))
		return;
	
	twitter_xt_get_friends_id_list(parsed, txl);
	json_value_free(parsed);

	td->follow_ids = txl->list;
	if (txl->next_cursor)
		/* These were just numbers. Up to 4000 in a response AFAIK so if we get here
		   we may be using a spammer account. \o/ */
		twitter_get_friends_ids(ic, txl->next_cursor);
	else
		/* Now to convert all those numbers into names.. */
		twitter_get_users_lookup(ic);

	txl->list = NULL;
	txl_free(txl);
}

static gboolean twitter_xt_get_users(json_value *node, struct twitter_xml_list *txl);
static void twitter_http_get_users_lookup(struct http_request *req);

static void twitter_get_users_lookup(struct im_connection *ic)
{
	struct twitter_data *td = ic->proto_data;
	char *args[2] = {
		"user_id",
		NULL,
	};
	GString *ids = g_string_new("");
	int i;
	
	/* We can request up to 100 users at a time. */
	for (i = 0; i < 100 && td->follow_ids; i ++) {
		g_string_append_printf(ids, ",%s", (char*) td->follow_ids->data);
		g_free(td->follow_ids->data);
		td->follow_ids = g_slist_remove(td->follow_ids, td->follow_ids->data);
	}
	if (ids->len > 0) {
		args[1] = ids->str + 1;
		/* POST, because I think ids can be up to 1KB long. */
		twitter_http(ic, TWITTER_USERS_LOOKUP_URL, twitter_http_get_users_lookup, ic, 1, args, 2);
	} else {
		/* We have all users. Continue with login. (Get statuses.) */
		td->flags |= TWITTER_HAVE_FRIENDS;
		twitter_login_finish(ic);
	}
	g_string_free(ids, TRUE);
}

/**
 * Callback for getting (twitter)friends...
 *
 * Be afraid, be very afraid! This function will potentially add hundreds of "friends". "Who has 
 * hundreds of friends?" you wonder? You probably not, since you are reading the source of 
 * BitlBee... Get a life and meet new people!
 */
static void twitter_http_get_users_lookup(struct http_request *req)
{
	struct im_connection *ic = req->data;
	json_value *parsed;
	struct twitter_xml_list *txl;
	GSList *l = NULL;
	struct twitter_xml_user *user;

	// Check if the connection is still active.
	if (!g_slist_find(twitter_connections, ic))
		return;

	txl = g_new0(struct twitter_xml_list, 1);
	txl->list = NULL;

	// Get the user list from the parsed xml feed.
	if (!(parsed = twitter_parse_response(ic, req)))
		return;
	twitter_xt_get_users(parsed, txl);
	json_value_free(parsed);

	// Add the users as buddies.
	for (l = txl->list; l; l = g_slist_next(l)) {
		user = l->data;
		twitter_add_buddy(ic, user->screen_name, user->name);
	}

	// Free the structure.
	txl_free(txl);

	twitter_get_users_lookup(ic);
}

struct twitter_xml_user *twitter_xt_get_user(const json_value *node)
{
	struct twitter_xml_user *txu;
	
	txu = g_new0(struct twitter_xml_user, 1);
	txu->name = g_strdup(json_o_str(node, "name"));
	txu->screen_name = g_strdup(json_o_str(node, "screen_name"));
	
	return txu;
}

/**
 * Function to fill a twitter_xml_list struct.
 * It sets:
 *  - all <user>s from the <users> element.
 */
static gboolean twitter_xt_get_users(json_value *node, struct twitter_xml_list *txl)
{
	struct twitter_xml_user *txu;
	int i;

	// Set the type of the list.
	txl->type = TXL_USER;

	if (!node || node->type != json_array)
		return FALSE;

	// The root <users> node should hold the list of users <user>
	// Walk over the nodes children.
	for (i = 0; i < node->u.array.length; i ++) {
		txu = twitter_xt_get_user(node->u.array.values[i]);
		if (txu)
			txl->list = g_slist_prepend(txl->list, txu);
	}

	return TRUE;
}

#ifdef __GLIBC__
#define TWITTER_TIME_FORMAT "%a %b %d %H:%M:%S %z %Y"
#else
#define TWITTER_TIME_FORMAT "%a %b %d %H:%M:%S +0000 %Y"
#endif

static char* expand_entities(char* text, const json_value *entities);

/**
 * Function to fill a twitter_xml_status struct.
 * It sets:
 *  - the status text and
 *  - the created_at timestamp and
 *  - the status id and
 *  - the user in a twitter_xml_user struct.
 */
static struct twitter_xml_status *twitter_xt_get_status(const json_value *node)
{
	struct twitter_xml_status *txs;
	const json_value *rt = NULL, *entities = NULL;
	
	if (node->type != json_object)
		return FALSE;
	txs = g_new0(struct twitter_xml_status, 1);

	JSON_O_FOREACH (node, k, v) {
		if (strcmp("text", k) == 0 && v->type == json_string) {
			txs->text = g_memdup(v->u.string.ptr, v->u.string.length + 1);
			strip_html(txs->text);
		} else if (strcmp("retweeted_status", k) == 0 && v->type == json_object) {
			rt = v;
		} else if (strcmp("created_at", k) == 0 && v->type == json_string) {
			struct tm parsed;

			/* Very sensitive to changes to the formatting of
			   this field. :-( Also assumes the timezone used
			   is UTC since C time handling functions suck. */
			if (strptime(v->u.string.ptr, TWITTER_TIME_FORMAT, &parsed) != NULL)
				txs->created_at = mktime_utc(&parsed);
		} else if (strcmp("user", k) == 0 && v->type == json_object) {
			txs->user = twitter_xt_get_user(v);
		} else if (strcmp("id", k) == 0 && v->type == json_integer) {
			txs->rt_id = txs->id = v->u.integer;
		} else if (strcmp("in_reply_to_status_id", k) == 0 && v->type == json_integer) {
			txs->reply_to = v->u.integer;
		} else if (strcmp("entities", k) == 0 && v->type == json_object) {
			entities = v;
		}
	}

	/* If it's a (truncated) retweet, get the original. Even if the API claims it
	   wasn't truncated because it may be lying. */
	if (rt) {
		struct twitter_xml_status *rtxs = twitter_xt_get_status(rt);
		if (rtxs) {
			g_free(txs->text);
			txs->text = g_strdup(rtxs->text);
			txs->id = rtxs->id;
			txs->rt = rtxs;
		}
	} else if (entities) {
		txs->text = expand_entities(txs->text, entities);
	}

	if (txs->text && txs->user && txs->id)
		return txs;
	
	txs_free(txs);
	return NULL;
}

/**
 * Function to fill a twitter_xml_status struct (DM variant).
 */
static struct twitter_xml_status *twitter_xt_get_dm(const json_value *node)
{
	struct twitter_xml_status *txs;
	const json_value *entities = NULL;
	
	if (node->type != json_object)
		return FALSE;
	txs = g_new0(struct twitter_xml_status, 1);

	JSON_O_FOREACH (node, k, v) {
		if (strcmp("text", k) == 0 && v->type == json_string) {
			txs->text = g_memdup(v->u.string.ptr, v->u.string.length + 1);
			strip_html(txs->text);
		} else if (strcmp("created_at", k) == 0 && v->type == json_string) {
			struct tm parsed;

			/* Very sensitive to changes to the formatting of
			   this field. :-( Also assumes the timezone used
			   is UTC since C time handling functions suck. */
			if (strptime(v->u.string.ptr, TWITTER_TIME_FORMAT, &parsed) != NULL)
				txs->created_at = mktime_utc(&parsed);
		} else if (strcmp("sender", k) == 0 && v->type == json_object) {
			txs->user = twitter_xt_get_user(v);
		} else if (strcmp("id", k) == 0 && v->type == json_integer) {
			txs->id = v->u.integer;
		}
	}

	if (entities) {
		txs->text = expand_entities(txs->text, entities);
	}

	if (txs->text && txs->user && txs->id)
		return txs;
	
	txs_free(txs);
	return NULL;
}

static char* expand_entities(char* text, const json_value *entities) {
	JSON_O_FOREACH (entities, k, v) {
		int i;
		
		if (v->type != json_array)
			continue;
		if (strcmp(k, "urls") != 0 && strcmp(k, "media") != 0)
			continue;
		
		for (i = 0; i < v->u.array.length; i ++) {
			if (v->u.array.values[i]->type != json_object)
				continue;
			
			const char *kort = json_o_str(v->u.array.values[i], "url");
			const char *disp = json_o_str(v->u.array.values[i], "display_url");
			char *pos, *new;
			
			if (!kort || !disp || !(pos = strstr(text, kort)))
				continue;
			
			*pos = '\0';
			new = g_strdup_printf("%s%s <%s>%s", text, kort,
			                      disp, pos + strlen(kort));
			
			g_free(text);
			text = new;
		}
	}
	
	return text;
}

/**
 * Function to fill a twitter_xml_list struct.
 * It sets:
 *  - all <status>es within the <status> element and
 *  - the next_cursor.
 */
static gboolean twitter_xt_get_status_list(struct im_connection *ic, const json_value *node,
                                           struct twitter_xml_list *txl)
{
	struct twitter_xml_status *txs;
	int i;

	// Set the type of the list.
	txl->type = TXL_STATUS;
	
	if (node->type != json_array)
		return FALSE;

	// The root <statuses> node should hold the list of statuses <status>
	// Walk over the nodes children.
	for (i = 0; i < node->u.array.length; i ++) {
		txs = twitter_xt_get_status(node->u.array.values[i]);
		if (!txs)
			continue;
		
		txl->list = g_slist_prepend(txl->list, txs);
	}

	return TRUE;
}

/**
 * Function to properly format a tweet as per the users configuration.
 */
static char *twitter_msg_get_text(struct im_connection *ic, int log_id, int reply_to,
				struct twitter_xml_status *txs, const char *prefix) {
	gchar * format = set_getstr(&ic->acc->set, "format_string");
	GString * text = g_string_new(NULL);

	gchar *c;
	if (reply_to != -1)
		format = set_getstr(&ic->acc->set, "reply_format_string");
	if (txs->rt)
		format = set_getstr(&ic->acc->set, "retweet_format_string");

	for (c = format; *c ; c++) {
		if (!(*c == '%' && *(c+1))) {
			text = g_string_append_c(text, *c);
			continue;
		}
		c++; // Move past the %
		switch (*c) {
			case 'i':
				g_string_append_printf(text, "%02x", log_id);
				break;
			case 'r':
				if (reply_to != -1) // In case someone does put %r in the wrong format_string
				g_string_append_printf(text, "%02x", reply_to);
				break;
			case 'a':
				if (txs->rt) // In case someone does put %a in the wrong format_string
					text = g_string_append(text, txs->rt->user->screen_name);
				break;
			case 'c':
				text = g_string_append(text, txs->text);
				break;
			default:
				text = g_string_append_c(text, *c);
		}
	}
	text = g_string_prepend(text, prefix);
	return g_string_free(text, FALSE);
}

/* Will log messages either way. Need to keep track of IDs for stream deduping.
   Plus, show_ids is on by default and I don't see why anyone would disable it. */
static char *twitter_msg_add_id(struct im_connection *ic,
				struct twitter_xml_status *txs, const char *prefix)
{
	struct twitter_data *td = ic->proto_data;
	int reply_to = -1;
	bee_user_t *bu;

	if (txs->reply_to) {
		int i;
		for (i = 0; i < TWITTER_LOG_LENGTH; i++)
			if (td->log[i].id == txs->reply_to) {
				reply_to = i;
				break;
			}
	}

	if (txs->user && txs->user->screen_name &&
	    (bu = bee_user_by_handle(ic->bee, ic, txs->user->screen_name))) {
		struct twitter_user_data *tud = bu->data;

		if (txs->id > tud->last_id) {
			tud->last_id = txs->id;
			tud->last_time = txs->created_at;
		}
	}
	
	td->log_id = (td->log_id + 1) % TWITTER_LOG_LENGTH;
	td->log[td->log_id].id = txs->id;
	td->log[td->log_id].bu = bee_user_by_handle(ic->bee, ic, txs->user->screen_name);
	
	/* This is all getting hairy. :-( If we RT'ed something ourselves,
	   remember OUR id instead so undo will work. In other cases, the
	   original tweet's id should be remembered for deduplicating. */
	if (g_strcasecmp(txs->user->screen_name, td->user) == 0)
		td->log[td->log_id].id = txs->rt_id;
	
	return twitter_msg_get_text(ic, td->log_id, reply_to, txs, prefix);
}

/**
 * Function that is called to see the statuses in a groupchat window.
 */
static void twitter_status_show_chat(struct im_connection *ic, struct twitter_xml_status *status)
{
	struct twitter_data *td = ic->proto_data;
	struct groupchat *gc;
	gboolean me = g_strcasecmp(td->user, status->user->screen_name) == 0;
	char *msg;

	// Create a new groupchat if it does not exsist.
	gc = twitter_groupchat_init(ic);

	if (!me)
		/* MUST be done before twitter_msg_add_id() to avoid #872. */
		twitter_add_buddy(ic, status->user->screen_name, status->user->name);
	msg = twitter_msg_add_id(ic, status, "");
	
	// Say it!
	if (me) {
		imcb_chat_log(gc, "You: %s", msg ? msg : status->text);
	} else {
		imcb_chat_msg(gc, status->user->screen_name,
			      msg ? msg : status->text, 0, status->created_at);
	}

	g_free(msg);
}

/**
 * Function that is called to see statuses as private messages.
 */
static void twitter_status_show_msg(struct im_connection *ic, struct twitter_xml_status *status)
{
	struct twitter_data *td = ic->proto_data;
	char from[MAX_STRING] = "";
	char *prefix = NULL, *text = NULL;
	gboolean me = g_strcasecmp(td->user, status->user->screen_name) == 0;

	if (td->flags & TWITTER_MODE_ONE) {
		g_snprintf(from, sizeof(from) - 1, "%s_%s", td->prefix, ic->acc->user);
		from[MAX_STRING - 1] = '\0';
	}

	if (td->flags & TWITTER_MODE_ONE)
		prefix = g_strdup_printf("\002<\002%s\002>\002 ",
		                         status->user->screen_name);
	else if (!me)
		twitter_add_buddy(ic, status->user->screen_name, status->user->name);
	else
		prefix = g_strdup("You: ");

	text = twitter_msg_add_id(ic, status, prefix ? prefix : "");

	imcb_buddy_msg(ic,
	               *from ? from : status->user->screen_name,
	               text ? text : status->text, 0, status->created_at);

	g_free(text);
	g_free(prefix);
}

static void twitter_status_show(struct im_connection *ic, struct twitter_xml_status *status)
{
	struct twitter_data *td = ic->proto_data;
	char *last_id_str;
	
	if (status->user == NULL || status->text == NULL)
		return;
	
	/* Grrrr. Would like to do this during parsing, but can't access
	   settings from there. */
	if (set_getbool(&ic->acc->set, "strip_newlines"))
		strip_newlines(status->text);
	
	if (td->flags & TWITTER_MODE_CHAT)
		twitter_status_show_chat(ic, status);
	else
		twitter_status_show_msg(ic, status);

	// Update the timeline_id to hold the highest id, so that by the next request
	// we won't pick up the updates already in the list.
	td->timeline_id = MAX(td->timeline_id, status->rt_id);

	last_id_str = g_strdup_printf("%" G_GUINT64_FORMAT, td->timeline_id);
	set_setstr(&ic->acc->set, "last_tweet", last_id_str);
	g_free(last_id_str);
}

static gboolean twitter_stream_handle_object(struct im_connection *ic, json_value *o);

static void twitter_http_stream(struct http_request *req)
{
	struct im_connection *ic = req->data;
	struct twitter_data *td;
	json_value *parsed;
	int len = 0;
	char c, *nl;
	
	if (!g_slist_find(twitter_connections, ic))
		return;
	
	ic->flags |= OPT_PONGED;
	td = ic->proto_data;
	
	if ((req->flags & HTTPC_EOF) || !req->reply_body) {
		td->stream = NULL;
		imcb_error(ic, "Stream closed (%s)", req->status_string);
		imc_logout(ic, TRUE);
		return;
	}
	
	/* MUST search for CRLF, not just LF:
	   https://dev.twitter.com/docs/streaming-apis/processing#Parsing_responses */
	if (!(nl = strstr(req->reply_body, "\r\n")))
		return;
	
	len = nl - req->reply_body;
	if (len > 0) {
		c = req->reply_body[len];
		req->reply_body[len] = '\0';
		
		if ((parsed = json_parse(req->reply_body, req->body_size))) {
			twitter_stream_handle_object(ic, parsed);
		}
		json_value_free(parsed);
		req->reply_body[len] = c;
	}
	
	http_flush_bytes(req, len + 2);
	
	/* One notification might bring multiple events! */
	if (req->body_size > 0)
		twitter_http_stream(req);
}

static gboolean twitter_stream_handle_event(struct im_connection *ic, json_value *o);
static gboolean twitter_stream_handle_status(struct im_connection *ic, struct twitter_xml_status *txs);

static gboolean twitter_stream_handle_object(struct im_connection *ic, json_value *o)
{
	struct twitter_data *td = ic->proto_data;
	struct twitter_xml_status *txs;
	json_value *c;
	
	if ((txs = twitter_xt_get_status(o))) {
		gboolean ret = twitter_stream_handle_status(ic, txs);
		txs_free(txs);
		return ret;
	} else if ((c = json_o_get(o, "direct_message")) &&
	           (txs = twitter_xt_get_dm(c))) {
		if (g_strcasecmp(txs->user->screen_name, td->user) != 0)
			imcb_buddy_msg(ic, txs->user->screen_name,
				       txs->text, 0, txs->created_at);
		txs_free(txs);
		return TRUE;
	} else if ((c = json_o_get(o, "event")) && c->type == json_string) {
		twitter_stream_handle_event(ic, o);
		return TRUE;
	} else if ((c = json_o_get(o, "disconnect")) && c->type == json_object) {
		/* HACK: Because we're inside an event handler, we can't just
		   disconnect here. Instead, just change the HTTP status string
		   into a Twitter status string. */
		char *reason = json_o_strdup(c, "reason");
		if (reason) {
			g_free(td->stream->status_string);
			td->stream->status_string = reason;
		}
		return TRUE;
	}
	return FALSE;
}

static gboolean twitter_stream_handle_status(struct im_connection *ic, struct twitter_xml_status *txs)
{
	struct twitter_data *td = ic->proto_data;
	int i;
	
	for (i = 0; i < TWITTER_LOG_LENGTH; i++) {
		if (td->log[i].id == txs->id) {
			/* Got a duplicate (RT, probably). Drop it. */
			return TRUE;
		}
	}
	
	if (!(g_strcasecmp(txs->user->screen_name, td->user) == 0 ||
	      set_getbool(&ic->acc->set, "fetch_mentions") ||
	      bee_user_by_handle(ic->bee, ic, txs->user->screen_name))) {
		/* Tweet is from an unknown person and the user does not want
		   to see @mentions, so drop it. twitter_stream_handle_event()
		   picks up new follows so this simple filter should be safe. */
		/* TODO: The streaming API seems to do poor @mention matching.
		   I.e. I'm getting mentions for @WilmerSomething, not just for
		   @Wilmer. But meh. You want spam, you get spam. */
		return TRUE;
	}
	
	twitter_status_show(ic, txs);
	
	return TRUE;
}

static gboolean twitter_stream_handle_event(struct im_connection *ic, json_value *o)
{
	struct twitter_data *td = ic->proto_data;
	json_value *source = json_o_get(o, "source");
	json_value *target = json_o_get(o, "target");
	const char *type = json_o_str(o, "event");
	
	if (!type || !source || source->type != json_object
	          || !target || target->type != json_object) {
		return FALSE;
	}
	
	if (strcmp(type, "follow") == 0) {
		struct twitter_xml_user *us = twitter_xt_get_user(source);
		struct twitter_xml_user *ut = twitter_xt_get_user(target);
		if (g_strcasecmp(us->screen_name, td->user) == 0) {
			twitter_add_buddy(ic, ut->screen_name, ut->name);
		}
		txu_free(us);
		txu_free(ut);
	}
	
	return TRUE;
}

gboolean twitter_open_stream(struct im_connection *ic)
{
	struct twitter_data *td = ic->proto_data;
	char *args[2] = {"with", "followings"};
	
	if ((td->stream = twitter_http(ic, TWITTER_USER_STREAM_URL,
	                               twitter_http_stream, ic, 0, args, 2))) {
		/* This flag must be enabled or we'll get no data until EOF
		   (which err, kind of, defeats the purpose of a streaming API). */
		td->stream->flags |= HTTPC_STREAMING;
		return TRUE;
	}
	
	return FALSE;
}

static void twitter_get_home_timeline(struct im_connection *ic, gint64 next_cursor);
static void twitter_get_mentions(struct im_connection *ic, gint64 next_cursor);

/**
 * Get the timeline with optionally mentions
 */
gboolean twitter_get_timeline(struct im_connection *ic, gint64 next_cursor)
{
	struct twitter_data *td = ic->proto_data;
	gboolean include_mentions = set_getbool(&ic->acc->set, "fetch_mentions");

	if (td->flags & TWITTER_DOING_TIMELINE) {
		if (++td->http_fails >= 5) {
			imcb_error(ic, "Fetch timeout (%d)", td->flags);
			imc_logout(ic, TRUE);
			return FALSE;
		}
	}

	td->flags |= TWITTER_DOING_TIMELINE;

	twitter_get_home_timeline(ic, next_cursor);

	if (include_mentions) {
		twitter_get_mentions(ic, next_cursor);
	}
	
	return TRUE;
}

/**
 * Call this one after receiving timeline/mentions. Show to user once we have
 * both.
 */
void twitter_flush_timeline(struct im_connection *ic)
{
	struct twitter_data *td = ic->proto_data;
	gboolean include_mentions = set_getbool(&ic->acc->set, "fetch_mentions");
	int show_old_mentions = set_getint(&ic->acc->set, "show_old_mentions");
	struct twitter_xml_list *home_timeline = td->home_timeline_obj;
	struct twitter_xml_list *mentions = td->mentions_obj;
	guint64 last_id = 0;
	GSList *output = NULL;
	GSList *l;

	imcb_connected(ic);
	
	if (!(td->flags & TWITTER_GOT_TIMELINE)) {
		return;
	}

	if (include_mentions && !(td->flags & TWITTER_GOT_MENTIONS)) {
		return;
	}

	if (home_timeline && home_timeline->list) {
		for (l = home_timeline->list; l; l = g_slist_next(l)) {
			output = g_slist_insert_sorted(output, l->data, twitter_compare_elements);
		}
	}

	if (include_mentions && mentions && mentions->list) {
		for (l = mentions->list; l; l = g_slist_next(l)) {
			if (show_old_mentions < 1 && output && twitter_compare_elements(l->data, output->data) < 0) {
				continue;
			}

			output = g_slist_insert_sorted(output, l->data, twitter_compare_elements);
		}
	}

	// See if the user wants to see the messages in a groupchat window or as private messages.
	while (output) {
		struct twitter_xml_status *txs = output->data;
		if (txs->id != last_id)
			twitter_status_show(ic, txs);
		last_id = txs->id;
		output = g_slist_remove(output, txs);
	}

	txl_free(home_timeline);
	txl_free(mentions);

	td->flags &= ~(TWITTER_DOING_TIMELINE | TWITTER_GOT_TIMELINE | TWITTER_GOT_MENTIONS);
	td->home_timeline_obj = td->mentions_obj = NULL;
}

static void twitter_http_get_home_timeline(struct http_request *req);
static void twitter_http_get_mentions(struct http_request *req);

/**
 * Get the timeline.
 */
static void twitter_get_home_timeline(struct im_connection *ic, gint64 next_cursor)
{
	struct twitter_data *td = ic->proto_data;

	txl_free(td->home_timeline_obj);
	td->home_timeline_obj = NULL;
	td->flags &= ~TWITTER_GOT_TIMELINE;

	char *args[6];
	args[0] = "cursor";
	args[1] = g_strdup_printf("%" G_GINT64_FORMAT, next_cursor);
	args[2] = "include_entities";
	args[3] = "true";
	if (td->timeline_id) {
		args[4] = "since_id";
		args[5] = g_strdup_printf("%" G_GUINT64_FORMAT, td->timeline_id);
	}

	if (twitter_http(ic, TWITTER_HOME_TIMELINE_URL, twitter_http_get_home_timeline, ic, 0, args,
		     td->timeline_id ? 6 : 4) == NULL) {
		if (++td->http_fails >= 5)
			imcb_error(ic, "Could not retrieve %s: %s",
			           TWITTER_HOME_TIMELINE_URL, "connection failed");
		td->flags |= TWITTER_GOT_TIMELINE;
		twitter_flush_timeline(ic);
	}

	g_free(args[1]);
	if (td->timeline_id) {
		g_free(args[5]);
	}
}

/**
 * Get mentions.
 */
static void twitter_get_mentions(struct im_connection *ic, gint64 next_cursor)
{
	struct twitter_data *td = ic->proto_data;

	txl_free(td->mentions_obj);
	td->mentions_obj = NULL;
	td->flags &= ~TWITTER_GOT_MENTIONS;

	char *args[6];
	args[0] = "cursor";
	args[1] = g_strdup_printf("%" G_GINT64_FORMAT, next_cursor);
	args[2] = "include_entities";
	args[3] = "true";
	if (td->timeline_id) {
		args[4] = "since_id";
		args[5] = g_strdup_printf("%" G_GUINT64_FORMAT, td->timeline_id);
	} else {
		args[4] = "count";
		args[5] = g_strdup_printf("%d", set_getint(&ic->acc->set, "show_old_mentions"));
	}

	if (twitter_http(ic, TWITTER_MENTIONS_URL, twitter_http_get_mentions,
	                 ic, 0, args, 6) == NULL) {
		if (++td->http_fails >= 5)
			imcb_error(ic, "Could not retrieve %s: %s",
			           TWITTER_MENTIONS_URL, "connection failed");
		td->flags |= TWITTER_GOT_MENTIONS;
		twitter_flush_timeline(ic);
	}

	g_free(args[1]);
	g_free(args[5]);
}

/**
 * Callback for getting the home timeline.
 */
static void twitter_http_get_home_timeline(struct http_request *req)
{
	struct im_connection *ic = req->data;
	struct twitter_data *td;
	json_value *parsed;
	struct twitter_xml_list *txl;

	// Check if the connection is still active.
	if (!g_slist_find(twitter_connections, ic))
		return;

	td = ic->proto_data;

	txl = g_new0(struct twitter_xml_list, 1);
	txl->list = NULL;

	// The root <statuses> node should hold the list of statuses <status>
	if (!(parsed = twitter_parse_response(ic, req)))
		goto end;
	twitter_xt_get_status_list(ic, parsed, txl);
	json_value_free(parsed);

	td->home_timeline_obj = txl;

      end:
	if (!g_slist_find(twitter_connections, ic))
		return;

	td->flags |= TWITTER_GOT_TIMELINE;

	twitter_flush_timeline(ic);
}

/**
 * Callback for getting mentions.
 */
static void twitter_http_get_mentions(struct http_request *req)
{
	struct im_connection *ic = req->data;
	struct twitter_data *td;
	json_value *parsed;
	struct twitter_xml_list *txl;

	// Check if the connection is still active.
	if (!g_slist_find(twitter_connections, ic))
		return;

	td = ic->proto_data;

	txl = g_new0(struct twitter_xml_list, 1);
	txl->list = NULL;

	// The root <statuses> node should hold the list of statuses <status>
	if (!(parsed = twitter_parse_response(ic, req)))
		goto end;
	twitter_xt_get_status_list(ic, parsed, txl);
	json_value_free(parsed);

	td->mentions_obj = txl;

      end:
	if (!g_slist_find(twitter_connections, ic))
		return;

	td->flags |= TWITTER_GOT_MENTIONS;

	twitter_flush_timeline(ic);
}

/**
 * Callback to use after sending a POST request to twitter.
 * (Generic, used for a few kinds of queries.)
 */
static void twitter_http_post(struct http_request *req)
{
	struct im_connection *ic = req->data;
	struct twitter_data *td;
	json_value *parsed, *id;

	// Check if the connection is still active.
	if (!g_slist_find(twitter_connections, ic))
		return;

	td = ic->proto_data;
	td->last_status_id = 0;

	if (!(parsed = twitter_parse_response(ic, req)))
		return;
	
	if ((id = json_o_get(parsed, "id")) && id->type == json_integer) {
		td->last_status_id = id->u.integer;
	}
	
	json_value_free(parsed);
	
	if (req->flags & TWITTER_HTTP_USER_ACK)
		twitter_log(ic, "Command processed successfully");
}

/**
 * Function to POST a new status to twitter.
 */
void twitter_post_status(struct im_connection *ic, char *msg, guint64 in_reply_to)
{
	char *args[4] = {
		"status", msg,
		"in_reply_to_status_id",
		g_strdup_printf("%" G_GUINT64_FORMAT, in_reply_to)
	};
	twitter_http(ic, TWITTER_STATUS_UPDATE_URL, twitter_http_post, ic, 1,
		     args, in_reply_to ? 4 : 2);
	g_free(args[3]);
}


/**
 * Function to POST a new message to twitter.
 */
void twitter_direct_messages_new(struct im_connection *ic, char *who, char *msg)
{
	char *args[4];
	args[0] = "screen_name";
	args[1] = who;
	args[2] = "text";
	args[3] = msg;
	// Use the same callback as for twitter_post_status, since it does basically the same.
	twitter_http(ic, TWITTER_DIRECT_MESSAGES_NEW_URL, twitter_http_post, ic, 1, args, 4);
}

void twitter_friendships_create_destroy(struct im_connection *ic, char *who, int create)
{
	char *args[2];
	args[0] = "screen_name";
	args[1] = who;
	twitter_http(ic, create ? TWITTER_FRIENDSHIPS_CREATE_URL : TWITTER_FRIENDSHIPS_DESTROY_URL,
		     twitter_http_post, ic, 1, args, 2);
}

void twitter_status_destroy(struct im_connection *ic, guint64 id)
{
	char *url;
	url = g_strdup_printf("%s%" G_GUINT64_FORMAT "%s",
	                      TWITTER_STATUS_DESTROY_URL, id, ".json");
	twitter_http_f(ic, url, twitter_http_post, ic, 1, NULL, 0,
	               TWITTER_HTTP_USER_ACK);
	g_free(url);
}

void twitter_status_retweet(struct im_connection *ic, guint64 id)
{
	char *url;
	url = g_strdup_printf("%s%" G_GUINT64_FORMAT "%s",
	                      TWITTER_STATUS_RETWEET_URL, id, ".json");
	twitter_http_f(ic, url, twitter_http_post, ic, 1, NULL, 0,
	               TWITTER_HTTP_USER_ACK);
	g_free(url);
}

/**
 * Report a user for sending spam.
 */
void twitter_report_spam(struct im_connection *ic, char *screen_name)
{
	char *args[2] = {
		"screen_name",
		NULL,
	};
	args[1] = screen_name;
	twitter_http_f(ic, TWITTER_REPORT_SPAM_URL, twitter_http_post,
	               ic, 1, args, 2, TWITTER_HTTP_USER_ACK);
}

/**
 * Favourite a tweet.
 */
void twitter_favourite_tweet(struct im_connection *ic, guint64 id)
{
	char *args[2] = {
		"id",
		NULL,
	};
	args[1] = g_strdup_printf("%" G_GUINT64_FORMAT, id);
	twitter_http_f(ic, TWITTER_FAVORITE_CREATE_URL, twitter_http_post,
	               ic, 1, args, 2, TWITTER_HTTP_USER_ACK);
	g_free(args[1]);
}
