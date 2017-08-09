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

// "2017-08-02T10:45:03.000Z" -- but we're ignoring microseconds and UTC timezone
#define MASTODON_TIME_FORMAT "%Y-%m-%dT%H:%M:%S"

#define MASTODON_REGISTER_APP_URL "/apps"
#define MASTODON_VERIFY_CREDENTIALS_URL "/accounts/verify_credentials"
#define MASTODON_STREAMING_USER_URL "/streaming/user"
#define MASTODON_STREAMING_HASHTAG_URL "/streaming/hashtag"
#define MASTODON_HOME_TIMELINE_URL "/timelines/home"
#define MASTODON_HASHTAG_TIMELINE_URL "/timelines/tag/%s"
#define MASTODON_NOTIFICATIONS_URL "/notifications"

#define MASTODON_REPORT_URL "/reports"
#define MASTODON_SEARCH_URL "/search"

#define MASTODON_INSTANCE_URL "/instance"

#define MASTODON_STATUS_POST_URL "/statuses"
#define MASTODON_STATUS_URL "/statuses/%" G_GINT64_FORMAT
#define MASTODON_STATUS_BOOST_URL "/statuses/%" G_GINT64_FORMAT "/reblog"
#define MASTODON_STATUS_UNBOOST_URL "/statuses/%" G_GINT64_FORMAT "/unreblog"
#define MASTODON_STATUS_MUTE_URL "/statuses/%" G_GINT64_FORMAT "/mute"
#define MASTODON_STATUS_UNMUTE_URL "/statuses/%" G_GINT64_FORMAT "/unmute"
#define MASTODON_STATUS_FAVOURITE_URL "/statuses/%" G_GINT64_FORMAT "/favourite"
#define MASTODON_STATUS_UNFAVOURITE_URL "/statuses/%" G_GINT64_FORMAT "/unfavourite"
#define MASTODON_STATUS_CONTEXT_URL "/statuses/%" G_GINT64_FORMAT "/context"

#define MASTODON_ACCOUNT_URL "/accounts/%" G_GINT64_FORMAT
#define MASTODON_ACCOUNT_SEARCH_URL "/accounts/search"
#define MASTODON_ACCOUNT_STATUSES_URL "/accounts/%" G_GINT64_FORMAT "/statuses"
#define MASTODON_ACCOUNT_FOLLOWING_URL "/accounts/%" G_GINT64_FORMAT "/following"
#define MASTODON_ACCOUNT_BLOCK_URL "/accounts/%" G_GINT64_FORMAT "/block"
#define MASTODON_ACCOUNT_UNBLOCK_URL "/accounts/%" G_GINT64_FORMAT "/unblock"
#define MASTODON_ACCOUNT_FOLLOW_URL "/accounts/%" G_GINT64_FORMAT "/follow"
#define MASTODON_ACCOUNT_UNFOLLOW_URL "/accounts/%" G_GINT64_FORMAT "/unfollow"
#define MASTODON_ACCOUNT_MUTE_URL "/accounts/%" G_GINT64_FORMAT "/mute"
#define MASTODON_ACCOUNT_UNMUTE_URL "/accounts/%" G_GINT64_FORMAT "/unmute"

#define MASTODON_ACCOUNT_RELATIONSHIP_URL "/accounts/relationships"

typedef enum {
	MASTODON_EVT_UNKNOWN,
	MASTODON_EVT_UPDATE,
	MASTODON_EVT_NOTIFICATION,
	MASTODON_EVT_DELETE,
} mastodon_evt_flags_t;

void mastodon_register_app(struct im_connection *ic);
void mastodon_verify_credentials(struct im_connection *ic);
void mastodon_following(struct im_connection *ic);
void mastodon_initial_timeline(struct im_connection *ic);
void mastodon_hashtag_timeline(struct im_connection *ic, char *hashtag);
void mastodon_open_stream(struct im_connection *ic);
void mastodon_post_status(struct im_connection *ic, char *msg, guint64 in_reply_to, gboolean direct);
void mastodon_post(struct im_connection *ic, char *format, guint64 id);
void mastodon_status_show_url(struct im_connection *ic, guint64 id);
void mastodon_report(struct im_connection *ic, guint64 id, char *comment);
void mastodon_follow(struct im_connection *ic, char *who);
void mastodon_status_delete(struct im_connection *ic, guint64 id);
void mastodon_instance(struct im_connection *ic);
void mastodon_account(struct im_connection *ic, guint64 id);
void mastodon_search_account(struct im_connection *ic, char *who);
void mastodon_status(struct im_connection *ic, guint64 id);
void mastodon_relationship(struct im_connection *ic, guint64 id);
void mastodon_search_relationship(struct im_connection *ic, char *who);
void mastodon_search(struct im_connection *ic, char *what);
void mastodon_context(struct im_connection *ic, guint64 id);
void mastodon_account_statuses(struct im_connection *ic, guint64 id);
