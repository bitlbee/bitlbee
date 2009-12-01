/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Simple module to facilitate twitter functionality.                       *
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

#include "nogaim.h"
#include "twitter.h"
#include "twitter_http.h"
#include "twitter_lib.h"


/**
 *  * Main loop function
 *   */
gboolean twitter_main_loop(gpointer data, gint fd, b_input_condition cond)
{
	struct im_connection *ic = data;
	// Check if we are still logged in...
	if ((ic->flags & OPT_LOGGED_IN) != OPT_LOGGED_IN)
		return 0;

	// Do stuff..
	twitter_get_home_timeline(ic, -1);

	// If we are still logged in run this function again after timeout.
	return (ic->flags & OPT_LOGGED_IN) == OPT_LOGGED_IN;
}


static void twitter_init( account_t *acc )
{
}

/**
 * Login method. Since the twitter API works with seperate HTTP request we 
 * only save the user and pass to the twitter_data object.
 */
static void twitter_login( account_t *acc )
{
	struct im_connection *ic = imcb_new( acc );
	struct twitter_data *td = g_new0( struct twitter_data, 1 );
	
	td->user = acc->user;
	td->pass = acc->pass;
	td->home_timeline_id = 0;

	ic->proto_data = td;

	// Set the status to logged in.
	ic->flags = OPT_LOGGED_IN;

	// Try to get the buddies...
	//twitter_get_friends_ids(ic, -1);

	//twitter_get_home_timeline(ic, -1);

	// Run this once. After this queue the main loop function.
	twitter_main_loop(ic, -1, 0);

	// Queue the main_loop
	b_timeout_add(60000, twitter_main_loop, ic);

	imcb_log( ic, "Connecting to twitter" );
	imcb_connected(ic);
}

/**
 * Logout method. Just free the twitter_data.
 */
static void twitter_logout( struct im_connection *ic )
{
	struct twitter_data *td = ic->proto_data;
	
	// Set the status to logged out.
	ic->flags = 0;

	if( td )
	{
		g_free( td );
	}
}

/**
 *
 */
static int twitter_buddy_msg( struct im_connection *ic, char *who, char *message, int away )
{
	imcb_log( ic, "In twitter_buddy_msg...");
	twitter_post_status(ic, message);
	return( 0 );
}

/**
 *
 */
static GList *twitter_away_states( struct im_connection *ic )
{
	static GList *l = NULL;
	return l;
}

static void twitter_set_away( struct im_connection *ic, char *state, char *message )
{
}

static void twitter_set_my_name( struct im_connection *ic, char *info )
{
	imcb_log( ic, "In twitter_set_my_name..." );
//	char * aap = twitter_http("http://gertje.org", NULL, ic, 1, "geert", "poep", NULL, 0);

//	imcb_log( ic, aap );
//	g_free(aap);
}

static void twitter_get_info(struct im_connection *ic, char *who) 
{
}

static void twitter_add_buddy( struct im_connection *ic, char *who, char *group )
{
}

static void twitter_remove_buddy( struct im_connection *ic, char *who, char *group )
{
}

static void twitter_chat_msg( struct groupchat *c, char *message, int flags )
{
}

static void twitter_chat_invite( struct groupchat *c, char *who, char *message )
{
}

static void twitter_chat_leave( struct groupchat *c )
{
}

static struct groupchat *twitter_chat_with( struct im_connection *ic, char *who )
{
	return NULL;
}

static void twitter_keepalive( struct im_connection *ic )
{
}

static void twitter_add_permit( struct im_connection *ic, char *who )
{
}

static void twitter_rem_permit( struct im_connection *ic, char *who )
{
}

static void twitter_add_deny( struct im_connection *ic, char *who )
{
}

static void twitter_rem_deny( struct im_connection *ic, char *who )
{
}

static int twitter_send_typing( struct im_connection *ic, char *who, int typing )
{
	return( 1 );
}

//static char *twitter_set_display_name( set_t *set, char *value )
//{
//	return value;
//}

void twitter_initmodule()
{
	struct prpl *ret = g_new0(struct prpl, 1);
	
	ret->name = "twitter";
	ret->login = twitter_login;
	ret->init = twitter_init;
	ret->logout = twitter_logout;
	ret->buddy_msg = twitter_buddy_msg;
	ret->away_states = twitter_away_states;
	ret->set_away = twitter_set_away;
	ret->get_info = twitter_get_info;
	ret->set_my_name = twitter_set_my_name;
	ret->add_buddy = twitter_add_buddy;
	ret->remove_buddy = twitter_remove_buddy;
	ret->chat_msg = twitter_chat_msg;
	ret->chat_invite = twitter_chat_invite;
	ret->chat_leave = twitter_chat_leave;
	ret->chat_with = twitter_chat_with;
	ret->keepalive = twitter_keepalive;
	ret->add_permit = twitter_add_permit;
	ret->rem_permit = twitter_rem_permit;
	ret->add_deny = twitter_add_deny;
	ret->rem_deny = twitter_rem_deny;
	ret->send_typing = twitter_send_typing;
	ret->handle_cmp = g_strcasecmp;

	register_protocol(ret);
}

