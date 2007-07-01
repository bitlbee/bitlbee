/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Jabber module - Main file                                                *
*                                                                           *
*  Copyright 2006 Wilmer van der Gaast <wilmer@gaast.net>                   *
*                                                                           *
*  This program is free software; you can redistribute it and/or modify     *
*  it under the terms of the GNU General Public License as published by     *
*  the Free Software Foundation; either version 2 of the License, or        *
*  (at your option) any later version.                                      *
*                                                                           *
*  This program is distributed in the hope that it will be useful,          *
*  but WITHOUT ANY WARRANTY; without even the implied warranty of           *
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
*  GNU General Public License for more details.                             *
*                                                                           *
*  You should have received a copy of the GNU General Public License along  *
*  with this program; if not, write to the Free Software Foundation, Inc.,  *
*  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.              *
*                                                                           *
\***************************************************************************/

#include <glib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <stdio.h>

#include "ssl_client.h"
#include "xmltree.h"
#include "bitlbee.h"
#include "jabber.h"

static void jabber_init( account_t *acc )
{
	set_t *s;
	
	s = set_add( &acc->set, "port", JABBER_PORT_DEFAULT, set_eval_int, acc );
	s->flags |= ACC_SET_OFFLINE_ONLY;
	
	s = set_add( &acc->set, "priority", "0", set_eval_priority, acc );
	
	s = set_add( &acc->set, "resource", "BitlBee", NULL, acc );
	s->flags |= ACC_SET_OFFLINE_ONLY;
	
	s = set_add( &acc->set, "resource_select", "priority", NULL, acc );
	
	s = set_add( &acc->set, "server", NULL, set_eval_account, acc );
	s->flags |= ACC_SET_NOSAVE | ACC_SET_OFFLINE_ONLY;
	
	s = set_add( &acc->set, "ssl", "false", set_eval_bool, acc );
	s->flags |= ACC_SET_OFFLINE_ONLY;
	
	s = set_add( &acc->set, "tls", "try", set_eval_tls, acc );
	s->flags |= ACC_SET_OFFLINE_ONLY;
}

static void jabber_login( account_t *acc )
{
	struct im_connection *ic = imcb_new( acc );
	struct jabber_data *jd = g_new0( struct jabber_data, 1 );
	struct ns_srv_reply *srv = NULL;
	char *connect_to, *s;
	
	jd->ic = ic;
	ic->proto_data = jd;
	
	jd->username = g_strdup( acc->user );
	jd->server = strchr( jd->username, '@' );
	
	if( jd->server == NULL )
	{
		imcb_error( ic, "Incomplete account name (format it like <username@jabberserver.name>)" );
		imc_logout( ic, FALSE );
		return;
	}
	
	/* So don't think of free()ing jd->server.. :-) */
	*jd->server = 0;
	jd->server ++;
	
	if( ( s = strchr( jd->server, '/' ) ) )
	{
		*s = 0;
		set_setstr( &acc->set, "resource", s + 1 );
		
		/* Also remove the /resource from the original variable so we
		   won't have to do this again every time. */
		s = strchr( acc->user, '/' );
		*s = 0;
	}
	
	/* This code isn't really pretty. Backwards compatibility never is... */
	s = acc->server;
	while( s )
	{
		static int had_port = 0;
		
		if( strncmp( s, "ssl", 3 ) == 0 )
		{
			set_setstr( &acc->set, "ssl", "true" );
			
			/* Flush this part so that (if this was the first
			   part of the server string) acc->server gets
			   flushed. We don't want to have to do this another
			   time. :-) */
			*s = 0;
			s ++;
			
			/* Only set this if the user didn't specify a custom
			   port number already... */
			if( !had_port )
				set_setint( &acc->set, "port", 5223 );
		}
		else if( isdigit( *s ) )
		{
			int i;
			
			/* The first character is a digit. It could be an
			   IP address though. Only accept this as a port#
			   if there are only digits. */
			for( i = 0; isdigit( s[i] ); i ++ );
			
			/* If the first non-digit character is a colon or
			   the end of the string, save the port number
			   where it should be. */
			if( s[i] == ':' || s[i] == 0 )
			{
				sscanf( s, "%d", &i );
				set_setint( &acc->set, "port", i );
				
				/* See above. */
				*s = 0;
				s ++;
			}
			
			had_port = 1;
		}
		
		s = strchr( s, ':' );
		if( s )
		{
			*s = 0;
			s ++;
		}
	}
	
	jd->node_cache = g_hash_table_new_full( g_str_hash, g_str_equal, NULL, jabber_cache_entry_free );
	jd->buddies = g_hash_table_new( g_str_hash, g_str_equal );
	
	/* Figure out the hostname to connect to. */
	if( acc->server && *acc->server )
		connect_to = acc->server;
	else if( ( srv = srv_lookup( "xmpp-client", "tcp", jd->server ) ) ||
		 ( srv = srv_lookup( "jabber-client", "tcp", jd->server ) ) )
		connect_to = srv->name;
	else
		connect_to = jd->server;
	
	imcb_log( ic, "Connecting" );
	
	if( set_getint( &acc->set, "port" ) < JABBER_PORT_MIN ||
	    set_getint( &acc->set, "port" ) > JABBER_PORT_MAX )
	{
		imcb_log( ic, "Incorrect port number, must be in the %d-%d range",
		               JABBER_PORT_MIN, JABBER_PORT_MAX );
		imc_logout( ic, FALSE );
		return;
	}
	
	/* For non-SSL connections we can try to use the port # from the SRV
	   reply, but let's not do that when using SSL, SSL usually runs on
	   non-standard ports... */
	if( set_getbool( &acc->set, "ssl" ) )
	{
		jd->ssl = ssl_connect( connect_to, set_getint( &acc->set, "port" ), jabber_connected_ssl, ic );
		jd->fd = jd->ssl ? ssl_getfd( jd->ssl ) : -1;
	}
	else
	{
		jd->fd = proxy_connect( connect_to, srv ? srv->port : set_getint( &acc->set, "port" ), jabber_connected_plain, ic );
	}
	g_free( srv );
	
	if( jd->fd == -1 )
	{
		imcb_error( ic, "Could not connect to server" );
		imc_logout( ic, TRUE );
	}
}

static void jabber_logout( struct im_connection *ic )
{
	struct jabber_data *jd = ic->proto_data;
	
	jabber_end_stream( ic );
	
	if( jd->r_inpa >= 0 )
		b_event_remove( jd->r_inpa );
	if( jd->w_inpa >= 0 )
		b_event_remove( jd->w_inpa );
	
	if( jd->ssl )
		ssl_disconnect( jd->ssl );
	if( jd->fd >= 0 )
		closesocket( jd->fd );
	
	if( jd->tx_len )
		g_free( jd->txq );
	
	g_hash_table_destroy( jd->node_cache );
	
	xt_free( jd->xt );
	
	g_free( jd->away_message );
	g_free( jd->username );
	g_free( jd );
}

static int jabber_buddy_msg( struct im_connection *ic, char *who, char *message, int flags )
{
	struct jabber_data *jd = ic->proto_data;
	struct jabber_buddy *bud;
	struct xt_node *node;
	char *s;
	int st;
	
	if( g_strcasecmp( who, JABBER_XMLCONSOLE_HANDLE ) == 0 )
		return jabber_write( ic, message, strlen( message ) );
	
	if( ( s = strchr( who, '=' ) ) && jabber_chat_by_name( ic, s + 1 ) )
		bud = jabber_buddy_by_ext_jid( ic, who, 0 );
	else
		bud = jabber_buddy_by_jid( ic, who, 0 );
	
	node = xt_new_node( "body", message, NULL );
	node = jabber_make_packet( "message", "chat", bud ? bud->full_jid : who, node );
	
	if( bud && ( jd->flags & JFLAG_WANT_TYPING ) &&
	    ( ( bud->flags & JBFLAG_DOES_XEP85 ) ||
	     !( bud->flags & JBFLAG_PROBED_XEP85 ) ) )
	{
		struct xt_node *act;
		
		/* If the user likes typing notification and if we don't know
		   (and didn't probe before) if this resource supports XEP85,
		   include a probe in this packet now. Also, if we know this
		   buddy does support XEP85, we have to send this <active/>
		   tag to tell that the user stopped typing (well, that's what
		   we guess when s/he pressed Enter...). */
		act = xt_new_node( "active", NULL, NULL );
		xt_add_attr( act, "xmlns", XMLNS_CHATSTATES );
		xt_add_child( node, act );
		
		/* Just make sure we do this only once. */
		bud->flags |= JBFLAG_PROBED_XEP85;
	}
	
	st = jabber_write_packet( ic, node );
	xt_free_node( node );
	
	return st;
}

static GList *jabber_away_states( struct im_connection *ic )
{
	static GList *l = NULL;
	int i;
	
	if( l == NULL )
		for( i = 0; jabber_away_state_list[i].full_name; i ++ )
			l = g_list_append( l, (void*) jabber_away_state_list[i].full_name );
	
	return l;
}

static void jabber_get_info( struct im_connection *ic, char *who )
{
	struct jabber_data *jd = ic->proto_data;
	struct jabber_buddy *bud;
	
	if( strchr( who, '/' ) )
		bud = jabber_buddy_by_jid( ic, who, 0 );
	else
	{
		char *s = jabber_normalize( who );
		bud = g_hash_table_lookup( jd->buddies, s );
		g_free( s );
	}
	
	while( bud )
	{
		imcb_log( ic, "Buddy %s (%d) information:\nAway state: %s\nAway message: %s",
		                   bud->full_jid, bud->priority,
		                   bud->away_state ? bud->away_state->full_name : "(none)",
		                   bud->away_message ? : "(none)" );
		bud = bud->next;
	}
	
	jabber_get_vcard( ic, bud ? bud->full_jid : who );
}

static void jabber_set_away( struct im_connection *ic, char *state_txt, char *message )
{
	struct jabber_data *jd = ic->proto_data;
	struct jabber_away_state *state;
	
	/* Save all this info. We need it, for example, when changing the priority setting. */
	state = (void *) jabber_away_state_by_name( state_txt );
	jd->away_state = state ? state : (void *) jabber_away_state_list; /* Fall back to "Away" if necessary. */
	g_free( jd->away_message );
	jd->away_message = ( message && *message ) ? g_strdup( message ) : NULL;
	
	presence_send_update( ic );
}

static void jabber_add_buddy( struct im_connection *ic, char *who, char *group )
{
	struct jabber_data *jd = ic->proto_data;
	
	if( g_strcasecmp( who, JABBER_XMLCONSOLE_HANDLE ) == 0 )
	{
		jd->flags |= JFLAG_XMLCONSOLE;
		imcb_add_buddy( ic, JABBER_XMLCONSOLE_HANDLE, NULL );
		return;
	}
	
	if( jabber_add_to_roster( ic, who, NULL ) )
		presence_send_request( ic, who, "subscribe" );
}

static void jabber_remove_buddy( struct im_connection *ic, char *who, char *group )
{
	struct jabber_data *jd = ic->proto_data;
	
	if( g_strcasecmp( who, JABBER_XMLCONSOLE_HANDLE ) == 0 )
	{
		jd->flags &= ~JFLAG_XMLCONSOLE;
		/* Not necessary for now. And for now the code isn't too
		   happy if the buddy is completely gone right after calling
		   this function already.
		imcb_remove_buddy( ic, JABBER_XMLCONSOLE_HANDLE, NULL );
		*/
		return;
	}
	
	/* We should always do this part. Clean up our administration a little bit. */
	jabber_buddy_remove_bare( ic, who );
	
	if( jabber_remove_from_roster( ic, who ) )
		presence_send_request( ic, who, "unsubscribe" );
}

static struct groupchat *jabber_chat_join_( struct im_connection *ic, char *room, char *nick, char *password )
{
	if( strchr( room, '@' ) == NULL )
		imcb_error( ic, "Invalid room name: %s", room );
	else if( jabber_chat_by_name( ic, room ) )
		imcb_error( ic, "Already present in chat `%s'", room );
	else
		return jabber_chat_join( ic, room, nick, password );
	
	return NULL;
}

static void jabber_chat_msg_( struct groupchat *c, char *message, int flags )
{
	if( c && message )
		jabber_chat_msg( c, message, flags );
}

static void jabber_chat_leave_( struct groupchat *c )
{
	if( c )
		jabber_chat_leave( c, NULL );
}

static void jabber_keepalive( struct im_connection *ic )
{
	/* Just any whitespace character is enough as a keepalive for XMPP sessions. */
	jabber_write( ic, "\n", 1 );
	
	/* This runs the garbage collection every minute, which means every packet
	   is in the cache for about a minute (which should be enough AFAIK). */
	jabber_cache_clean( ic );
}

static int jabber_send_typing( struct im_connection *ic, char *who, int typing )
{
	struct jabber_data *jd = ic->proto_data;
	struct jabber_buddy *bud;
	
	/* Enable typing notification related code from now. */
	jd->flags |= JFLAG_WANT_TYPING;
	
	if( ( bud = jabber_buddy_by_jid( ic, who, 0 ) ) == NULL )
	{
		/* Sending typing notifications to unknown buddies is
		   unsupported for now. Shouldn't be a problem, I think. */
		return 0;
	}
	
	if( bud->flags & JBFLAG_DOES_XEP85 )
	{
		/* We're only allowed to send this stuff if we know the other
		   side supports it. */
		
		struct xt_node *node;
		char *type;
		int st;
		
		if( typing & OPT_TYPING )
			type = "composing";
		else if( typing & OPT_THINKING )
			type = "paused";
		else
			type = "active";
		
		node = xt_new_node( type, NULL, NULL );
		xt_add_attr( node, "xmlns", XMLNS_CHATSTATES );
		node = jabber_make_packet( "message", "chat", bud->full_jid, node );
		
		st = jabber_write_packet( ic, node );
		xt_free_node( node );
		
		return st;
	}
	
	return 1;
}

void jabber_initmodule()
{
	struct prpl *ret = g_new0( struct prpl, 1 );
	
	ret->name = "jabber";
	ret->login = jabber_login;
	ret->init = jabber_init;
	ret->logout = jabber_logout;
	ret->buddy_msg = jabber_buddy_msg;
	ret->away_states = jabber_away_states;
	ret->set_away = jabber_set_away;
//	ret->set_info = jabber_set_info;
	ret->get_info = jabber_get_info;
	ret->add_buddy = jabber_add_buddy;
	ret->remove_buddy = jabber_remove_buddy;
	ret->chat_msg = jabber_chat_msg_;
//	ret->chat_invite = jabber_chat_invite;
	ret->chat_leave = jabber_chat_leave_;
	ret->chat_join = jabber_chat_join_;
	ret->keepalive = jabber_keepalive;
	ret->send_typing = jabber_send_typing;
	ret->handle_cmp = g_strcasecmp;

	register_protocol( ret );
}
