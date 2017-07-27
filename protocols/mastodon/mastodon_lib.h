/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Simple module to facilitate mastodon functionality.                       *
*                                                                           *
*  Copyright 2009 Geert Mulders <g.c.w.m.mulders@gmail.com>                 *
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


#ifndef _MASTODON_LIB_H
#define _MASTODON_LIB_H

#include "nogaim.h"
#include "mastodon_http.h"

#define MASTODON_API_URL "https://octodon.social/api/v1"

#define MASTODON_REGISTER_APP_URL "/apps"
#define MASTODON_VERIFY_CREDENTIALS_URL "/accounts/verify_credentials"
#define MASTODON_FOLLOWING_URL "/accounts/%" G_GINT64_FORMAT "/following"
#define MASTODON_USER_STREAMING_URL "/streaming/user"
#define MASTODON_HOME_TIMELINE_URL "/timelines/home"
#define MASTODON_NOTIFICATIONS_URL "/notifications"

typedef enum {
	MASTODON_EVT_UNKNOWN,
	MASTODON_EVT_UPDATE,
	MASTODON_EVT_NOTIFICATION,
	MASTODON_EVT_DELETE,
} mastodon_evt_flags_t;

/* Anything below is unchecked, renamed Twitter stuff. */

/* Status URLs */
#define MASTODON_STATUS_UPDATE_URL "/statuses/update.json"
#define MASTODON_STATUS_SHOW_URL "/statuses/show/"
#define MASTODON_STATUS_DESTROY_URL "/statuses/destroy/"
#define MASTODON_STATUS_RETWEET_URL "/statuses/retweet/"

/* Timeline URLs */
#define MASTODON_PUBLIC_TIMELINE_URL "/statuses/public_timeline.json"
#define MASTODON_FEATURED_USERS_URL "/statuses/featured.json"
#define MASTODON_FRIENDS_TIMELINE_URL "/statuses/friends_timeline.json"
#define MASTODON_MENTIONS_URL "/statuses/mentions_timeline.json"
#define MASTODON_USER_TIMELINE_URL "/statuses/user_timeline.json"

/* Users URLs */
#define MASTODON_USERS_LOOKUP_URL "/users/lookup.json"

/* Direct messages URLs */
#define MASTODON_DIRECT_MESSAGES_URL "/direct_messages.json"
#define MASTODON_DIRECT_MESSAGES_NEW_URL "/direct_messages/new.json"
#define MASTODON_DIRECT_MESSAGES_SENT_URL "/direct_messages/sent.json"
#define MASTODON_DIRECT_MESSAGES_DESTROY_URL "/direct_messages/destroy/"

/* Friendships URLs */
#define MASTODON_FRIENDSHIPS_CREATE_URL "/friendships/create.json"
#define MASTODON_FRIENDSHIPS_DESTROY_URL "/friendships/destroy.json"
#define MASTODON_FRIENDSHIPS_SHOW_URL "/friendships/show.json"

/* Social graphs URLs */
#define MASTODON_FRIENDS_IDS_URL "/friends/ids.json"
#define MASTODON_FOLLOWERS_IDS_URL "/followers/ids.json"
#define MASTODON_MUTES_IDS_URL "/mutes/users/ids.json"
#define MASTODON_NORETWEETS_IDS_URL "/friendships/no_retweets/ids.json"

/* Account URLs */
#define MASTODON_ACCOUNT_RATE_LIMIT_URL "/account/rate_limit_status.json"

/* Favorites URLs */
#define MASTODON_FAVORITES_GET_URL "/favorites.json"
#define MASTODON_FAVORITE_CREATE_URL "/favorites/create.json"
#define MASTODON_FAVORITE_DESTROY_URL "/favorites/destroy.json"

/* Block URLs */
#define MASTODON_BLOCKS_CREATE_URL "/blocks/create/"
#define MASTODON_BLOCKS_DESTROY_URL "/blocks/destroy/"

/* Mute URLs */
#define MASTODON_MUTES_CREATE_URL "/mutes/users/create.json"
#define MASTODON_MUTES_DESTROY_URL "/mutes/users/destroy.json"

/* Report spam */
#define MASTODON_REPORT_SPAM_URL "/users/report_spam.json"

/* Stream URLs */
#define MASTODON_FILTER_STREAM_URL "https://stream.mastodon.com/1.1/statuses/filter.json"

gboolean mastodon_open_stream(struct im_connection *ic);
gboolean mastodon_open_filter_stream(struct im_connection *ic);
gboolean mastodon_get_timeline(struct im_connection *ic, gint64 next_cursor);
void mastodon_get_friends_ids(struct im_connection *ic, gint64 next_cursor);
void mastodon_get_mutes_ids(struct im_connection *ic, gint64 next_cursor);
void mastodon_get_noretweets_ids(struct im_connection *ic, gint64 next_cursor);
void mastodon_get_statuses_friends(struct im_connection *ic, gint64 next_cursor);

void mastodon_following(struct im_connection *ic, gint64 next_cursor);
void mastodon_verify_credentials(struct im_connection *ic);
void mastodon_register_app(struct im_connection *ic);
void mastodon_post_status(struct im_connection *ic, char *msg, guint64 in_reply_to);
void mastodon_direct_messages_new(struct im_connection *ic, char *who, char *message);
void mastodon_friendships_create_destroy(struct im_connection *ic, char *who, int create);
void mastodon_mute_create_destroy(struct im_connection *ic, char *who, int create);
void mastodon_status_destroy(struct im_connection *ic, guint64 id);
void mastodon_status_retweet(struct im_connection *ic, guint64 id);
void mastodon_report_spam(struct im_connection *ic, char *screen_name);
void mastodon_favourite_tweet(struct im_connection *ic, guint64 id);
void mastodon_status_show_url(struct im_connection *ic, guint64 id);

#endif //_MASTODON_LIB_H

