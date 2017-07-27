/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Simple module to facilitate mastodon functionality.                       *
*                                                                           *
*  Copyright 2009-2010 Geert Mulders <g.c.w.m.mulders@gmail.com>            *
*  Copyright 2010-2012 Wilmer van der Gaast <wilmer@gaast.net>              *
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

#pragma once

#ifdef DEBUG_MASTODON
#define debug(text ...) imcb_log(ic, text);
#else
#define debug(text ...)
#endif

#define MASTODON_OAUTH_HANDLE "mastodon_oauth"
#define MASTODON_SCOPE "read%20write%20follow" // URL escaped

typedef enum {
	MASTODON_HAVE_FRIENDS   = 0x00001,
	MASTODON_MODE_ONE       = 0x00002,
	MASTODON_MODE_MANY      = 0x00004,
	MASTODON_MODE_CHAT      = 0x00008,
	MASTODON_DOING_TIMELINE = 0x10000,
	MASTODON_GOT_TIMELINE   = 0x20000,
	MASTODON_GOT_MENTIONS   = 0x40000,
} mastodon_flags_t;

typedef enum {
	MASTODON_FILTER_TYPE_FOLLOW = 0,
	MASTODON_FILTER_TYPE_TRACK
} mastodon_filter_type_t;

struct mastodon_log_data;

struct mastodon_data {
	char* user;
	struct oauth2_service *oauth2_service;
	char *oauth2_access_token;

	gpointer home_timeline_obj;
	gpointer mentions_obj;

	GSList *follow_ids;
	GSList *mutes_ids;
	GSList *noretweets_ids;
	GSList *filters;

	guint64 last_status_id; /* For undo */
	gint main_loop_id;
	gint filter_update_id;
	struct http_request *stream;
	struct http_request *filter_stream;
	struct groupchat *timeline_gc;
	gint http_fails;
	mastodon_flags_t flags;

	/* set base_url */
	gboolean url_ssl;
	int url_port;
	char *url_host;
	char *url_path;

	char *prefix; /* Used to generate contact + channel name. */

	/* set show_ids */
	struct mastodon_log_data *log;
	int log_id;
};

#define MASTODON_FILTER_UPDATE_WAIT 3000
struct mastodon_filter {
	mastodon_filter_type_t type;
	char *text;
	guint64 uid;
	GSList *groupchats;
};

struct mastodon_user_data {
	guint64 last_id;
	time_t last_time;
};

#define MASTODON_LOG_LENGTH 256
struct mastodon_log_data {
	guint64 id;
	/* DANGER: bu can be a dead pointer. Check it first.
	 * mastodon_message_id_from_command_arg() will do this. */
	struct bee_user *bu;
};

/**
 * This has the same function as the msn_connections GSList. We use this to
 * make sure the connection is still alive in callbacks before we do anything
 * else.
 */
extern GSList *mastodon_connections;

/**
 * Evil hack: Fake bee_user which will always point at the local user.
 * Sometimes used as a return value by mastodon_message_id_from_command_arg.
 * NOT thread safe but don't you dare to even think of ever making BitlBee
 * threaded. :-)
 */
extern bee_user_t mastodon_log_local_user;

struct http_request;
char *mastodon_parse_error(struct http_request *req);

void mastodon_log(struct im_connection *ic, char *format, ...);
struct groupchat *mastodon_groupchat_init(struct im_connection *ic);
