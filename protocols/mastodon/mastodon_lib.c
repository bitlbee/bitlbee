/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Simple module to facilitate Mastodon functionality.                      *
*                                                                           *
*  Copyright 2009-2010 Geert Mulders <g.c.w.m.mulders@gmail.com>            *
*  Copyright 2010-2013 Wilmer van der Gaast <wilmer@gaast.net>              *
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
#include "oauth2.h"
#include "json_util.h"
#include <ctype.h>
#include <errno.h>

typedef enum {
	ML_STATUS,
	ML_NOTIFICATION,
	ML_USER,
	ML_ID,
} mastodon_list_type_t;

struct mastodon_list {
	mastodon_list_type_t type;
	gint64 next_cursor;
	GSList *list;
};

struct mastodon_account {
	guint64 id;
	char *display_name;
	char *acct;
};

struct mastodon_status {
	time_t created_at;
	char *text;
	char *url;
	struct mastodon_account *account;
	guint64 id, rt_id; /* Usually equal, with RTs id == *original* id */
	guint64 reply_to;
	gboolean from_filter;
};

typedef enum {
	MN_MENTION = 1,
	MN_REBLOG,
	MN_FAVOURITE,
	MN_FOLLOW,
} mastodon_notification_type_t;

struct mastodon_notification {
	guint64 id;
	mastodon_notification_type_t type;
	time_t created_at;
	struct mastodon_account *account;
	struct mastodon_status *status;
	gboolean from_filter;
};

struct mastodon_report {
	struct im_connection *ic;
	guint64 account_id;
	guint64 status_id;
	char *comment;
};


/**
 * Frees a mastodon_account struct.
 */
static void ma_free(struct mastodon_account *ma)
{
	if (ma == NULL) {
		return;
	}

	g_free(ma->display_name);
	g_free(ma->acct);
	g_free(ma);
}

/**
 * Frees a mastodon_status struct.
 */
static void ms_free(struct mastodon_status *ms)
{
	if (ms == NULL) {
		return;
	}

	g_free(ms->text);
	g_free(ms->url);
	ma_free(ms->account);
	g_free(ms);
}

/**
 * Frees a mastodon_notification struct.
 */
static void mn_free(struct mastodon_notification *mn)
{
	if (mn == NULL) {
		return;
	}

	ma_free(mn->account);
	ms_free(mn->status);
	g_free(mn);
}

/**
 * Free a mastodon_list struct.
 * type is the type of list the struct holds.
 */
static void ml_free(struct mastodon_list *ml)
{
	GSList *l;

	if (ml == NULL) {
		return;
	}

	for (l = ml->list; l; l = g_slist_next(l)) {
		if (ml->type == ML_STATUS) {
			ms_free((struct mastodon_status *) l->data);
		} else if (ml->type == ML_NOTIFICATION) {
			mn_free((struct mastodon_notification *) l->data);
		} else if (ml->type == ML_ID) {
			g_free(l->data);
		} else if (ml->type == ML_USER) {
			ma_free(l->data);
		}
	}

	g_slist_free(ml->list);
	g_free(ml);
}

/**
 * Frees a mastodon_report struct.
 */
static void mr_free(struct mastodon_report *mr)
{
	if (mr == NULL) {
		return;
	}

	g_free(mr->comment);
	g_free(mr);
}

/**
 * Compare status elements
 */
static gint mastodon_compare_elements(gconstpointer a, gconstpointer b)
{
	struct mastodon_status *a_status = (struct mastodon_status *) a;
	struct mastodon_status *b_status = (struct mastodon_status *) b;

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
static void mastodon_add_buddy(struct im_connection *ic, gint64 id, char *name, const char *fullname)
{
	struct mastodon_data *md = ic->proto_data;

	// Check if the buddy is already in the buddy list.
	if (!bee_user_by_handle(ic->bee, ic, name)) {
		// The buddy is not in the list, add the buddy and set the status to logged in.
		imcb_add_buddy(ic, name, NULL);
		imcb_rename_buddy(ic, name, fullname);

		bee_user_t *bu = bee_user_by_handle(ic->bee, ic, name);
		struct mastodon_user_data *mud = (struct mastodon_user_data*) bu->data;
		mud->account_id = id;

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
		err = json_o_get(root, "error");
		if (err && err->type == json_string && err->u.string.length) {
			ret = g_strdup_printf("%s (%s)", req->status_string, err->u.string.ptr);
		}
		json_value_free(root);
	}

	return ret ? ret : req->status_string;
}

/* WATCH OUT: This function might or might not destroy your connection.
   Sub-optimal indeed, but just be careful when this returns NULL! */
static json_value *mastodon_parse_response(struct im_connection *ic, struct http_request *req)
{
	char path[64] = "", *s;

	if ((s = strchr(req->request, ' '))) {
		path[sizeof(path) - 1] = '\0';
		strncpy(path, s + 1, sizeof(path) - 1);
		if ((s = strchr(path, '?')) || (s = strchr(path, ' '))) {
			*s = '\0';
		}
	}

	if (req->status_code != 200) {
		mastodon_log(ic, "Error: %s returned status code %s", path, mastodon_parse_error(req));

		if (!(ic->flags & OPT_LOGGED_IN)) {
			imc_logout(ic, TRUE);
		}
		return NULL;
	}

	json_value *ret;
	if ((ret = json_parse(req->reply_body, req->body_size)) == NULL) {
		imcb_error(ic, "Error: %s return data that could not be parsed as JSON", path);
	}
	return ret;
}

struct mastodon_account *mastodon_xt_get_user(const json_value *node)
{
	struct mastodon_account *ma;
	json_value *jv;

	ma = g_new0(struct mastodon_account, 1);
	ma->display_name = g_strdup(json_o_str(node, "display_name"));
	ma->acct = g_strdup(json_o_str(node, "acct"));

	if ((jv = json_o_get(node, "id"))) {
		ma->id = jv->u.integer;
	}

	return ma;
}

// "2017-08-02T10:45:03.000Z" -- but we're ignoring microseconds and UTC timezone
#define MASTODON_TIME_FORMAT "%Y-%m-%dT%H:%M:%S"

static void expand_entities(char **text, const json_value *node, const json_value *extended_node);

/**
 * Function to fill a mastodon_status struct.
 */
static struct mastodon_status *mastodon_xt_get_status(const json_value *node)
{
	struct mastodon_status *ms = {0};
	const json_value *rt = NULL;
	const json_value *text_value = NULL;
	const json_value *url_value = NULL;
	const json_value *extended_node = NULL;

	if (node->type != json_object) {
		return FALSE;
	}
	ms = g_new0(struct mastodon_status, 1);

	JSON_O_FOREACH(node, k, v) {
		if (strcmp("content", k) == 0 && v->type == json_string && text_value == NULL) {
			text_value = v;
		} else if (strcmp("url", k) == 0 && v->type == json_string && url_value == NULL) {
			url_value = v;
		} else if (strcmp("reblog", k) == 0 && v->type == json_object) {
			rt = v;
		} else if (strcmp("created_at", k) == 0 && v->type == json_string) {
			struct tm parsed;

			/* Very sensitive to changes to the formatting of
			   this field. :-( Also assumes the timezone used
			   is UTC since C time handling functions suck. */
			if (strptime(v->u.string.ptr, MASTODON_TIME_FORMAT, &parsed) != NULL) {
				ms->created_at = mktime_utc(&parsed);
			}
		} else if (strcmp("account", k) == 0 && v->type == json_object) {
			ms->account = mastodon_xt_get_user(v);
		} else if (strcmp("id", k) == 0 && v->type == json_integer) {
			ms->rt_id = ms->id = v->u.integer;
		} else if (strcmp("in_reply_to_id", k) == 0 && v->type == json_integer) {
			ms->reply_to = v->u.integer;
		}
	}

	if (rt) {
		struct mastodon_status *rms = mastodon_xt_get_status(rt);
		if (rms) {
			ms->text = g_strdup_printf("boosted @%s: %s", rms->account->acct, rms->text);
			ms->id = rms->id;
			ms->url = g_strdup(rms->url);
			ms_free(rms);
		}
	} else if (text_value && text_value->type == json_string) {
		ms->text = g_strdup(text_value->u.string.ptr);
		strip_html(ms->text);
		expand_entities(&ms->text, node, extended_node);

		ms->url = g_strdup(url_value->u.string.ptr);
	}

	if (ms->text && ms->account && ms->id) {
		return ms;
	}

	ms_free(ms);
	return NULL;
}

/**
 * Function to fill a mastodon_notification struct.
 */
static struct mastodon_notification *mastodon_xt_get_notification(const json_value *node)
{
	if (node->type != json_object) {
		return FALSE;
	}

	struct mastodon_notification *mn = g_new0(struct mastodon_notification, 1);

	JSON_O_FOREACH(node, k, v) {
		if (strcmp("id", k) == 0 && v->type == json_integer) {
			mn->id = v->u.integer;
		} else if (strcmp("created_at", k) == 0 && v->type == json_string) {
			struct tm parsed;

			/* Very sensitive to changes to the formatting of
			   this field. :-( Also assumes the timezone used
			   is UTC since C time handling functions suck. */
			if (strptime(v->u.string.ptr, MASTODON_TIME_FORMAT, &parsed) != NULL) {
				mn->created_at = mktime_utc(&parsed);
			}
		} else if (strcmp("account", k) == 0 && v->type == json_object) {
			mn->account = mastodon_xt_get_user(v);
		} else if (strcmp("status", k) == 0 && v->type == json_object) {
			mn->status = mastodon_xt_get_status(v);
		} else if (strcmp("type", k) == 0 && v->type == json_string) {
			if (strcmp(v->u.string.ptr, "mention") == 0) {
				mn->type = MN_MENTION;
			} else if (strcmp(v->u.string.ptr, "reblog") == 0) {
				mn->type = MN_REBLOG;
			} else if (strcmp(v->u.string.ptr, "favourite") == 0) {
				mn->type = MN_FAVOURITE;
			} else if (strcmp(v->u.string.ptr, "follow") == 0) {
				mn->type = MN_FOLLOW;
			}
		}
	}

	if (mn->type) {
		return mn;
	}

	mn_free(mn);
	return NULL;
}

// FIXME: handle media instead; no quoted status
static void expand_entities(char **text, const json_value *node, const json_value *extended_node)
{
	json_value *entities, *extended_entities, *quoted;
	char *quote_url = NULL, *quote_text = NULL;

	if (!((entities = json_o_get(node, "entities")) && entities->type == json_object))
		return;
	if ((quoted = json_o_get(node, "quoted_status")) && quoted->type == json_object) {
		/* New "boosts with comments" feature. Grab the
		 * full message and try to insert it when we run into the
		 * Toot entity. */
		struct mastodon_status *ms = mastodon_xt_get_status(quoted);
		quote_text = g_strdup_printf("@%s: %s", ms->account->acct, ms->text);
		quote_url = g_strdup_printf("%s/status/%" G_GUINT64_FORMAT, ms->account->acct, ms->id);
		ms_free(ms);
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
			   in the Toot at all, or if the full-ish one *is* in it already
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

static gboolean mastodon_xt_get_status_list(struct im_connection *ic, const json_value *node,
					    struct mastodon_list *ml)
{
	ml->type = ML_STATUS;

	if (node->type != json_array) {
		return FALSE;
	}

	for (int i = 0; i < node->u.array.length; i++) {
		struct mastodon_status *ms = mastodon_xt_get_status(node->u.array.values[i]);
		if (ms) {
			ml->list = g_slist_prepend(ml->list, ms);
		}
	}
	ml->list = g_slist_reverse(ml->list);
	return TRUE;
}

static gboolean mastodon_xt_get_notification_list(struct im_connection *ic, const json_value *node,
						  struct mastodon_list *ml)
{
	ml->type = ML_NOTIFICATION;

	if (node->type != json_array) {
		return FALSE;
	}

	for (int i = 0; i < node->u.array.length; i++) {
		struct mastodon_notification *mn = mastodon_xt_get_notification(node->u.array.values[i]);
		if (mn) {
			ml->list = g_slist_prepend(ml->list, mn);
		}
	}
	ml->list = g_slist_reverse(ml->list);
	return TRUE;
}

/* Will log messages either way. Need to keep track of IDs for stream deduping.
   Plus, show_ids is on by default and I don't see why anyone would disable it. */
static char *mastodon_msg_add_id(struct im_connection *ic,
                                struct mastodon_status *ms, const char *prefix)
{
	struct mastodon_data *md = ic->proto_data;
	int reply_to = -1;
	bee_user_t *bu;

	if (ms->reply_to) {
		int i;
		for (i = 0; i < MASTODON_LOG_LENGTH; i++) {
			if (md->log[i].id == ms->reply_to) {
				reply_to = i;
				break;
			}
		}
	}

	if (ms->account && ms->account->acct &&
	    (bu = bee_user_by_handle(ic->bee, ic, ms->account->acct))) {
		struct mastodon_user_data *tud = bu->data;

		if (ms->id > tud->last_id) {
			tud->last_id = ms->id;
			tud->last_time = ms->created_at;
		}
	}

	md->log_id = (md->log_id + 1) % MASTODON_LOG_LENGTH;
	md->log[md->log_id].id = ms->id;
	md->log[md->log_id].bu = bee_user_by_handle(ic->bee, ic, ms->account->acct);

	/* This is all getting hairy. :-( If we RT'ed something ourselves,
	   remember OUR id instead so undo will work. In other cases, the
	   original toot's id should be remembered for deduplicating. */
	if (g_strcasecmp(ms->account->acct, md->user) == 0) {
		md->log[md->log_id].id = ms->rt_id;
		/* More useful than NULL. */
		md->log[md->log_id].bu = &mastodon_log_local_user;
	}

	if (set_getbool(&ic->acc->set, "show_ids")) {
		if (reply_to != -1) {
			return g_strdup_printf("\002[\002%02x->%02x\002]\002 %s%s",
			                       md->log_id, reply_to, prefix, ms->text);
		} else {
			return g_strdup_printf("\002[\002%02x\002]\002 %s%s",
			                       md->log_id, prefix, ms->text);
		}
	} else {
		if (*prefix) {
			return g_strconcat(prefix, ms->text, NULL);
		} else {
			return NULL;
		}
	}
}

/**
 * Function that is called to see the filter statuses in groupchat windows.
 */
static void mastodon_status_show_filter(struct im_connection *ic, struct mastodon_status *status)
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
			if (status->account->id != tf->uid) {
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
			imcb_chat_msg(l->data, status->account->acct,
			              msg ? msg : status->text, 0, 0);
		}
	}

	g_free(msg);
}

/**
 * Function that is called to see the statuses in a groupchat window.
 */
static void mastodon_status_show_chat(struct im_connection *ic, struct mastodon_status *status)
{
	struct mastodon_data *md = ic->proto_data;
	struct groupchat *gc;
	gboolean me = g_strcasecmp(md->user, status->account->acct) == 0;
	char *msg;

	// Create a new groupchat if it does not exsist.
	gc = mastodon_groupchat_init(ic);

	if (!me) {
		/* MUST be done before mastodon_msg_add_id() to avoid #872. */
		mastodon_add_buddy(ic, status->account->id, status->account->acct, status->account->display_name);
	}
	msg = mastodon_msg_add_id(ic, status, "");

	// Say it!
	if (me) {
		imcb_chat_log(gc, "You: %s", msg ? msg : status->text);
	} else {
		imcb_chat_msg(gc, status->account->acct,
		              msg ? msg : status->text, 0, status->created_at);
	}

	g_free(msg);
}

/**
 * Function that is called to see statuses as private messages.
 */
static void mastodon_status_show_msg(struct im_connection *ic, struct mastodon_status *status)
{
	struct mastodon_data *md = ic->proto_data;
	char from[MAX_STRING] = "";
	char *prefix = NULL, *text = NULL;
	gboolean me = g_strcasecmp(md->user, status->account->acct) == 0;

	if (md->flags & MASTODON_MODE_ONE) {
		g_snprintf(from, sizeof(from) - 1, "%s_%s", md->prefix, ic->acc->user);
		from[MAX_STRING - 1] = '\0';
	}

	if (md->flags & MASTODON_MODE_ONE) {
		prefix = g_strdup_printf("\002<\002%s\002>\002 ",
		                         status->account->acct);
	} else if (!me) {
		mastodon_add_buddy(ic, status->account->id, status->account->acct, status->account->display_name);
	} else {
		prefix = g_strdup("You: ");
	}

	text = mastodon_msg_add_id(ic, status, prefix ? prefix : "");

	imcb_buddy_msg(ic,
	               *from ? from : status->account->acct,
	               text ? text : status->text, 0, status->created_at);

	g_free(text);
	g_free(prefix);
}

struct mastodon_status *mastodon_notification_to_status(struct mastodon_notification *notification)
{
	struct mastodon_account *ma = notification->account;
	struct mastodon_status *ms = notification->status;

	if (ma == NULL) {
		// Should not happen.
		ma = g_new0(struct mastodon_account, 1);
		ma->acct = g_strdup("anon");
		ma->display_name = g_strdup("Unknown");
	}

	if (ms == NULL) {
		// Could be a FOLLOW notification without status.
		ms = g_new0(struct mastodon_status, 1);
		ms->account = notification->account;
		ms->created_at = notification->created_at;
	}

	char *original = ms->text;

	switch (notification->type) {
	case MN_MENTION:
		ms->text = g_strdup_printf("@%s mentioned you: %s", ma->acct, original);
		break;
	case MN_REBLOG:
		ms->text = g_strdup_printf("@%s boosted your status: %s", ma->acct, original);
		break;
	case MN_FAVOURITE:
		ms->text = g_strdup_printf("@%s favourited your status: %s", ma->acct, original);
		break;
	case MN_FOLLOW:
		ms->text = g_strdup_printf("@%s [%s] followed you", ma->acct, ma->display_name);
		break;
	}

	g_free(original);

	return ms;
}

static void mastodon_status_show(struct im_connection *ic, struct mastodon_status *ms)
{
	struct mastodon_data *md = ic->proto_data;

	if (ms->account == NULL || ms->text == NULL) {
		return;
	}

	/* Grrrr. Would like to do this during parsing, but can't access
	   settings from there. */
	if (set_getbool(&ic->acc->set, "strip_newlines")) {
		strip_newlines(ms->text);
	}

	if (ms->from_filter) {
		mastodon_status_show_filter(ic, ms);
	} else if (md->flags & MASTODON_MODE_CHAT) {
		mastodon_status_show_chat(ic, ms);
	} else {
		mastodon_status_show_msg(ic, ms);
	}
}

static void mastodon_notification_show(struct im_connection *ic, struct mastodon_notification *notification)
{
	mastodon_status_show(ic, mastodon_notification_to_status(notification));
}

/**
 * Add exactly one notification to the timeline.
 */
static void mastodon_stream_handle_notification(struct im_connection *ic, json_value *parsed, gboolean from_filter)
{
	struct mastodon_notification *mn = mastodon_xt_get_notification(parsed);
	if (mn) {
		mastodon_notification_show(ic, mn);
		mn_free(mn);
	}
}

/**
 * Add exactly one status to the timeline.
 */
static void mastodon_stream_handle_update(struct im_connection *ic, json_value *parsed, gboolean from_filter)
{
	struct mastodon_status *ms = mastodon_xt_get_status(parsed);
	if (ms) {
		mastodon_status_show(ic, ms);
		ms_free(ms);
	}
}

static void mastodon_stream_handle_delete(struct im_connection *ic, json_value *parsed, gboolean from_filter)
{
	struct mastodon_data *md = ic->proto_data;
	struct mastodon_status *ms = mastodon_xt_get_status(parsed);
	if (ms) {
		mastodon_log(ic, "Message %d was deleted", md->log[ms->id].id);
		ms_free(ms);
	}
}

static void mastodon_stream_handle_event(struct im_connection *ic, mastodon_evt_flags_t evt_type, json_value *parsed, gboolean from_filter)
{
	if (evt_type == MASTODON_EVT_UPDATE) {
		mastodon_stream_handle_update(ic, parsed, from_filter);
	} else if (evt_type == MASTODON_EVT_NOTIFICATION) {
		mastodon_stream_handle_notification(ic, parsed, from_filter);
	} else if (evt_type == MASTODON_EVT_DELETE) {
		mastodon_stream_handle_delete(ic, parsed, from_filter);
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
				json_value_free(parsed);
			}

			g_string_free(data, TRUE);
		}
	}

end:
	http_flush_bytes(req, len);

	/* We might have multiple events */
	if (req->body_size > 0) {
		mastodon_http_stream(req);
	}
}

gboolean mastodon_open_stream(struct im_connection *ic)
{
	struct mastodon_data *md = ic->proto_data;

	if ((md->stream = mastodon_http(ic, MASTODON_USER_STREAMING_URL,
	                               mastodon_http_stream, ic, HTTP_GET, NULL, 0))) {
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
	                                      mastodon_http_stream, ic, HTTP_GET,
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
	req = mastodon_http(ic, MASTODON_USERS_LOOKUP_URL, mastodon_filter_users_post, ic, HTTP_GET, args, 2);

	g_string_free(ustr, TRUE);
	return req != NULL;
}

/**
 * Call this one after receiving timeline/notifications. Show to user
 * once we have both.
 */
void mastodon_flush_timeline(struct im_connection *ic)
{
	struct mastodon_data *md = ic->proto_data;
	struct mastodon_list *home_timeline = md->home_timeline_obj;
	struct mastodon_list *notifications = md->notifications_obj;
	guint64 last_id = 0;
	GSList *output = NULL;
	GSList *l;

	imcb_connected(ic);

	if (!(md->flags & MASTODON_GOT_TIMELINE) ||
	    !(md->flags & MASTODON_GOT_NOTIFICATIONS)) {
		return;
	}

	if (home_timeline && home_timeline->list) {
		for (l = home_timeline->list; l; l = g_slist_next(l)) {
			output = g_slist_insert_sorted(output, l->data, mastodon_compare_elements);
		}
	}

	if (notifications && notifications->list) {
		for (l = notifications->list; l; l = g_slist_next(l)) {
			// Skip notifications older than the earliest entry in the timeline.
			struct mastodon_status *ms = mastodon_notification_to_status((struct mastodon_notification *) l->data);
			if (output && mastodon_compare_elements(ms, output->data) < 0) {
				continue;
			}

			output = g_slist_insert_sorted(output, ms, mastodon_compare_elements);
		}
	}

	// See if the user wants to see the messages in a groupchat window or as private messages.
	while (output) {
		struct mastodon_status *ms = output->data;
		if (ms->id != last_id) {
			mastodon_status_show(ic, ms);
		}
		last_id = ms->id;
		output = g_slist_remove(output, ms);
	}

	ml_free(home_timeline);
	ml_free(notifications);

	md->flags &= ~(MASTODON_DOING_TIMELINE | MASTODON_GOT_TIMELINE | MASTODON_GOT_NOTIFICATIONS);
	md->home_timeline_obj = md->notifications_obj = NULL;
}

/**
 * Callback for getting the home timeline. This runs in parallel to
 * getting the notifications.
 */
static void mastodon_http_get_home_timeline(struct http_request *req)
{
	struct im_connection *ic = req->data;
	struct mastodon_data *md;
	json_value *parsed;
	struct mastodon_list *ml;

	// Check if the connection is still active.
	if (!g_slist_find(mastodon_connections, ic)) {
		return;
	}

	md = ic->proto_data;

	if (!(parsed = mastodon_parse_response(ic, req))) {
		goto end;
	}

	ml = g_new0(struct mastodon_list, 1);
	ml->list = NULL;

	mastodon_xt_get_status_list(ic, parsed, ml);
	json_value_free(parsed);

	md->home_timeline_obj = ml;

end:
	if (!g_slist_find(mastodon_connections, ic)) {
		return;
	}

	md->flags |= MASTODON_GOT_TIMELINE;

	mastodon_flush_timeline(ic);
}

/**
 * Callback for getting the notifications. This runs in parallel to
 * getting the home timeline.
 */
static void mastodon_http_get_notifications(struct http_request *req)
{
	struct im_connection *ic = req->data;
	struct mastodon_data *md;
	json_value *parsed;
	struct mastodon_list *ml;

	// Check if the connection is still active.
	if (!g_slist_find(mastodon_connections, ic)) {
		return;
	}

	md = ic->proto_data;

	if (!(parsed = mastodon_parse_response(ic, req))) {
		goto end;
	}

	ml = g_new0(struct mastodon_list, 1);
	ml->list = NULL;

	mastodon_xt_get_notification_list(ic, parsed, ml);
	json_value_free(parsed);

	md->notifications_obj = ml;

end:
	if (!g_slist_find(mastodon_connections, ic)) {
		return;
	}

	md->flags |= MASTODON_GOT_NOTIFICATIONS;

	mastodon_flush_timeline(ic);
}

static void mastodon_get_home_timeline(struct im_connection *ic)
{
	struct mastodon_data *md = ic->proto_data;

	ml_free(md->home_timeline_obj);
	md->home_timeline_obj = NULL;
	md->flags &= ~MASTODON_GOT_TIMELINE;

	if (mastodon_http(ic, MASTODON_HOME_TIMELINE_URL, mastodon_http_get_home_timeline, ic, HTTP_GET, NULL, 0) == NULL) {
		md->flags |= MASTODON_GOT_TIMELINE;
		mastodon_flush_timeline(ic);
	}
}

static void mastodon_get_notifications(struct im_connection *ic)
{
	struct mastodon_data *md = ic->proto_data;

	ml_free(md->notifications_obj);
	md->notifications_obj = NULL;
	md->flags &= ~MASTODON_GOT_NOTIFICATIONS;

	if (mastodon_http(ic, MASTODON_NOTIFICATIONS_URL, mastodon_http_get_notifications, ic, HTTP_GET, NULL, 0) == NULL) {
		md->flags |= MASTODON_GOT_NOTIFICATIONS;
		mastodon_flush_timeline(ic);
	}
}

/**
 * Get the initial timeline. This consists of two things: the home
 * timeline, and notifications. During normal use, these are provided
 * via the Streaming API. However, when we connect to an instance we
 * want to load the home timeline and notifications. In order to sort
 * them in a meaningful way, we use various flags:
 * MASTODON_DOING_TIMELINE for the entire process,
 * MASTODON_GOT_TIMELINE to indicate that we now have home timeline,
 * MASTODON_GOT_NOTIFICATIONS to indicate that we now have notifications.
 * Both callbacks will attempt to flush the initial timeline, but this
 * will only succeed if both flags are set.
 */
void mastodon_initial_timeline(struct im_connection *ic)
{
	struct mastodon_data *md = ic->proto_data;

	imcb_log(ic, "Getting home timeline");

	md->flags |= MASTODON_DOING_TIMELINE;

	mastodon_get_home_timeline(ic);
	mastodon_get_notifications(ic);

	return;
}

/**
 * Generic callback to use after sending a POST request to mastodon
 * when the reply doesn't have any information we need. All we care
 * about are errors. If got here, there was no error, so tell the user
 * that everything went fine. If there was an id in the object
 * returned, store it for later use.
 */
static void mastodon_http_callback(struct http_request *req)
{
	struct im_connection *ic = req->data;
	struct mastodon_data *md = ic->proto_data;
	json_value *parsed, *id;

	// Check if the connection is still active.
	if (!g_slist_find(mastodon_connections, ic)) {
		return;
	}

	if (!(parsed = mastodon_parse_response(ic, req))) {
		return;
	}

	if ((id = json_o_get(parsed, "id")) && id->type == json_integer) {
		md->last_id = id->u.integer;
	} else {
		md->last_id = 0;
	}

	json_value_free(parsed);
}

/**
 * Call the generic callback function and print an acknowledgement for
 * the user.
 */
static void mastodon_http_callback_and_ack(struct http_request *req)
{
	mastodon_http_callback(req);
	if (req->status_code == 200) {
		mastodon_log(req->data, "Command processed successfully");
	}
}

/**
 * Return a static string n spaces long. No deallocation needed.
 */
static char *indent(int n)
{
	char *spaces = "          ";
	int len = 10;
	return n > len ? spaces : spaces + len - n;
}

static char *yes_or_no(int bool)
{
	return bool ? "yes" : "no";
}

static void mastodon_log_object(struct im_connection *ic, json_value *node, int prefix);

static void mastodon_log_array(struct im_connection *ic, json_value *node, int prefix)
{
	for (int i = 0; i < node->u.array.length; i++) {
		json_value *v = node->u.array.values[i];
		char *s;
		switch (v->type) {
		case json_object:
			if (v->u.object.values == 0) {
				mastodon_log(ic, "%s{}", indent(prefix));
				break;
			}
			mastodon_log(ic, "%s{", indent(prefix));
			mastodon_log_object (ic, v, prefix + 1);
			mastodon_log(ic, "%s}", indent(prefix));
			break;
		case json_array:
			if (v->u.array.length == 0) {
				mastodon_log(ic, "%s[]", indent(prefix));
				break;
			}
			mastodon_log(ic, "%s[", indent(prefix));
			for (int i = 0; i < v->u.array.length; i++) {
				mastodon_log_object (ic, node->u.array.values[i], prefix + 1);
			}
			mastodon_log(ic, "%s]", indent(prefix));
			break;
		case json_string:
			s = g_strdup(v->u.string.ptr);
			strip_html(s);
			mastodon_log(ic, "%s%s", indent(prefix), s);
			g_free(s);
			break;
		case json_double:
			mastodon_log(ic, "%s%f", indent(prefix), v->u.dbl);
			break;
		case json_integer:
			mastodon_log(ic, "%s%d", indent(prefix), v->u.boolean);
			break;
		case json_boolean:
			mastodon_log(ic, "%s%s: %s", indent(prefix), yes_or_no(v->u.boolean));
			break;
		case json_null:
			mastodon_log(ic, "%snull", indent(prefix));
			break;
		case json_none:
			mastodon_log(ic, "%snone", indent(prefix));
			break;
		}
	}
}

static void mastodon_log_object(struct im_connection *ic, json_value *node, int prefix)
{
	char *s;
	JSON_O_FOREACH(node, k, v) {
		switch (v->type) {
		case json_object:
			if (v->u.object.values == 0) {
				mastodon_log(ic, "%s%s: {}", indent(prefix), k);
				break;
			}
			mastodon_log(ic, "%s%s: {", indent(prefix), k);
			mastodon_log_object (ic, v, prefix + 1);
			mastodon_log(ic, "%s}", indent(prefix));
			break;
		case json_array:
			if (v->u.array.length == 0) {
				mastodon_log(ic, "%s%s: []", indent(prefix), k);
				break;
			}
			mastodon_log(ic, "%s%s: [", indent(prefix), k);
			mastodon_log_array(ic, v, prefix + 1);
			mastodon_log(ic, "%s]", indent(prefix));
			break;
		case json_string:
			s = g_strdup(v->u.string.ptr);
			strip_html(s);
			mastodon_log(ic, "%s%s: %s", indent(prefix), k, s);
			g_free(s);
			break;
		case json_double:
			mastodon_log(ic, "%s%s: %f", indent(prefix), k, v->u.dbl);
			break;
		case json_integer:
			mastodon_log(ic, "%s%s: %d", indent(prefix), k, v->u.boolean);
			break;
		case json_boolean:
			mastodon_log(ic, "%s%s: %s", indent(prefix), k, yes_or_no(v->u.boolean));
			break;
		case json_null:
			mastodon_log(ic, "%s%s: null", indent(prefix), k);
			break;
		case json_none:
			mastodon_log(ic, "%s%s: unknown type", indent(prefix), k);
			break;
		}
	}
}

static void mastodon_http_log_all(struct http_request *req)
{
	struct im_connection *ic = req->data;
	json_value *parsed;

	// Check if the connection is still active.
	if (!g_slist_find(mastodon_connections, ic)) {
		return;
	}

	if (!(parsed = mastodon_parse_response(ic, req))) {
		return;
	}

	if (parsed->type == json_object) {
		mastodon_log_object(ic, parsed, 0);
	} else if (parsed->type == json_array) {
		mastodon_log_array(ic, parsed, 0);
	} else {
		mastodon_log(ic, "Sadly, the response to this request is not a JSON object or array.");
	}

	json_value_free(parsed);
}

/**
 * Function to POST a new status to mastodon. We don't support the visibility levels "private" and "unlisted".
 */
void mastodon_post_status(struct im_connection *ic, char *msg, guint64 in_reply_to, int direct)
{
	char *args[6] = {
		"status", msg,
		"visibility", direct ? "public" : "direct",
		"in_reply_to_id", g_strdup_printf("%" G_GUINT64_FORMAT, in_reply_to)
	};

	// No need to acknowledge the processing of a post: we will get notified.
	mastodon_http(ic, MASTODON_STATUS_POST_URL, mastodon_http_callback, ic, HTTP_POST,
	             args, in_reply_to ? 6 : 4);

	g_free(args[5]);
}

/**
 * Generic POST request taking a numeric ID. The format string must
 * contain one placeholder for the ID, like "/accounts/%"
 * G_GINT64_FORMAT "/mute".
 */
void mastodon_post(struct im_connection *ic, char *format, guint64 id)
{
	char *url = g_strdup_printf(format, id);
	mastodon_http(ic, url, mastodon_http_callback_and_ack, ic, HTTP_POST, NULL, 0);
	g_free(url);
}

void mastodon_status_delete(struct im_connection *ic, guint64 id)
{
	char *url = g_strdup_printf(MASTODON_STATUS_URL, id);
	mastodon_http(ic, url, mastodon_http_callback_and_ack, ic, HTTP_DELETE, NULL, 0);
	g_free(url);
}

/**
 * Callback for reporting a user for sending spam.
 */
void mastodon_http_report(struct http_request *req)
{
	struct mastodon_report *mr = req->data;
	struct im_connection *ic = mr->ic;
	json_value *parsed;

	// Check if the connection is still active.
	if (!g_slist_find(mastodon_connections, ic)) {
		goto finally;
	}

	// Parse the data.
	if (!(parsed = mastodon_parse_response(ic, req))) {
		goto finally;
	}

	struct mastodon_status *ms = mastodon_xt_get_status(parsed);
	if (ms) {
		mr->account_id = ms->account->id;
		ms_free(ms);
	} else {
		mastodon_log(ic, "Error: could not fetch toot to report.");
		goto finish;
	}

	char *args[6] = {
		"account_id", g_strdup_printf("%" G_GUINT64_FORMAT, mr->account_id),
		"status_ids", g_strdup_printf("%" G_GUINT64_FORMAT, mr->status_id), // API allows an array, here
		"comment", mr->comment,
	};

	mastodon_http(ic, MASTODON_REPORT_URL, mastodon_http_callback_and_ack, ic, HTTP_POST, args, 6);

	g_free(args[1]);
	g_free(args[3]);
finish:
	ms_free(ms);
	json_value_free(parsed);
finally:
	// The report structure was created by mastodon_report and has
	// to be freed under all circumstances.
	mr_free(mr);
}

/**
 * Report a user. Since all we have is the id of the offending status,
 * we need to retrieve the status, first.
 */
void mastodon_report(struct im_connection *ic, guint64 id, char *comment)
{
	char *url = g_strdup_printf(MASTODON_STATUS_URL, id);
	struct mastodon_report *mr = g_new0(struct mastodon_report, 1);

	mr->ic = ic;
	mr->status_id = id;
	mr->comment = g_strdup(comment);

	mastodon_http(ic, url, mastodon_http_report, mr, HTTP_GET, NULL, 0);
	g_free(url);
}


void mastodon_instance(struct im_connection *ic)
{
	mastodon_http(ic, MASTODON_INSTANCE_URL, mastodon_http_log_all, ic, HTTP_GET, NULL, 0);
}

void mastodon_account(struct im_connection *ic, guint64 id)
{
	char *url = g_strdup_printf(MASTODON_ACCOUNT_URL, id);
	mastodon_http(ic, url, mastodon_http_log_all, ic, HTTP_GET, NULL, 0);
	g_free(url);
}

void mastodon_search_account(struct im_connection *ic, char *who)
{
	char *args[2] = {
		"q", who,
	};

	mastodon_http(ic, MASTODON_ACCOUNT_SEARCH_URL, mastodon_http_log_all, ic, HTTP_GET, args, 2);
}

void mastodon_relationship(struct im_connection *ic, guint64 id)
{
	char *args[2] = {
		"id", g_strdup_printf("%" G_GUINT64_FORMAT, id),
	};

	mastodon_http(ic, MASTODON_ACCOUNT_RELATIONSHIP_URL, mastodon_http_log_all, ic, HTTP_GET, args, 2);
	g_free(args[1]);
}

static void mastodon_http_search_relationship(struct http_request *req)
{
	struct im_connection *ic = req->data;
	json_value *parsed;

	// Check if the connection is still active.
	if (!g_slist_find(mastodon_connections, ic)) {
		return;
	}

	if (!(parsed = mastodon_parse_response(ic, req))) {
		return;
	}

	struct mastodon_account *ma = mastodon_xt_get_user(parsed);

	if (!ma->id) {
		mastodon_log(ic, "Couldn't find a matching account.");
		goto finish;
	}

	char *args[2] = {
		"id", g_strdup_printf("%" G_GUINT64_FORMAT, ma->id),
	};

	mastodon_http(ic, MASTODON_ACCOUNT_RELATIONSHIP_URL, mastodon_http_log_all, ic, HTTP_GET, args, 2);

	g_free(args[1]);
finish:
	ma_free(ma);
	json_value_free(parsed);
}

void mastodon_search_relationship(struct im_connection *ic, char *who)
{
	char *args[2] = {
		"q", who,
	};

	mastodon_http(ic, MASTODON_ACCOUNT_SEARCH_URL, mastodon_http_search_relationship, ic, HTTP_GET, args, 2);
}

void mastodon_status(struct im_connection *ic, guint64 id)
{
	char *url = g_strdup_printf(MASTODON_STATUS_URL, id);
	mastodon_http(ic, url, mastodon_http_log_all, ic, HTTP_GET, NULL, 0);
	g_free(url);
}

static void mastodon_http_status_show_url(struct http_request *req)
{
	struct im_connection *ic = req->data;
	json_value *parsed;

	// Check if the connection is still active.
	if (!g_slist_find(mastodon_connections, ic)) {
		return;
	}

	if (!(parsed = mastodon_parse_response(ic, req))) {
		return;
	}

	struct mastodon_status *ms = mastodon_xt_get_status(parsed);
	if (ms) {
		mastodon_log(ic, ms->url);
		ms_free(ms);
	} else {
		mastodon_log(ic, "Error: could not fetch toot url.");
	}

	json_value_free(parsed);
}

void mastodon_status_show_url(struct im_connection *ic, guint64 id)
{
	char *url = g_strdup_printf(MASTODON_STATUS_URL, id);
	mastodon_http(ic, url, mastodon_http_status_show_url, ic, HTTP_GET, NULL, 0);
	g_free(url);
}

/**
 * Call back for step 3 of mastodon_follow: adding the buddy.
 */
static void mastodon_http_follow3(struct http_request *req)
{
	struct im_connection *ic = req->data;
	json_value *parsed;

	// Check if the connection is still active.
	if (!g_slist_find(mastodon_connections, ic)) {
		return;
	}

	if (!(parsed = mastodon_parse_response(ic, req))) {
		return;
	}

	struct mastodon_account *ma = mastodon_xt_get_user(parsed);

	if (ma->id != 0 && ma->acct != NULL) {
		mastodon_add_buddy(ic, ma->id, ma->acct, ma->display_name);
		mastodon_log(ic, "You are now following %s.", ma->acct);
	} else {
		mastodon_log(ic, "This user does not have and id and account name, this is totally illegal. I'm not adding them!");
	}

	ma_free(ma);
	json_value_free(parsed);
}

/**
 * Call back for step 2 of mastodon_follow: actually following.
 */
static void mastodon_http_follow2(struct http_request *req)
{
	struct im_connection *ic = req->data;
	json_value *parsed, *it;

	// Check if the connection is still active.
	if (!g_slist_find(mastodon_connections, ic)) {
		return;
	}

	if (!(parsed = mastodon_parse_response(ic, req))) {
		return;
	}

	if ((it = json_o_get(parsed, "domain_blocking")) && it->type == json_boolean && it->u.boolean) {
		mastodon_log(ic, "This user's domain is being blocked by your instance.");
	}

	if ((it = json_o_get(parsed, "blocking")) && it->type == json_boolean && it->u.boolean) {
		mastodon_log(ic, "You need to unblock this user.");
	}

	if ((it = json_o_get(parsed, "muting")) && it->type == json_boolean && it->u.boolean) {
		mastodon_log(ic, "You might want to unmute this user.");
	}

	if ((it = json_o_get(parsed, "muting")) && it->type == json_boolean && it->u.boolean) {
		mastodon_log(ic, "You might want to unmute this user.");
	}

	if ((it = json_o_get(parsed, "requested")) && it->type == json_boolean && it->u.boolean) {
		mastodon_log(ic, "You have requested to follow this user.");
	}

	if ((it = json_o_get(parsed, "followed_by")) && it->type == json_boolean && it->u.boolean) {
		mastodon_log(ic, "Nice, this user is already following you.");
	}

	if ((it = json_o_get(parsed, "following")) && it->type == json_boolean && it->u.boolean) {
		if ((it = json_o_get(parsed, "id")) && it->type == json_integer) {
			guint64 id = it->u.integer;
			struct mastodon_data *md = ic->proto_data;
			md->last_id = id;
			char *url = g_strdup_printf(MASTODON_ACCOUNT_URL, id);
			mastodon_http(ic, url, mastodon_http_follow3, ic, HTTP_GET, NULL, 0);
			g_free(url);
		} else {
			mastodon_log(ic, "I can't believe it: this relation has no id. I can't add them!");
		}
	}

	json_value_free(parsed);
}

/**
 * Call back for step 1 of mastodon_follow: searching for the account to follow.
 */
static void mastodon_http_follow1(struct http_request *req)
{
	struct im_connection *ic = req->data;
	json_value *parsed;

	// Check if the connection is still active.
	if (!g_slist_find(mastodon_connections, ic)) {
		return;
	}

	if (!(parsed = mastodon_parse_response(ic, req))) {
		return;
	}

	if (parsed->type != json_array && parsed->u.array.length > 0) {
		goto finish;
	}

	// Just use the first one, let's hope these are sorted appropriately!
	struct mastodon_account *ma = mastodon_xt_get_user(parsed->u.array.values[0]);

	if (ma->id) {
		char *url = g_strdup_printf(MASTODON_ACCOUNT_FOLLOW_URL, ma->id);
		mastodon_http(ic, url, mastodon_http_follow2, ic, HTTP_POST, NULL, 0);
		g_free(url);
	} else {
		mastodon_log(ic, "The account found has no id. How is this even possible?");
	}

	ma_free(ma);
finish:
	json_value_free(parsed);
}

/**
 * Function to follow an unknown user. First we need to search for it,
 * though.
 */
void mastodon_follow(struct im_connection *ic, char *who)
{
	char *args[2] = {
		"q", who,
	};

	// No need to acknowledge the processing of a post: we will get notified.
	mastodon_http(ic, MASTODON_ACCOUNT_SEARCH_URL, mastodon_http_follow1, ic, HTTP_GET, args, 2);
}

/**
 * Callback for adding the buddies you are following.
 */
static void mastodon_http_following(struct http_request *req)
{
	struct im_connection *ic = req->data;
	struct mastodon_data *md = ic->proto_data;

	json_value *parsed;

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

	for (int i = 0; i < parsed->u.array.length; i++) {

		struct mastodon_account *ma = mastodon_xt_get_user(parsed->u.array.values[i]);

		if (ma->id != 0 && ma->acct != NULL) {
			mastodon_add_buddy(ic, ma->id, ma->acct, ma->display_name);
		}

		ma_free(ma);
	}

finish:
	json_value_free(parsed);

	// try to fetch more if there is a header saying that there is
	// more (URL in angled brackets)
	char *header = NULL;
	if ((header = get_rfc822_header(req->reply_headers, "Link", 0))) {

		char *url = NULL;
		char *s = NULL;
		int len = 0;

		for (int i = 0; header[i]; i++) {
			if (header[i] == '<') {
				url = header + i + 1;
			} else if (header[i] == '?') {
				header[i] = 0; // end url
				s = header + i + 1;
				len = 1;
			} else if (s && header[i] == '&') {
				header[i] = '='; // for later splitting
				len++;
			} else if (url && header[i] == '>') {
				header[i] = 0;
				if (strncmp(header + i, "; rel=\"next\"", 12) == 0) {
					break;
				} else {
					url = NULL;
					s = NULL;
					len = 0;
				}
			}
		}

		if (url) {
			gchar **args = NULL;

			if (s) {
				args = g_strsplit (s, "=", -1);
			}

			mastodon_http(ic, url, mastodon_http_following, ic, HTTP_GET, args, len);

			g_strfreev(args);
		}

		g_free(header);
	}

	md->flags |= MASTODON_HAVE_FRIENDS;
}

/**
 * Add the buddies the current account is following.
 */
void mastodon_following(struct im_connection *ic, gint64 max_id)
{
	gint64 id = set_getint(&ic->acc->set, "account_id");

	if (!id) {
		return;
	}

	// insert id into the URL
	char *url = g_strdup_printf(MASTODON_FOLLOWING_URL, id);

	// the first call must not set a max_id
	if (max_id == 0) {
		mastodon_http(ic, url, mastodon_http_following, ic, HTTP_GET, NULL, 0);
	} else {
		char *args[2];
		args[0] = "max_id";
		args[1] = g_strdup_printf("%" G_GINT64_FORMAT, max_id);

		mastodon_http(ic, url, mastodon_http_following, ic, HTTP_GET, args, 2);

		g_free(args[1]);
	}

	g_free(url);
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
	}
}

/**
 * Get the account of the current user.
 */
void mastodon_verify_credentials(struct im_connection *ic)
{
	imcb_log(ic, "Verifying credentials");
	mastodon_http(ic, MASTODON_VERIFY_CREDENTIALS_URL, mastodon_http_verify_credentials, ic, HTTP_GET, NULL, 0);
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

		char *key = json_o_strdup(parsed, "client_id");
		char *secret = json_o_strdup(parsed, "client_secret");

		json_value_free(parsed);

		// save for future sessions
		set_setstr(&ic->acc->set, "consumer_key", key);
		set_setstr(&ic->acc->set, "consumer_secret", secret);

		// and set for the current session, and connect
		struct mastodon_data *md = ic->proto_data;
		struct oauth2_service *os = md->oauth2_service;
		os->consumer_key = key;
		os->consumer_secret = secret;

		oauth2_init(ic);
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

	mastodon_http(ic, MASTODON_REGISTER_APP_URL, mastodon_http_register_app, ic, HTTP_POST, args, 8);
}
