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

/* For strptime(): */
#if (__sun)
#else
#define _XOPEN_SOURCE
#endif

#include "mastodon_http.h"
#include "mastodon.h"
#include "bitlbee.h"
#include "url.h"
#include "misc.h"
#include "base64.h"
#include "mastodon_lib.h"
#include "json_util.h"
#include <ctype.h>
#include <errno.h>

#define TXL_STATUS 1
#define TXL_USER 2
#define TXL_ID 3

struct mastodon_xml_list {
	int type;
	gint64 next_cursor;
	GSList *list;
};

struct mastodon_xml_user {
	guint64 uid;
	char *name;
	char *screen_name;
};

struct mastodon_xml_status {
	time_t created_at;
	char *text;
	struct mastodon_xml_user *user;
	guint64 id, rt_id; /* Usually equal, with RTs id == *original* id */
	guint64 reply_to;
	gboolean from_filter;
};

/**
 * Frees a mastodon_xml_user struct.
 */
static void txu_free(struct mastodon_xml_user *txu)
{
	if (txu == NULL) {
		return;
	}

	g_free(txu->name);
	g_free(txu->screen_name);
	g_free(txu);
}

/**
 * Frees a mastodon_xml_status struct.
 */
static void txs_free(struct mastodon_xml_status *txs)
{
	if (txs == NULL) {
		return;
	}

	g_free(txs->text);
	txu_free(txs->user);
	g_free(txs);
}

/**
 * Free a mastodon_xml_list struct.
 * type is the type of list the struct holds.
 */
static void txl_free(struct mastodon_xml_list *txl)
{
	GSList *l;

	if (txl == NULL) {
		return;
	}

	for (l = txl->list; l; l = g_slist_next(l)) {
		if (txl->type == TXL_STATUS) {
			txs_free((struct mastodon_xml_status *) l->data);
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
static gint mastodon_compare_elements(gconstpointer a, gconstpointer b)
{
	struct mastodon_xml_status *a_status = (struct mastodon_xml_status *) a;
	struct mastodon_xml_status *b_status = (struct mastodon_xml_status *) b;

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
static void mastodon_add_buddy(struct im_connection *ic, char *name, const char *fullname)
{
	struct mastodon_data *md = ic->proto_data;

	// Check if the buddy is already in the buddy list.
	if (!bee_user_by_handle(ic->bee, ic, name)) {
		// The buddy is not in the list, add the buddy and set the status to logged in.
		imcb_add_buddy(ic, name, NULL);
		imcb_rename_buddy(ic, name, fullname);
		if (md->flags & MASTODON_MODE_CHAT) {
			/* Necessary so that nicks always get translated to the
			   exact Mastodon username. */
			imcb_buddy_nick_hint(ic, name, name);
			if (md->timeline_gc) {
				imcb_chat_add_buddy(md->timeline_gc, name);
			}
		} else if (md->flags & MASTODON_MODE_MANY) {
			imcb_buddy_status(ic, name, OPT_LOGGED_IN, NULL, NULL);
		}
	}
}

/* Warning: May return a malloc()ed value, which will be free()d on the next
   call. Only for short-term use. NOT THREADSAFE!  */
char *mastodon_parse_error(struct http_request *req)
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
			if (msg) {
				ret = g_strdup_printf("%s (%s)", req->status_string, msg);
			}
		}
		json_value_free(root);
	}

	return ret ? ret : req->status_string;
}

/* WATCH OUT: This function might or might not destroy your connection.
   Sub-optimal indeed, but just be careful when this returns NULL! */
static json_value *mastodon_parse_response(struct im_connection *ic, struct http_request *req)
{
	gboolean logging_in = !(ic->flags & OPT_LOGGED_IN);
	gboolean periodic;
	struct mastodon_data *md = ic->proto_data;
	json_value *ret;
	char path[64] = "", *s;

	if ((s = strchr(req->request, ' '))) {
		path[sizeof(path) - 1] = '\0';
		strncpy(path, s + 1, sizeof(path) - 1);
		if ((s = strchr(path, '?')) || (s = strchr(path, ' '))) {
			*s = '\0';
		}
	}

	/* Kinda nasty. :-( Trying to suppress error messages, but only
	   for periodic (i.e. mentions/timeline) queries. */
	periodic = strstr(path, "timeline") || strstr(path, "mentions");

	if (req->status_code == 401 && logging_in) {
		/* Twitter once had an outage where they were randomly
		   throwing 401s so we'll keep treating this one as fatal
		   only during login. */
		imcb_error(ic, "Authentication failure (%s)",
		           mastodon_parse_error(req));
		imc_logout(ic, FALSE);
		return NULL;
	} else if (req->status_code != 200) {
		// It didn't go well, output the error and return.
		if (!periodic || logging_in || ++md->http_fails >= 5) {
			mastodon_log(ic, "Error: Could not retrieve %s: %s",
			            path, mastodon_parse_error(req));
		}

		if (logging_in) {
			imc_logout(ic, TRUE);
		}
		return NULL;
	} else {
		md->http_fails = 0;
	}

	if ((ret = json_parse(req->reply_body, req->body_size)) == NULL) {
		imcb_error(ic, "Could not retrieve %s: %s",
		           path, "JSON parse error");
	}
	return ret;
}

static void mastodon_http_get_mutes_ids(struct http_request *req);
static void mastodon_http_get_noretweets_ids(struct http_request *req);

/**
 * Get the muted users ids.
 */
void mastodon_get_mutes_ids(struct im_connection *ic, gint64 next_cursor)
{
	char *args[2];

	args[0] = "cursor";
	args[1] = g_strdup_printf("%" G_GINT64_FORMAT, next_cursor);
	mastodon_http(ic, MASTODON_MUTES_IDS_URL, mastodon_http_get_mutes_ids, ic, 0, args, 2);

	g_free(args[1]);
}

/**
 * Get the ids for users from whom we should ignore retweets.
 */
void mastodon_get_noretweets_ids(struct im_connection *ic, gint64 next_cursor)
{
	char *args[2];

	args[0] = "cursor";
	args[1] = g_strdup_printf("%" G_GINT64_FORMAT, next_cursor);
	mastodon_http(ic, MASTODON_NORETWEETS_IDS_URL, mastodon_http_get_noretweets_ids, ic, 0, args, 2);

	g_free(args[1]);
}

/**
 * Fill a list of ids.
 */
static gboolean mastodon_xt_get_friends_id_list(json_value *node, struct mastodon_xml_list *txl)
{
	json_value *c;
	int i;

	// Set the list type.
	txl->type = TXL_ID;

	c = json_o_get(node, "ids");
	if (!c || c->type != json_array) {
		return FALSE;
	}

	for (i = 0; i < c->u.array.length; i++) {
		if (c->u.array.values[i]->type != json_integer) {
			continue;
		}

		txl->list = g_slist_prepend(txl->list,
		                            g_strdup_printf("%" PRIu64, c->u.array.values[i]->u.integer));
	}

	c = json_o_get(node, "next_cursor");
	if (c && c->type == json_integer) {
		txl->next_cursor = c->u.integer;
	} else {
		txl->next_cursor = -1;
	}

	return TRUE;
}

static void mastodon_get_users_lookup(struct im_connection *ic);

/**
 * Callback for getting the mutes ids.
 */
static void mastodon_http_get_mutes_ids(struct http_request *req)
{
	struct im_connection *ic = req->data;
	json_value *parsed;
	struct mastodon_xml_list *txl;
	struct mastodon_data *md;

	// Check if the connection is stil active
	if (!g_slist_find(mastodon_connections, ic)) {
		return;
	}

	md = ic->proto_data;

	if (req->status_code != 200) {
		/* Fail silently */
		return;
	}

	// Parse the data.
	if (!(parsed = mastodon_parse_response(ic, req))) {
		return;
	}

	txl = g_new0(struct mastodon_xml_list, 1);
	txl->list = md->mutes_ids;

	/* mute ids API response is similar enough to friends response
	   to reuse this method */
	mastodon_xt_get_friends_id_list(parsed, txl);
	json_value_free(parsed);

	md->mutes_ids = txl->list;
	if (txl->next_cursor) {
		/* Recurse while there are still more pages */
		mastodon_get_mutes_ids(ic, txl->next_cursor);
	}

	txl->list = NULL;
	txl_free(txl);
}

/**
 * Callback for getting the no-retweets ids.
 */
static void mastodon_http_get_noretweets_ids(struct http_request *req)
{
	struct im_connection *ic = req->data;
	json_value *parsed;
	struct mastodon_xml_list *txl;
	struct mastodon_data *md;

	// Check if the connection is stil active
	if (!g_slist_find(mastodon_connections, ic)) {
		return;
	}

	if (req->status_code != 200) {
		/* Fail silently */
		return;
	}

	md = ic->proto_data;

	// Parse the data.
	if (!(parsed = mastodon_parse_response(ic, req))) {
		return;
	}

	txl = g_new0(struct mastodon_xml_list, 1);
	txl->list = md->noretweets_ids;
	
	// Process the retweet ids
	txl->type = TXL_ID;
	if (parsed->type == json_array) {
		unsigned int i;
		for (i = 0; i < parsed->u.array.length; i++) {
			json_value *c = parsed->u.array.values[i];
			if (c->type != json_integer) {
				continue;
			}
			txl->list = g_slist_prepend(txl->list,
			                            g_strdup_printf("%"PRIu64, c->u.integer));
		}
	}

	json_value_free(parsed);
	md->noretweets_ids = txl->list;

	txl->list = NULL;
	txl_free(txl);
}

static gboolean mastodon_xt_get_users(json_value *node, struct mastodon_xml_list *txl);
static void mastodon_http_get_users_lookup(struct http_request *req);

static void mastodon_get_users_lookup(struct im_connection *ic)
{
	struct mastodon_data *md = ic->proto_data;
	char *args[2] = {
		"user_id",
		NULL,
	};
	GString *ids = g_string_new("");
	int i;

	/* We can request up to 100 users at a time. */
	for (i = 0; i < 100 && md->follow_ids; i++) {
		g_string_append_printf(ids, ",%s", (char *) md->follow_ids->data);
		g_free(md->follow_ids->data);
		md->follow_ids = g_slist_remove(md->follow_ids, md->follow_ids->data);
	}
	if (ids->len > 0) {
		args[1] = ids->str + 1;
		/* POST, because I think ids can be up to 1KB long. */
		mastodon_http(ic, MASTODON_USERS_LOOKUP_URL, mastodon_http_get_users_lookup, ic, 1, args, 2);
	} else {
		/* We have all users. Continue with login. (Get statuses.) */
		md->flags |= MASTODON_HAVE_FRIENDS;
	}
	g_string_free(ids, TRUE);
}

/**
 * Callback for getting (mastodon)friends...
 *
 * Be afraid, be very afraid! This function will potentially add hundreds of "friends". "Who has
 * hundreds of friends?" you wonder? You probably not, since you are reading the source of
 * BitlBee... Get a life and meet new people!
 */
static void mastodon_http_get_users_lookup(struct http_request *req)
{
	struct im_connection *ic = req->data;
	json_value *parsed;
	struct mastodon_xml_list *txl;
	GSList *l = NULL;
	struct mastodon_xml_user *user;

	// Check if the connection is still active.
	if (!g_slist_find(mastodon_connections, ic)) {
		return;
	}

	// Get the user list from the parsed xml feed.
	if (!(parsed = mastodon_parse_response(ic, req))) {
		return;
	}

	txl = g_new0(struct mastodon_xml_list, 1);
	txl->list = NULL;

	mastodon_xt_get_users(parsed, txl);
	json_value_free(parsed);

	// Add the users as buddies.
	for (l = txl->list; l; l = g_slist_next(l)) {
		user = l->data;
		mastodon_add_buddy(ic, user->screen_name, user->name);
	}

	// Free the structure.
	txl_free(txl);

	mastodon_get_users_lookup(ic);
}

struct mastodon_xml_user *mastodon_xt_get_user(const json_value *node)
{
	struct mastodon_xml_user *txu;
	json_value *jv;

	txu = g_new0(struct mastodon_xml_user, 1);
	txu->name = g_strdup(json_o_str(node, "acct"));
	txu->screen_name = g_strdup(json_o_str(node, "display_name"));

	jv = json_o_get(node, "id");
	txu->uid = jv->u.integer;

	return txu;
}

/**
 * Function to fill a mastodon_xml_list struct.
 * It sets:
 *  - all <user>s from the <users> element.
 */
static gboolean mastodon_xt_get_users(json_value *node, struct mastodon_xml_list *txl)
{
	struct mastodon_xml_user *txu;
	int i;

	// Set the type of the list.
	txl->type = TXL_USER;

	if (!node || node->type != json_array) {
		return FALSE;
	}

	// The root <users> node should hold the list of users <user>
	// Walk over the nodes children.
	for (i = 0; i < node->u.array.length; i++) {
		txu = mastodon_xt_get_user(node->u.array.values[i]);
		if (txu) {
			txl->list = g_slist_prepend(txl->list, txu);
		}
	}

	return TRUE;
}

#ifdef __GLIBC__
#define MASTODON_TIME_FORMAT "%a %b %d %H:%M:%S %z %Y"
#else
#define MASTODON_TIME_FORMAT "%a %b %d %H:%M:%S +0000 %Y"
#endif

static void expand_entities(char **text, const json_value *node, const json_value *extended_node);

/**
 * Function to fill a mastodon_xml_status struct.
 * It sets:
 *  - the status text and
 *  - the created_at timestamp and
 *  - the status id and
 *  - the user in a mastodon_xml_user struct.
 */
static struct mastodon_xml_status *mastodon_xt_get_status(const json_value *node)
{
	struct mastodon_xml_status *txs = {0};
	const json_value *rt = NULL;
	const json_value *text_value = NULL;
	const json_value *extended_node = NULL;

	if (node->type != json_object) {
		return FALSE;
	}
	txs = g_new0(struct mastodon_xml_status, 1);

	JSON_O_FOREACH(node, k, v) {
		if (strcmp("content", k) == 0 && v->type == json_string && text_value == NULL) {
			text_value = v;
		} else if (strcmp("reblog", k) == 0 && v->type == json_object) {
			rt = v;
		} else if (strcmp("created_at", k) == 0 && v->type == json_string) {
			struct tm parsed;

			/* Very sensitive to changes to the formatting of
			   this field. :-( Also assumes the timezone used
			   is UTC since C time handling functions suck. */
			if (strptime(v->u.string.ptr, MASTODON_TIME_FORMAT, &parsed) != NULL) {
				txs->created_at = mktime_utc(&parsed);
			}
		} else if (strcmp("account", k) == 0 && v->type == json_object) {
			txs->user = mastodon_xt_get_user(v);
		} else if (strcmp("id", k) == 0 && v->type == json_integer) {
			txs->rt_id = txs->id = v->u.integer;
		} else if (strcmp("in_reply_to_id", k) == 0 && v->type == json_integer) {
			txs->reply_to = v->u.integer;
		}
	}

	/* If it's a (truncated) retweet, get the original. Even if the API claims it
	   wasn't truncated because it may be lying. */
	if (rt) {
		struct mastodon_xml_status *rtxs = mastodon_xt_get_status(rt);
		if (rtxs) {
			txs->text = g_strdup_printf("RT @%s: %s", rtxs->user->screen_name, rtxs->text);
			txs->id = rtxs->id;
			txs_free(rtxs);
		}
	} else if (text_value && text_value->type == json_string) {
		txs->text = g_memdup(text_value->u.string.ptr, text_value->u.string.length + 1);
		strip_html(txs->text);
		expand_entities(&txs->text, node, extended_node);
	}

	if (txs->text && txs->user && txs->id) {
		return txs;
	}

	txs_free(txs);
	return NULL;
}

static void expand_entities(char **text, const json_value *node, const json_value *extended_node)
{
	json_value *entities, *extended_entities, *quoted;
	char *quote_url = NULL, *quote_text = NULL;

	if (!((entities = json_o_get(node, "entities")) && entities->type == json_object))
		return;
	if ((quoted = json_o_get(node, "quoted_status")) && quoted->type == json_object) {
		/* New "retweets with comments" feature. Grab the
		 * full message and try to insert it when we run into the
		 * Tweet entity. */
		struct mastodon_xml_status *txs = mastodon_xt_get_status(quoted);
		quote_text = g_strdup_printf("@%s: %s", txs->user->screen_name, txs->text);
		quote_url = g_strdup_printf("%s/status/%" G_GUINT64_FORMAT, txs->user->screen_name, txs->id);
		txs_free(txs);
	} else {
		quoted = NULL;
	}

	if (extended_node) {
		extended_entities = json_o_get(extended_node, "entities");
		if (extended_entities && extended_entities->type == json_object) {
			entities = extended_entities;
		}
	}

	JSON_O_FOREACH(entities, k, v) {
		int i;

		if (v->type != json_array) {
			continue;
		}
		if (strcmp(k, "urls") != 0 && strcmp(k, "media") != 0) {
			continue;
		}

		for (i = 0; i < v->u.array.length; i++) {
			const char *format = "%s%s <%s>%s";

			if (v->u.array.values[i]->type != json_object) {
				continue;
			}

			const char *kort = json_o_str(v->u.array.values[i], "url");
			const char *disp = json_o_str(v->u.array.values[i], "display_url");
			const char *full = json_o_str(v->u.array.values[i], "expanded_url");
			char *pos, *new;

			/* Skip if a required field is missing, if the t.co URL is not in fact 
			   in the Tweet at all, or if the full-ish one *is* in it already
			   (dupes appear, especially in streaming API). */
			if (!kort || !disp || !(pos = strstr(*text, kort)) || strstr(*text, disp)) {
				continue;
			}
			if (quote_url && strstr(full, quote_url)) {
				format = "%s<%s> [%s]%s";
				disp = quote_text;
			}

			*pos = '\0';
			new = g_strdup_printf(format, *text, kort,
			                      disp, pos + strlen(kort));

			g_free(*text);
			*text = new;
		}
	}
	g_free(quote_text);
	g_free(quote_url);
}

/**
 * Function to fill a mastodon_xml_list struct.
 * It sets:
 *  - all <status>es within the <status> element and
 *  - the next_cursor.
 */
static gboolean mastodon_xt_get_status_list(struct im_connection *ic, const json_value *node,
                                           struct mastodon_xml_list *txl)
{
	struct mastodon_xml_status *txs;
	int i;

	// Set the type of the list.
	txl->type = TXL_STATUS;

	if (node->type != json_array) {
		return FALSE;
	}

	// The root <statuses> node should hold the list of statuses <status>
	// Walk over the nodes children.
	for (i = 0; i < node->u.array.length; i++) {
		txs = mastodon_xt_get_status(node->u.array.values[i]);
		if (!txs) {
			continue;
		}

		txl->list = g_slist_prepend(txl->list, txs);
	}
	txl->list = g_slist_reverse(txl->list);
	return TRUE;
}

/* Will log messages either way. Need to keep track of IDs for stream deduping.
   Plus, show_ids is on by default and I don't see why anyone would disable it. */
static char *mastodon_msg_add_id(struct im_connection *ic,
                                struct mastodon_xml_status *txs, const char *prefix)
{
	struct mastodon_data *md = ic->proto_data;
	int reply_to = -1;
	bee_user_t *bu;

	if (txs->reply_to) {
		int i;
		for (i = 0; i < MASTODON_LOG_LENGTH; i++) {
			if (md->log[i].id == txs->reply_to) {
				reply_to = i;
				break;
			}
		}
	}

	if (txs->user && txs->user->screen_name &&
	    (bu = bee_user_by_handle(ic->bee, ic, txs->user->screen_name))) {
		struct mastodon_user_data *tud = bu->data;

		if (txs->id > tud->last_id) {
			tud->last_id = txs->id;
			tud->last_time = txs->created_at;
		}
	}

	md->log_id = (md->log_id + 1) % MASTODON_LOG_LENGTH;
	md->log[md->log_id].id = txs->id;
	md->log[md->log_id].bu = bee_user_by_handle(ic->bee, ic, txs->user->screen_name);

	/* This is all getting hairy. :-( If we RT'ed something ourselves,
	   remember OUR id instead so undo will work. In other cases, the
	   original tweet's id should be remembered for deduplicating. */
	if (g_strcasecmp(txs->user->screen_name, md->user) == 0) {
		md->log[md->log_id].id = txs->rt_id;
		/* More useful than NULL. */
		md->log[md->log_id].bu = &mastodon_log_local_user;
	}

	if (set_getbool(&ic->acc->set, "show_ids")) {
		if (reply_to != -1) {
			return g_strdup_printf("\002[\002%02x->%02x\002]\002 %s%s",
			                       md->log_id, reply_to, prefix, txs->text);
		} else {
			return g_strdup_printf("\002[\002%02x\002]\002 %s%s",
			                       md->log_id, prefix, txs->text);
		}
	} else {
		if (*prefix) {
			return g_strconcat(prefix, txs->text, NULL);
		} else {
			return NULL;
		}
	}
}

/**
 * Function that is called to see the filter statuses in groupchat windows.
 */
static void mastodon_status_show_filter(struct im_connection *ic, struct mastodon_xml_status *status)
{
	struct mastodon_data *md = ic->proto_data;
	char *msg = mastodon_msg_add_id(ic, status, "");
	struct mastodon_filter *tf;
	GSList *f;
	GSList *l;

	for (f = md->filters; f; f = g_slist_next(f)) {
		tf = f->data;

		switch (tf->type) {
		case MASTODON_FILTER_TYPE_FOLLOW:
			if (status->user->uid != tf->uid) {
				continue;
			}
			break;

		case MASTODON_FILTER_TYPE_TRACK:
			if (strcasestr(status->text, tf->text) == NULL) {
				continue;
			}
			break;

		default:
			continue;
		}

		for (l = tf->groupchats; l; l = g_slist_next(l)) {
			imcb_chat_msg(l->data, status->user->screen_name,
			              msg ? msg : status->text, 0, 0);
		}
	}

	g_free(msg);
}

/**
 * Function that is called to see the statuses in a groupchat window.
 */
static void mastodon_status_show_chat(struct im_connection *ic, struct mastodon_xml_status *status)
{
	struct mastodon_data *md = ic->proto_data;
	struct groupchat *gc;
	gboolean me = g_strcasecmp(md->user, status->user->screen_name) == 0;
	char *msg;

	// Create a new groupchat if it does not exsist.
	gc = mastodon_groupchat_init(ic);

	if (!me) {
		/* MUST be done before mastodon_msg_add_id() to avoid #872. */
		mastodon_add_buddy(ic, status->user->screen_name, status->user->name);
	}
	msg = mastodon_msg_add_id(ic, status, "");

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
static void mastodon_status_show_msg(struct im_connection *ic, struct mastodon_xml_status *status)
{
	struct mastodon_data *md = ic->proto_data;
	char from[MAX_STRING] = "";
	char *prefix = NULL, *text = NULL;
	gboolean me = g_strcasecmp(md->user, status->user->screen_name) == 0;

	if (md->flags & MASTODON_MODE_ONE) {
		g_snprintf(from, sizeof(from) - 1, "%s_%s", md->prefix, ic->acc->user);
		from[MAX_STRING - 1] = '\0';
	}

	if (md->flags & MASTODON_MODE_ONE) {
		prefix = g_strdup_printf("\002<\002%s\002>\002 ",
		                         status->user->screen_name);
	} else if (!me) {
		mastodon_add_buddy(ic, status->user->screen_name, status->user->name);
	} else {
		prefix = g_strdup("You: ");
	}

	text = mastodon_msg_add_id(ic, status, prefix ? prefix : "");

	imcb_buddy_msg(ic,
	               *from ? from : status->user->screen_name,
	               text ? text : status->text, 0, status->created_at);

	g_free(text);
	g_free(prefix);
}

static void mastodon_status_show(struct im_connection *ic, struct mastodon_xml_status *status)
{
	struct mastodon_data *md = ic->proto_data;
	char *last_id_str;
	char *uid_str;

	if (status->user == NULL || status->text == NULL) {
		return;
	}
	
	/* Check this is not a tweet that should be muted */
	uid_str = g_strdup_printf("%" G_GUINT64_FORMAT, status->user->uid);

	if (g_slist_find_custom(md->mutes_ids, uid_str, (GCompareFunc)strcmp)) {
		g_free(uid_str);
		return;
	}
	if (status->id != status->rt_id && g_slist_find_custom(md->noretweets_ids, uid_str, (GCompareFunc)strcmp)) {
		g_free(uid_str);
		return;
	}

	/* Grrrr. Would like to do this during parsing, but can't access
	   settings from there. */
	if (set_getbool(&ic->acc->set, "strip_newlines")) {
		strip_newlines(status->text);
	}

	if (status->from_filter) {
		mastodon_status_show_filter(ic, status);
	} else if (md->flags & MASTODON_MODE_CHAT) {
		mastodon_status_show_chat(ic, status);
	} else {
		mastodon_status_show_msg(ic, status);
	}

	// Update the timeline_id to hold the highest id, so that by the next request
	// we won't pick up the updates already in the list.
	md->timeline_id = MAX(md->timeline_id, status->rt_id);

	last_id_str = g_strdup_printf("%" G_GUINT64_FORMAT, md->timeline_id);
	set_setstr(&ic->acc->set, "_last_tweet", last_id_str);
	g_free(last_id_str);
	g_free(uid_str);
}

/**
 * Add exactly one status to the timeline.
 */
static void mastodon_stream_handle_update(struct im_connection *ic, json_value *parsed, gboolean from_filter)
{
	struct mastodon_xml_status *txs = mastodon_xt_get_status(parsed);
	if (txs) {
		mastodon_status_show(ic, txs);
		txs_free(txs);
	}
}

static void mastodon_stream_handle_event(struct im_connection *ic, mastodon_evt_flags_t evt_type, json_value *parsed, gboolean from_filter)
{
	if (evt_type == MASTODON_EVT_UPDATE) {
		mastodon_stream_handle_update(ic, parsed, from_filter);
	} else {
		mastodon_log(ic, "Ignoring event type %d", evt_type);
	}
}

static void mastodon_http_stream(struct http_request *req)
{
	struct im_connection *ic = req->data;
	struct mastodon_data *md = ic->proto_data;
	int len = 0;
	char *nl;
	gboolean from_filter;

	if (!g_slist_find(mastodon_connections, ic)) {
		return;
	}

	if ((req->flags & HTTPC_EOF) || !req->reply_body) {
		if (req == md->stream) {
			md->stream = NULL;
		} else if (req == md->filter_stream) {
			md->filter_stream = NULL;
		}

		imcb_error(ic, "Stream closed (%s)", req->status_string);
		if (req->status_code == 401) {
			imcb_error(ic, "Check your system clock.");
		}
		imc_logout(ic, TRUE);
		return;
	}

	if (req == md->stream) {
		ic->flags |= OPT_PONGED;
	}

	/*
https://github.com/tootsuite/documentation/blob/master/Using-the-API/Streaming-API.md
https://developer.mozilla.org/en-US/docs/Web/API/Server-sent_events/Using_server-sent_events#Event_stream_format
	*/

	if (req->reply_body[0] == ':' &&
	    (nl = strchr(req->reply_body, '\n'))) {
		// found a comment such as the heartbeat ":thump\n"
		len = nl - req->reply_body + 1;
		goto end;
	} else if (!(nl = strstr(req->reply_body, "\n\n"))) {
		// wait until we have a complete event
		return;
	}

	// include the two newlines at the end
	len = nl - req->reply_body + 2;
	
	if (len > 0) {
		char *p;
		mastodon_evt_flags_t evt_type = MASTODON_EVT_UNKNOWN;

		// assuming space after colon
		if (strncmp(req->reply_body, "event: ", 7) == 0) {
			p = req->reply_body + 7;
			if (strncmp(p, "update\n", 7) == 0) {
				evt_type = MASTODON_EVT_UPDATE;
				p += 7;
			} else if (strncmp(p, "notification\n", 13) == 0) {
				evt_type = MASTODON_EVT_NOTIFICATION;
				p += 13;
			} else if (strncmp(p, "delete\n", 7) == 0) {
				evt_type = MASTODON_EVT_DELETE;
				p += 7;
			}
		}

		if (evt_type != MASTODON_EVT_UNKNOWN) {

			GString *data = g_string_new("");
			char* q;

			while (strncmp(p, "data: ", 6) == 0) {
				p += 6;
				q = strchr(p, '\n');
				p[q-p] = '\0';
				g_string_append(data, p);
				p = q + 1;
			}

			json_value *parsed;
			if ((parsed = json_parse(data->str, data->len))) {
				from_filter = (req == md->filter_stream);
				mastodon_stream_handle_event(ic, evt_type, parsed, from_filter);
			}

			json_value_free(parsed);
			g_string_free(data, TRUE);
		}
	}
	
end:
	http_flush_bytes(req, len);

	/* One notification might bring multiple events! */
	if (req->body_size > 0) {
		mastodon_http_stream(req);
	}
}

gboolean mastodon_open_stream(struct im_connection *ic)
{
	struct mastodon_data *md = ic->proto_data;

	if ((md->stream = mastodon_http(ic, MASTODON_USER_STREAMING_URL,
	                               mastodon_http_stream, ic, 0, NULL, 0))) {
		/* This flag must be enabled or we'll get no data until EOF
		   (which err, kind of, defeats the purpose of a streaming API). */
		md->stream->flags |= HTTPC_STREAMING;
		return TRUE;
	}

	return FALSE;
}

static gboolean mastodon_filter_stream(struct im_connection *ic)
{
	struct mastodon_data *md = ic->proto_data;
	char *args[4] = { "follow", NULL, "track", NULL };
	GString *followstr = g_string_new("");
	GString *trackstr = g_string_new("");
	gboolean ret = FALSE;
	struct mastodon_filter *tf;
	GSList *l;

	for (l = md->filters; l; l = g_slist_next(l)) {
		tf = l->data;

		switch (tf->type) {
		case MASTODON_FILTER_TYPE_FOLLOW:
			if (followstr->len > 0) {
				g_string_append_c(followstr, ',');
			}

			g_string_append_printf(followstr, "%" G_GUINT64_FORMAT,
			                       tf->uid);
			break;

		case MASTODON_FILTER_TYPE_TRACK:
			if (trackstr->len > 0) {
				g_string_append_c(trackstr, ',');
			}

			g_string_append(trackstr, tf->text);
			break;

		default:
			continue;
		}
	}

	args[1] = followstr->str;
	args[3] = trackstr->str;

	if (md->filter_stream) {
		http_close(md->filter_stream);
	}

	if ((md->filter_stream = mastodon_http(ic, MASTODON_FILTER_STREAM_URL,
	                                      mastodon_http_stream, ic, 0,
	                                      args, 4))) {
		/* This flag must be enabled or we'll get no data until EOF
		   (which err, kind of, defeats the purpose of a streaming API). */
		md->filter_stream->flags |= HTTPC_STREAMING;
		ret = TRUE;
	}

	g_string_free(followstr, TRUE);
	g_string_free(trackstr, TRUE);

	return ret;
}

static void mastodon_filter_users_post(struct http_request *req)
{
	struct im_connection *ic = req->data;
	struct mastodon_data *md;
	struct mastodon_filter *tf;
	GList *users = NULL;
	json_value *parsed;
	json_value *id;
	const char *name;
	GString *fstr;
	GSList *l;
	GList *u;
	int i;

	// Check if the connection is still active.
	if (!g_slist_find(mastodon_connections, ic)) {
		return;
	}

	md = ic->proto_data;

	if (!(parsed = mastodon_parse_response(ic, req))) {
		return;
	}

	for (l = md->filters; l; l = g_slist_next(l)) {
		tf = l->data;

		if (tf->type == MASTODON_FILTER_TYPE_FOLLOW) {
			users = g_list_prepend(users, tf);
		}
	}

	if (parsed->type != json_array) {
		goto finish;
	}

	for (i = 0; i < parsed->u.array.length; i++) {
		id = json_o_get(parsed->u.array.values[i], "id");
		name = json_o_str(parsed->u.array.values[i], "screen_name");

		if (!name || !id || id->type != json_integer) {
			continue;
		}

		for (u = users; u; u = g_list_next(u)) {
			tf = u->data;

			if (g_strcasecmp(tf->text, name) == 0) {
				tf->uid = id->u.integer;
				users = g_list_delete_link(users, u);
				break;
			}
		}
	}

finish:
	json_value_free(parsed);
	mastodon_filter_stream(ic);

	if (!users) {
		return;
	}

	fstr = g_string_new("");

	for (u = users; u; u = g_list_next(u)) {
		if (fstr->len > 0) {
			g_string_append(fstr, ", ");
		}

		g_string_append(fstr, tf->text);
	}

	imcb_error(ic, "Failed UID acquisitions: %s", fstr->str);

	g_string_free(fstr, TRUE);
	g_list_free(users);
}

gboolean mastodon_open_filter_stream(struct im_connection *ic)
{
	struct mastodon_data *md = ic->proto_data;
	char *args[2] = { "screen_name", NULL };
	GString *ustr = g_string_new("");
	struct mastodon_filter *tf;
	struct http_request *req;
	GSList *l;

	for (l = md->filters; l; l = g_slist_next(l)) {
		tf = l->data;

		if (tf->type != MASTODON_FILTER_TYPE_FOLLOW || tf->uid != 0) {
			continue;
		}

		if (ustr->len > 0) {
			g_string_append_c(ustr, ',');
		}

		g_string_append(ustr, tf->text);
	}

	if (ustr->len == 0) {
		g_string_free(ustr, TRUE);
		return mastodon_filter_stream(ic);
	}

	args[1] = ustr->str;
	req = mastodon_http(ic, MASTODON_USERS_LOOKUP_URL,
	                   mastodon_filter_users_post,
	                   ic, 0, args, 2);

	g_string_free(ustr, TRUE);
	return req != NULL;
}

static void mastodon_get_home_timeline(struct im_connection *ic, gint64 next_cursor);

/**
 * Get the timeline with optionally mentions
 */
gboolean mastodon_get_timeline(struct im_connection *ic, gint64 next_cursor)
{
	struct mastodon_data *md = ic->proto_data;

	imcb_log(ic, "Getting home timeline");

	if (md->flags & MASTODON_DOING_TIMELINE) {
		if (++md->http_fails >= 5) {
			imcb_error(ic, "Fetch timeout (%d)", md->flags);
			imc_logout(ic, TRUE);
			return FALSE;
		}
	}

	md->flags |= MASTODON_DOING_TIMELINE;

	mastodon_get_home_timeline(ic, next_cursor);

	return TRUE;
}

/**
 * Call this one after receiving timeline/mentions. Show to user once we have
 * both.
 */
void mastodon_flush_timeline(struct im_connection *ic)
{
	struct mastodon_data *md = ic->proto_data;
	struct mastodon_xml_list *home_timeline = md->home_timeline_obj;
	struct mastodon_xml_list *mentions = md->mentions_obj;
	guint64 last_id = 0;
	GSList *output = NULL;
	GSList *l;

	imcb_connected(ic);

	if (!(md->flags & MASTODON_GOT_TIMELINE)) {
		return;
	}

	if (home_timeline && home_timeline->list) {
		for (l = home_timeline->list; l; l = g_slist_next(l)) {
			output = g_slist_insert_sorted(output, l->data, mastodon_compare_elements);
		}
	}

	// See if the user wants to see the messages in a groupchat window or as private messages.
	while (output) {
		struct mastodon_xml_status *txs = output->data;
		if (txs->id != last_id) {
			mastodon_status_show(ic, txs);
		}
		last_id = txs->id;
		output = g_slist_remove(output, txs);
	}

	txl_free(home_timeline);
	txl_free(mentions);

	md->flags &= ~(MASTODON_DOING_TIMELINE | MASTODON_GOT_TIMELINE | MASTODON_GOT_MENTIONS);
	md->home_timeline_obj = md->mentions_obj = NULL;
}

/**
 * Callback for getting the home timeline.
 */
static void mastodon_http_get_home_timeline(struct http_request *req)
{
	struct im_connection *ic = req->data;
	struct mastodon_data *md;
	json_value *parsed;
	struct mastodon_xml_list *txl;

	// Check if the connection is still active.
	if (!g_slist_find(mastodon_connections, ic)) {
		return;
	}

	md = ic->proto_data;

	// The root <statuses> node should hold the list of statuses <status>
	if (!(parsed = mastodon_parse_response(ic, req))) {
		goto end;
	}

	txl = g_new0(struct mastodon_xml_list, 1);
	txl->list = NULL;

	mastodon_xt_get_status_list(ic, parsed, txl);
	json_value_free(parsed);

	md->home_timeline_obj = txl;

end:
	if (!g_slist_find(mastodon_connections, ic)) {
		return;
	}

	md->flags |= MASTODON_GOT_TIMELINE;

	mastodon_flush_timeline(ic);
}

/**
 * Get the timeline.
 */
static void mastodon_get_home_timeline(struct im_connection *ic, gint64 next_cursor)
{
	struct mastodon_data *md = ic->proto_data;

	txl_free(md->home_timeline_obj);
	md->home_timeline_obj = NULL;
	md->flags &= ~MASTODON_GOT_TIMELINE;

	if (mastodon_http(ic, MASTODON_HOME_TIMELINE_URL, mastodon_http_get_home_timeline, ic, 0, NULL, 0) == NULL) {
		if (++md->http_fails >= 5) {
			imcb_error(ic, "Could not retrieve %s: %s",
			           MASTODON_HOME_TIMELINE_URL, "connection failed");
		}
		md->flags |= MASTODON_GOT_TIMELINE;
		mastodon_flush_timeline(ic);
	}
}

/**
 * Callback to use after sending a POST request to mastodon.
 * (Generic, used for a few kinds of queries.)
 */
static void mastodon_http_post(struct http_request *req)
{
	struct im_connection *ic = req->data;
	struct mastodon_data *md;
	json_value *parsed, *id;

	// Check if the connection is still active.
	if (!g_slist_find(mastodon_connections, ic)) {
		return;
	}

	md = ic->proto_data;
	md->last_status_id = 0;

	if (!(parsed = mastodon_parse_response(ic, req))) {
		return;
	}

	if ((id = json_o_get(parsed, "id")) && id->type == json_integer) {
		md->last_status_id = id->u.integer;
	}

	json_value_free(parsed);

	if (req->flags & MASTODON_HTTP_USER_ACK) {
		mastodon_log(ic, "Command processed successfully");
	}
}

/**
 * Function to POST a new status to mastodon.
 */
void mastodon_post_status(struct im_connection *ic, char *msg, guint64 in_reply_to)
{
	char *args[4] = {
		"status", msg,
		"in_reply_to_status_id",
		g_strdup_printf("%" G_GUINT64_FORMAT, in_reply_to)
	};

	mastodon_http(ic, MASTODON_STATUS_UPDATE_URL, mastodon_http_post, ic, 1,
	             args, in_reply_to ? 4 : 2);
	g_free(args[3]);
}


/**
 * Function to POST a new message to mastodon.
 */
void mastodon_direct_messages_new(struct im_connection *ic, char *who, char *msg)
{
	char *args[4];

	args[0] = "screen_name";
	args[1] = who;
	args[2] = "text";
	args[3] = msg;
	// Use the same callback as for mastodon_post_status, since it does basically the same.
	mastodon_http(ic, MASTODON_DIRECT_MESSAGES_NEW_URL, mastodon_http_post, ic, 1, args, 4);
}

void mastodon_friendships_create_destroy(struct im_connection *ic, char *who, int create)
{
	char *args[2];

	args[0] = "screen_name";
	args[1] = who;
	mastodon_http(ic, create ? MASTODON_FRIENDSHIPS_CREATE_URL : MASTODON_FRIENDSHIPS_DESTROY_URL,
	             mastodon_http_post, ic, 1, args, 2);
}

/**
 * Mute or unmute a user
 */
void mastodon_mute_create_destroy(struct im_connection *ic, char *who, int create)
{
	char *args[2];

	args[0] = "screen_name";
	args[1] = who;
	mastodon_http(ic, create ? MASTODON_MUTES_CREATE_URL : MASTODON_MUTES_DESTROY_URL,
		     mastodon_http_post, ic, 1, args, 2);
}

void mastodon_status_destroy(struct im_connection *ic, guint64 id)
{
	char *url;

	url = g_strdup_printf("%s%" G_GUINT64_FORMAT "%s",
	                      MASTODON_STATUS_DESTROY_URL, id, ".json");
	mastodon_http_f(ic, url, mastodon_http_post, ic, 1, NULL, 0,
	               MASTODON_HTTP_USER_ACK);
	g_free(url);
}

void mastodon_status_retweet(struct im_connection *ic, guint64 id)
{
	char *url;

	url = g_strdup_printf("%s%" G_GUINT64_FORMAT "%s",
	                      MASTODON_STATUS_RETWEET_URL, id, ".json");
	mastodon_http_f(ic, url, mastodon_http_post, ic, 1, NULL, 0,
	               MASTODON_HTTP_USER_ACK);
	g_free(url);
}

/**
 * Report a user for sending spam.
 */
void mastodon_report_spam(struct im_connection *ic, char *screen_name)
{
	char *args[2] = {
		"screen_name",
		NULL,
	};

	args[1] = screen_name;
	mastodon_http_f(ic, MASTODON_REPORT_SPAM_URL, mastodon_http_post,
	               ic, 1, args, 2, MASTODON_HTTP_USER_ACK);
}

/**
 * Favourite a tweet.
 */
void mastodon_favourite_tweet(struct im_connection *ic, guint64 id)
{
	char *args[2] = {
		"id",
		NULL,
	};

	args[1] = g_strdup_printf("%" G_GUINT64_FORMAT, id);
	mastodon_http_f(ic, MASTODON_FAVORITE_CREATE_URL, mastodon_http_post,
	               ic, 1, args, 2, MASTODON_HTTP_USER_ACK);
	g_free(args[1]);
}

static void mastodon_http_status_show_url(struct http_request *req)
{
	struct im_connection *ic = req->data;
	json_value *parsed, *id;
	const char *name;

	// Check if the connection is still active.
	if (!g_slist_find(mastodon_connections, ic)) {
		return;
	}

	if (!(parsed = mastodon_parse_response(ic, req))) {
		return;
	}

	/* for the parson branch:
	name = json_object_dotget_string(json_object(parsed), "user.screen_name");
	id = json_object_get_integer(json_object(parsed), "id");
	*/

	name = json_o_str(json_o_get(parsed, "user"), "screen_name");
	id = json_o_get(parsed, "id");

	if (name && id && id->type == json_integer) {
		mastodon_log(ic, "https://mastodon.com/%s/status/%" G_GUINT64_FORMAT, name, id->u.integer);
	} else {
		mastodon_log(ic, "Error: could not fetch tweet url.");
	}

	json_value_free(parsed);
}

void mastodon_status_show_url(struct im_connection *ic, guint64 id)
{
	char *url = g_strdup_printf("%s%" G_GUINT64_FORMAT "%s", MASTODON_STATUS_SHOW_URL, id, ".json");
	mastodon_http(ic, url, mastodon_http_status_show_url, ic, 0, NULL, 0);
	g_free(url);
}

/**
 * Callback for getting followers.
 */
static void mastodon_http_following(struct http_request *req)
{
	struct im_connection *ic = req->data;
	struct mastodon_data *md = ic->proto_data;

	json_value *parsed;
	guint64 max_id = 0;
	
	// Check if the connection is still active.
	if (!g_slist_find(mastodon_connections, ic)) {
		return;
	}

	// Parse the data.
	if (!(parsed = mastodon_parse_response(ic, req))) {
		return;
	}

	if (parsed->type != json_array) {
		goto finish;
	}

	// unlike Twitter, we don't have to resolve ids: just add buddies directly
	for (int i = 0; i < parsed->u.array.length; i++) {
		guint64 id = 0;
		char *acct = NULL;
		char *display_name = NULL;

		JSON_O_FOREACH(parsed->u.array.values[i], k, v) {
			if (strcmp("id", k) == 0 && v->type == json_integer) {
				id = v->u.integer;
			} else if (strcmp("acct", k) == 0 && v->type == json_string) {
				acct = g_memdup(v->u.string.ptr, v->u.string.length + 1);
			} else if (strcmp("display_name", k) == 0 && v->type == json_string) {
				display_name = g_memdup(v->u.string.ptr, v->u.string.length + 1);
			}
		}
		
		if (id != 0 && acct != NULL && display_name != NULL) {
			mastodon_add_buddy(ic, acct, display_name);
			if (id > max_id) {
				max_id = id;
			}
		} else {
			g_free(acct);
			g_free(display_name);
		}
	}
	
finish:
	json_value_free(parsed);
	
	// try to fetch more if we got at least one id
	
	if (max_id) {
		imcb_log(ic, "Found some buddies and won't ask for more");
		// mastodon_following(ic, max_id+1);
	}

	md->flags |= MASTODON_HAVE_FRIENDS;
}

/**
 * Get the followers of an account. Default to the current id.
 */
void mastodon_following(struct im_connection *ic, gint64 since_id)
{
	gint64 id = set_getint(&ic->acc->set, "account_id");

	imcb_log(ic, "Finding followers for id %" G_GINT64_FORMAT
		 ", since_id= %" G_GINT64_FORMAT, id, since_id);
	
	if (!id) {
		return;
	}

	// insert id into the URL
	char *url = g_strdup_printf(MASTODON_FOLLOWING_URL, id);
	
	char *args[4];
	args[0] = "since_id";
	args[1] = g_strdup_printf("%" G_GINT64_FORMAT, since_id);
	args[2] = "limit";
	args[3] = "80";
	
	mastodon_http(ic, url, mastodon_http_following, ic, 0, args, 4);

	g_free(url);
	g_free(args[1]);
}

/**
 * Callback for getting your own account.
 */
static void mastodon_http_verify_credentials(struct http_request *req)
{
	struct im_connection *ic = req->data;
	json_value *parsed;

	// Check if the connection is still active.
	if (!g_slist_find(mastodon_connections, ic)) {
		return;
	}

	if ((parsed = mastodon_parse_response(ic, req))) {

		set_setint(&ic->acc->set, "account_id", json_o_get(parsed, "id")->u.integer);

		json_value_free(parsed);

		if (req->flags & MASTODON_HTTP_USER_ACK) {
			mastodon_log(ic, "Verified credentials successfully");
		}
	}
}

/**
 * Get the account of the current user.
 */
void mastodon_verify_credentials(struct im_connection *ic)
{
	imcb_log(ic, "Verifying credentials");
	mastodon_http(ic, MASTODON_VERIFY_CREDENTIALS_URL, mastodon_http_verify_credentials, ic, 0, NULL, 0);
}

/**
 * Callback for registering a new application.
 */
static void mastodon_http_register_app(struct http_request *req)
{
	struct im_connection *ic = req->data;
	json_value *parsed;

	// Check if the connection is still active.
	if (!g_slist_find(mastodon_connections, ic)) {
		return;
	}

	mastodon_log(ic, "Parsing application registration response");
	if ((parsed = mastodon_parse_response(ic, req))) {

		set_setint(&ic->acc->set, "app_id", json_o_get(parsed, "id")->u.integer);
		set_setstr(&ic->acc->set, "consumer_key", json_o_strdup(parsed, "client_id"));
		set_setstr(&ic->acc->set, "consumer_secret", json_o_strdup(parsed, "client_secret"));

		json_value_free(parsed);

		if (req->flags & MASTODON_HTTP_USER_ACK) {
			mastodon_log(ic, "Application registered successfully");
		}
	}
}

/**
 * Function to register a new application (Bitlbee) for the server.
 */
void mastodon_register_app(struct im_connection *ic)
{
	char *args[8] = {
		"client_name", "bitblee",
		"redirect_uris", "urn:ietf:wg:oauth:2.0:oob",
		"scopes", "read write follow",
		"website", "https://www.bitlbee.org/"
	};

	mastodon_http(ic, MASTODON_REGISTER_APP_URL, mastodon_http_register_app, ic, 1,
	             args, 8);
}
