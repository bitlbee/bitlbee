/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Simple module to facilitate Mastodon functionality.                      *
*                                                                           *
*  Copyright 2009 Geert Mulders <g.c.w.m.mulders@gmail.com>                 *
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

#pragma once

#include "nogaim.h"
#include "mastodon_http.h"

#define MASTODON_API_URL "https://octodon.social/api/v1"

#define MASTODON_REGISTER_APP_URL "/apps"
#define MASTODON_VERIFY_CREDENTIALS_URL "/accounts/verify_credentials"
#define MASTODON_FOLLOWING_URL "/accounts/%" G_GINT64_FORMAT "/following"
#define MASTODON_USER_STREAMING_URL "/streaming/user"
#define MASTODON_HOME_TIMELINE_URL "/timelines/home"
#define MASTODON_NOTIFICATIONS_URL "/notifications"
#define MASTODON_REPORT_URL "/reports"

#define MASTODON_INSTANCE_URL "/instance"

#define MASTODON_STATUS_POST_URL "/statuses"
#define MASTODON_STATUS_URL "/statuses/%" G_GINT64_FORMAT
#define MASTODON_STATUS_BOOST_URL "/statuses/%" G_GINT64_FORMAT "/reblog"
#define MASTODON_STATUS_UNBOOST_URL "/statuses/%" G_GINT64_FORMAT "/unreblog"
#define MASTODON_STATUS_MUTE_URL "/statuses/%" G_GINT64_FORMAT "/mute"
#define MASTODON_STATUS_UNMUTE_URL "/statuses/%" G_GINT64_FORMAT "/unmute"
#define MASTODON_STATUS_FAVOURITE_URL "/statuses/%" G_GINT64_FORMAT "/favourite"
#define MASTODON_STATUS_UNFAVOURITE_URL "/statuses/%" G_GINT64_FORMAT "/unfavourite"

#define MASTODON_ACCOUNT_URL "/accounts/%" G_GINT64_FORMAT
#define MASTODON_ACCOUNT_SEARCH_URL "/accounts/search"
#define MASTODON_ACCOUNT_BLOCK_URL "/accounts/%" G_GINT64_FORMAT "/block"
#define MASTODON_ACCOUNT_UNBLOCK_URL "/accounts/%" G_GINT64_FORMAT "/unblock"
#define MASTODON_ACCOUNT_FOLLOW_URL "/accounts/%" G_GINT64_FORMAT "/follow"
#define MASTODON_ACCOUNT_UNFOLLOW_URL "/accounts/%" G_GINT64_FORMAT "/unfollow"
#define MASTODON_ACCOUNT_MUTE_URL "/accounts/%" G_GINT64_FORMAT "/mute"
#define MASTODON_ACCOUNT_UNMUTE_URL "/accounts/%" G_GINT64_FORMAT "/unmute"

typedef enum {
	MASTODON_EVT_UNKNOWN,
	MASTODON_EVT_UPDATE,
	MASTODON_EVT_NOTIFICATION,
	MASTODON_EVT_DELETE,
} mastodon_evt_flags_t;

void mastodon_register_app(struct im_connection *ic);
void mastodon_verify_credentials(struct im_connection *ic);
void mastodon_following(struct im_connection *ic, gint64 next_cursor);
void mastodon_initial_timeline(struct im_connection *ic);
void mastodon_post_status(struct im_connection *ic, char *msg, guint64 in_reply_to, int direct);
void mastodon_post(struct im_connection *ic, char *format, guint64 id);
void mastodon_status_show_url(struct im_connection *ic, guint64 id);
void mastodon_report(struct im_connection *ic, guint64 id, char *comment);
void mastodon_follow(struct im_connection *ic, char *who);
void mastodon_status_delete(struct im_connection *ic, guint64 id);
void mastodon_instance(struct im_connection *ic);
void mastodon_account(struct im_connection *ic, guint64 id);
void mastodon_search_account(struct im_connection *ic, char *who);
void mastodon_status(struct im_connection *ic, guint64 id);

/* Anything below is unchecked, renamed Twitter stuff. */

/* Status URLs */
#define MASTODON_STATUS_DESTROY_URL "/statuses/destroy/"

/* Timeline URLs */
#define MASTODON_PUBLIC_TIMELINE_URL "/statuses/public_timeline.json"
#define MASTODON_FEATURED_USERS_URL "/statuses/featured.json"
#define MASTODON_FRIENDS_TIMELINE_URL "/statuses/friends_timeline.json"
#define MASTODON_USER_TIMELINE_URL "/statuses/user_timeline.json"

/* Users URLs */
#define MASTODON_USERS_LOOKUP_URL "/users/lookup.json"

/* Direct messages URLs */
#define MASTODON_DIRECT_MESSAGES_URL "/direct_messages.json"
#define MASTODON_DIRECT_MESSAGES_NEW_URL "/direct_messages/new.json"
#define MASTODON_DIRECT_MESSAGES_SENT_URL "/direct_messages/sent.json"
#define MASTODON_DIRECT_MESSAGES_DESTROY_URL "/direct_messages/destroy/"

/* Social graphs URLs */
#define MASTODON_FRIENDS_IDS_URL "/friends/ids.json"
#define MASTODON_FOLLOWERS_IDS_URL "/followers/ids.json"
#define MASTODON_MUTES_IDS_URL "/mutes/users/ids.json"
#define MASTODON_NOBOOSTS_IDS_URL "/friendships/no_boosts/ids.json"

/* Account URLs */
#define MASTODON_ACCOUNT_RATE_LIMIT_URL "/account/rate_limit_status.json"

/* Stream URLs */
#define MASTODON_FILTER_STREAM_URL "https://stream.mastodon.com/1.1/statuses/filter.json"

gboolean mastodon_open_stream(struct im_connection *ic);
gboolean mastodon_open_filter_stream(struct im_connection *ic);
