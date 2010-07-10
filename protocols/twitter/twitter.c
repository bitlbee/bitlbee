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
#include "oauth.h"
#include "twitter.h"
#include "twitter_http.h"
#include "twitter_lib.h"
#include "url.h"

/**
 * Main loop function
 */
gboolean twitter_main_loop(gpointer data, gint fd, b_input_condition cond)
{
	struct im_connection *ic = data;
	
	// Check if we are still logged in...
	if (!g_slist_find( twitter_connections, ic ))
		return 0;

	// Do stuff..
	twitter_get_home_timeline(ic, -1);

	// If we are still logged in run this function again after timeout.
	return (ic->flags & OPT_LOGGED_IN) == OPT_LOGGED_IN;
}

static void twitter_main_loop_start( struct im_connection *ic )
{
	struct twitter_data *td = ic->proto_data;
	
	imcb_log( ic, "Getting initial statuses" );

	// Run this once. After this queue the main loop function.
	twitter_main_loop(ic, -1, 0);

	// Queue the main_loop
	// Save the return value, so we can remove the timeout on logout.
	td->main_loop_id = b_timeout_add(60000, twitter_main_loop, ic);
}

static void twitter_oauth_start( struct im_connection *ic );

void twitter_login_finish( struct im_connection *ic )
{
	struct twitter_data *td = ic->proto_data;
	
	if( set_getbool( &ic->acc->set, "oauth" ) && !td->oauth_info )
		twitter_oauth_start( ic );
	else if( g_strcasecmp( set_getstr( &ic->acc->set, "mode" ), "one" ) != 0 &&
	         !( td->flags & TWITTER_HAVE_FRIENDS ) )
	{
		imcb_log( ic, "Getting contact list" );
		twitter_get_statuses_friends( ic, -1 );
	}
	else
		twitter_main_loop_start( ic );
}

static const struct oauth_service twitter_oauth =
{
	"http://api.twitter.com/oauth/request_token",
	"http://api.twitter.com/oauth/access_token",
	"https://api.twitter.com/oauth/authorize",
	.consumer_key = "xsDNKJuNZYkZyMcu914uEA",
	.consumer_secret = "FCxqcr0pXKzsF9ajmP57S3VQ8V6Drk4o2QYtqMcOszo",
};

static gboolean twitter_oauth_callback( struct oauth_info *info );

static void twitter_oauth_start( struct im_connection *ic )
{
	struct twitter_data *td = ic->proto_data;
	
	imcb_log( ic, "Requesting OAuth request token" );

	td->oauth_info = oauth_request_token( &twitter_oauth, twitter_oauth_callback, ic );
}

static gboolean twitter_oauth_callback( struct oauth_info *info )
{
	struct im_connection *ic = info->data;
	struct twitter_data *td;
	
	if( !g_slist_find( twitter_connections, ic ) )
		return FALSE;
	
	td = ic->proto_data;
	if( info->stage == OAUTH_REQUEST_TOKEN )
	{
		char name[strlen(ic->acc->user)+9], *msg;
		
		if( info->request_token == NULL )
		{
			imcb_error( ic, "OAuth error: %s", info->http->status_string );
			imc_logout( ic, TRUE );
			return FALSE;
		}
		
		sprintf( name, "twitter_%s", ic->acc->user );
		msg = g_strdup_printf( "To finish OAuth authentication, please visit "
		                       "%s and respond with the resulting PIN code.",
		                       info->auth_url );
		imcb_buddy_msg( ic, name, msg, 0, 0 );
		g_free( msg );
	}
	else if( info->stage == OAUTH_ACCESS_TOKEN )
	{
		if( info->token == NULL || info->token_secret == NULL )
		{
			imcb_error( ic, "OAuth error: %s", info->http->status_string );
			imc_logout( ic, TRUE );
			return FALSE;
		}
		
		/* IM mods didn't do this so far and it's ugly but I should
		   be able to get away with it... */
		g_free( ic->acc->pass );
		ic->acc->pass = oauth_to_string( info );
		
		twitter_login_finish( ic );
	}
	
	return TRUE;
}


static char *set_eval_mode( set_t *set, char *value )
{
	if( g_strcasecmp( value, "one" ) == 0 ||
	    g_strcasecmp( value, "many" ) == 0 ||
	    g_strcasecmp( value, "chat" ) == 0 )
		return value;
	else
		return NULL;
}

static gboolean twitter_length_check( struct im_connection *ic, gchar *msg )
{
	int max = set_getint( &ic->acc->set, "message_length" ), len;
	
	if( max == 0 || ( len = g_utf8_strlen( msg, -1 ) ) <= max )
		return TRUE;
	
	imcb_error( ic, "Maximum message length exceeded: %d > %d", len, max );
	
	return FALSE;
}

static void twitter_init( account_t *acc )
{
	set_t *s;
	
	s = set_add( &acc->set, "base_url", TWITTER_API_URL, NULL, acc );
	s->flags |= ACC_SET_OFFLINE_ONLY;
	
	s = set_add( &acc->set, "message_length", "140", set_eval_int, acc );
	
	s = set_add( &acc->set, "mode", "one", set_eval_mode, acc );
	s->flags |= ACC_SET_OFFLINE_ONLY;
	
	s = set_add( &acc->set, "oauth", "true", set_eval_bool, acc );
}

/**
 * Login method. Since the twitter API works with seperate HTTP request we 
 * only save the user and pass to the twitter_data object.
 */
static void twitter_login( account_t *acc )
{
	struct im_connection *ic = imcb_new( acc );
	struct twitter_data *td;
	char name[strlen(acc->user)+9];
	url_t url;

	if( !url_set( &url, set_getstr( &ic->acc->set, "base_url" ) ) ||
	    ( url.proto != PROTO_HTTP && url.proto != PROTO_HTTPS ) )
	{
		imcb_error( ic, "Incorrect API base URL: %s", set_getstr( &ic->acc->set, "base_url" ) );
		imc_logout( ic, FALSE );
		return;
	}
	
	twitter_connections = g_slist_append( twitter_connections, ic );
	td = g_new0( struct twitter_data, 1 );
	ic->proto_data = td;
	
	td->url_ssl = url.proto == PROTO_HTTPS;
	td->url_port = url.port;
	td->url_host = g_strdup( url.host );
	if( strcmp( url.file, "/" ) != 0 )
		td->url_path = g_strdup( url.file );
	else
		td->url_path = g_strdup( "" );
	
	td->user = acc->user;
	if( strstr( acc->pass, "oauth_token=" ) )
		td->oauth_info = oauth_from_string( acc->pass, &twitter_oauth );
	
	sprintf( name, "twitter_%s", acc->user );
	imcb_add_buddy( ic, name, NULL );
	imcb_buddy_status( ic, name, OPT_LOGGED_IN, NULL, NULL );
	
	imcb_log( ic, "Connecting" );
	
	twitter_login_finish( ic );
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
		oauth_info_free( td->oauth_info );
		g_free( td->url_host );
		g_free( td->url_path );
		g_free( td->pass );
		g_free( td );
	}

	twitter_connections = g_slist_remove( twitter_connections, ic );
}

/**
 *
 */
static int twitter_buddy_msg( struct im_connection *ic, char *who, char *message, int away )
{
	struct twitter_data *td = ic->proto_data;
	
	if (g_strncasecmp(who, "twitter_", 8) == 0 &&
	    g_strcasecmp(who + 8, ic->acc->user) == 0)
	{
		if( set_getbool( &ic->acc->set, "oauth" ) &&
		    td->oauth_info && td->oauth_info->token == NULL )
		{
			char pin[strlen(message)+1], *s;
			
			strcpy( pin, message );
			for( s = pin + sizeof( pin ) - 2; s > pin && isspace( *s ); s -- )
				*s = '\0';
			for( s = pin; *s && isspace( *s ); s ++ ) {}
			
			if( !oauth_access_token( s, td->oauth_info ) )
			{
				imcb_error( ic, "OAuth error: %s", "Failed to send access token request" );
				imc_logout( ic, TRUE );
				return FALSE;
			}
		}
		else if( twitter_length_check(ic, message) )
			twitter_post_status(ic, message);
	}
	else
	{
		twitter_direct_messages_new(ic, who, message);
	}
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
	twitter_friendships_create_destroy(ic, who, 1);
}

static void twitter_remove_buddy( struct im_connection *ic, char *who, char *group )
{
	twitter_friendships_create_destroy(ic, who, 0);
}

static void twitter_chat_msg( struct groupchat *c, char *message, int flags )
{
	if( c && message && twitter_length_check( c->ic, message ) )
	{
		char *s, *new = NULL;
		
		if( ( s = strchr( message, ':' ) ) ||
		    ( s = strchr( message, ',' ) ) )
		{
			bee_user_t *bu;
			
			new = g_strdup( message );
			new[s-message] = '\0';
			if( ( bu = bee_user_by_handle( c->ic->bee, c->ic, new ) ) )
			{
				sprintf( new, "@%s", bu->handle );
				new[s-message+1] = ' ';
				message = new;
			}
		}
		
		twitter_post_status( c->ic, message );
		g_free( new );
	}
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

