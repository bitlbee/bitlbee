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
 * Main loop function
 */
gboolean twitter_main_loop(gpointer data, gint fd, b_input_condition cond)
{
	struct im_connection *ic = data;
	// Check if we are still logged in...
	// We are logged in if the flag says so and the connection is still in the connections list.
	if (!g_slist_find( twitter_connections, ic ) ||
	    (ic->flags & OPT_LOGGED_IN) != OPT_LOGGED_IN) 
		return 0;

	// If the user uses multiple private message windows we need to get the 
	// users buddies.
	if (!set_getbool( &ic->acc->set, "use_groupchat" ))
		twitter_get_statuses_friends(ic, -1);

	// Do stuff..
	twitter_get_home_timeline(ic, -1);

	// If we are still logged in run this function again after timeout.
	return (ic->flags & OPT_LOGGED_IN) == OPT_LOGGED_IN;
}


static void twitter_init( account_t *acc )
{
	set_t *s;
	s = set_add( &acc->set, "use_groupchat", "false", set_eval_bool, acc );
	s->flags |= ACC_SET_OFFLINE_ONLY;
}

/**
 * Login method. Since the twitter API works with seperate HTTP request we 
 * only save the user and pass to the twitter_data object.
 */
static void twitter_login( account_t *acc )
{
	struct im_connection *ic = imcb_new( acc );
	struct twitter_data *td = g_new0( struct twitter_data, 1 );

	twitter_connections = g_slist_append( twitter_connections, ic );

	td->user = acc->user;
	td->pass = acc->pass;
	td->home_timeline_id = 0;

	ic->proto_data = td;

	imcb_log( ic, "Connecting to Twitter" );
	imcb_connected(ic);

	// Run this once. After this queue the main loop function.
	twitter_main_loop(ic, -1, 0);

	// Queue the main_loop
	// Save the return value, so we can remove the timeout on logout.
	td->main_loop_id = b_timeout_add(60000, twitter_main_loop, ic);
}

/**
 * Logout method. Just free the twitter_data.
 */
static void twitter_logout( struct im_connection *ic )
{
	struct twitter_data *td = ic->proto_data;
	
	// Set the status to logged out.
	ic->flags = 0;

	// Remove the main_loop function from the function queue.
	b_event_remove(td->main_loop_id);

	if(td->home_timeline_gc)
		imcb_chat_free(td->home_timeline_gc);

	if( td )
	{
		g_free( td );
	}

	twitter_connections = g_slist_remove( twitter_connections, ic );
}

/**
 *
 */
static int twitter_buddy_msg( struct im_connection *ic, char *who, char *message, int away )
{
	// Let's just update the status.
//	if ( g_strcasecmp(who, ic->acc->user) == 0 )
		twitter_post_status(ic, message);
//	else
//		twitter_direct_messages_new(ic, who, message);
	return( 0 );
}

/**
 *
 */
static void twitter_set_my_name( struct im_connection *ic, char *info )
{
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
	if( c && message )
		twitter_post_status(c->ic, message);
}

static void twitter_chat_invite( struct groupchat *c, char *who, char *message )
{
}

static void twitter_chat_leave( struct groupchat *c )
{
	struct twitter_data *td = c->ic->proto_data;
	
	if( c != td->home_timeline_gc )
		return; /* WTF? */
	
	/* If the user leaves the channel: Fine. Rejoin him/her once new
	   tweets come in. */
	imcb_chat_free(td->home_timeline_gc);
	td->home_timeline_gc = NULL;
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

	// Initialise the twitter_connections GSList.
	twitter_connections = NULL;
}

