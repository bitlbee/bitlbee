/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Simple module to facilitate Mastodon functionality.                      *
*                                                                           *
*  Copyright 2009-2010 Geert Mulders <g.c.w.m.mulders@gmail.com>            *
*  Copyright 2010-2012 Wilmer van der Gaast <wilmer@gaast.net>              *
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

#pragma once

#ifdef DEBUG_MASTODON
#define debug(text ...) imcb_log(ic, text);
#else
#define debug(text ...)
#endif

#define MASTODON_OAUTH_HANDLE "mastodon_oauth"
#define MASTODON_SCOPE "read%20write%20follow" // URL escaped
#define MASTODON_URL_REGEX "https?://\\S+"
#define MASTODON_MENTION_REGEX "@(([a-zA-Z0-9_]+)@[a-zA-Z0-9.-]+[a-zA-Z0-9])"

typedef enum {
	MASTODON_HAVE_FRIENDS      = 0x00001,
	MASTODON_MODE_ONE          = 0x00002,
	MASTODON_MODE_MANY         = 0x00004,
	MASTODON_MODE_CHAT         = 0x00008,
	MASTODON_GOT_TIMELINE      = 0x10000,
	MASTODON_GOT_NOTIFICATIONS = 0x20000,
	MASTODON_GOT_STATUS        = 0x40000,
	MASTODON_GOT_CONTEXT       = 0x80000,
} mastodon_flags_t;

typedef enum {
	MASTODON_DIRECT,
	MASTODON_REPLY,
	MASTODON_MAYBE_REPLY,
	MASTODON_NEW_MESSAGE,
} mastodon_message_t;

/**
 * These are the various ways a command can influence the undo/redo
 * queue.
 */
typedef enum {
	MASTODON_NEW,
	MASTODON_UNDO,
	MASTODON_REDO,
} mastodon_undo_t;

/**
 * These are the commands that can be undone and redone.
 */
typedef enum {
	MC_UNKNOWN,
	MC_POST,
	MC_DELETE,
	MC_FOLLOW,
	MC_UNFOLLOW,
	MC_BLOCK,
	MC_UNBLOCK,
	MC_FAVOURITE,
	MC_UNFAVOURITE,
	MC_ACCOUNT_MUTE,
	MC_ACCOUNT_UNMUTE,
	MC_STATUS_MUTE,
	MC_STATUS_UNMUTE,
	MC_BOOST,
	MC_UNBOOST,
} mastodon_command_type_t;

struct mastodon_log_data;

#define MASTODON_MAX_UNDO 10

struct mastodon_data {
	char* user;
	struct oauth2_service *oauth2_service;
	char *oauth2_access_token;

	gpointer home_timeline_obj; /* of mastodon_list */
	gpointer notifications_obj; /* of mastodon_list */
	gpointer status_obj; /* of mastodon_status */
	gpointer context_before_obj; /* of mastodon_list */
	gpointer context_after_obj; /* of mastodon_list */

	GSList *streams; /* of struct http_request */
	struct groupchat *timeline_gc;
	guint64 last_id; /* For replying or deleting the user's last status */
	guint64 seen_id; /* For deduplication */
	mastodon_flags_t flags;

	mastodon_undo_t undo_type; /* for the current command */
	char *undo[MASTODON_MAX_UNDO]; /* a small stack of undo statements */
	char *redo[MASTODON_MAX_UNDO]; /* a small stack of redo statements */
	int first_undo; /* index of the latest item in the undo and redo stacks */
	int current_undo; /* index of the current item in the undo and redo stacks */

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

struct mastodon_user_data {
	guint64 account_id;
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
void oauth2_init(struct im_connection *ic);
struct groupchat *mastodon_groupchat_init(struct im_connection *ic);

void mastodon_do(struct im_connection *ic, char *redo, char *undo);
void mastodon_do_update(struct im_connection *ic, char *to);
