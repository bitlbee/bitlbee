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

static void jabber_acc_init( account_t *acc )
{
	set_t *s;
	
	s = set_add( &acc->set, "port", "5222", set_eval_int, acc );
	s->flags |= ACC_SET_OFFLINE_ONLY;
	
	s = set_add( &acc->set, "priority", "0", set_eval_priority, acc );
	
	s = set_add( &acc->set, "resource", "BitlBee", NULL, acc );
	s->flags |= ACC_SET_OFFLINE_ONLY;
	
	s = set_add( &acc->set, "server", NULL, set_eval_account, acc );
	s->flags |= ACC_SET_NOSAVE | ACC_SET_OFFLINE_ONLY;
	
	s = set_add( &acc->set, "ssl", "false", set_eval_bool, acc );
	s->flags |= ACC_SET_OFFLINE_ONLY;
	
	s = set_add( &acc->set, "tls", "try", set_eval_tls, acc );
	s->flags |= ACC_SET_OFFLINE_ONLY;
}

static void jabber_login( account_t *acc )
{
	struct gaim_connection *gc = new_gaim_conn( acc );
	struct jabber_data *jd = g_new0( struct jabber_data, 1 );
	
	jd->gc = gc;
	gc->proto_data = jd;
	
	jd->username = g_strdup( acc->user );
	jd->server = strchr( jd->username, '@' );
	
	if( jd->server == NULL )
	{
		hide_login_progress( gc, "Incomplete account name (format it like <username@jabberserver.name>)" );
		signoff( gc );
		return;
	}
	
	/* So don't think of free()ing jd->server.. :-) */
	*jd->server = 0;
	jd->server ++;
	
	jd->node_cache = xt_new_node( "cache", NULL, NULL );
	
	if( set_getbool( &acc->set, "ssl" ) )
	{
		jd->ssl = ssl_connect( jd->server, set_getint( &acc->set, "port" ), jabber_connected_ssl, gc );
		jd->fd = ssl_getfd( jd->ssl );
	}
	else
	{
		jd->fd = proxy_connect( jd->server, set_getint( &acc->set, "port" ), jabber_connected_plain, gc );
	}
}

static void jabber_close( struct gaim_connection *gc )
{
	struct jabber_data *jd = gc->proto_data;
	
	jabber_end_stream( gc );
	
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
	
	xt_free_node( jd->node_cache );
	xt_free( jd->xt );
	
	g_free( jd->away_message );
	g_free( jd->username );
	g_free( jd );
}

static int jabber_send_im( struct gaim_connection *gc, char *who, char *message, int len, int away )
{
	struct xt_node *node, *event;
	int st;
	
	/*
	event = xt_new_node( "active", NULL, NULL );
	xt_add_attr( event, "xlmns", "http://jabber.org/protocol/chatstates" );
	
	event = xt_new_node( "x", NULL, xt_new_node( "composing", NULL, NULL ) );
	xt_add_attr( event, "xmlns", "jabber:x:event" );
	*/
	
	node = xt_new_node( "body", message, NULL );
	node = jabber_make_packet( "message", "chat", who, node );
	xt_add_child( node, event );
	st = jabber_write_packet( gc, node );
	xt_free_node( node );
	
	return st;
}

static GList *jabber_away_states( struct gaim_connection *gc )
{
	static GList *l = NULL;
	int i;
	
	if( l == NULL )
		for( i = 0; jabber_away_state_list[i].full_name; i ++ )
			l = g_list_append( l, (void*) jabber_away_state_list[i].full_name );
	
	return l;
}

static void jabber_set_away( struct gaim_connection *gc, char *state_txt, char *message )
{
	struct jabber_data *jd = gc->proto_data;
	struct jabber_away_state *state;
	
	/* Save all this info. We need it, for example, when changing the priority setting. */
	state = (void *) jabber_away_state_by_name( state_txt );
	jd->away_state = state ? state : (void *) jabber_away_state_list; /* Fall back to "Away" if necessary. */
	g_free( jd->away_message );
	jd->away_message = ( message && *message ) ? g_strdup( message ) : NULL;
	
	presence_send_update( gc );
}

static void jabber_add_buddy( struct gaim_connection *gc, char *who )
{
	if( jabber_add_to_roster( gc, who, NULL ) )
		presence_send_request( gc, who, "subscribe" );
}

static void jabber_remove_buddy( struct gaim_connection *gc, char *who, char *group )
{
	if( jabber_remove_from_roster( gc, who ) )
		presence_send_request( gc, who, "unsubscribe" );
}

static void jabber_keepalive( struct gaim_connection *gc )
{
	struct jabber_data *jd = gc->proto_data;
	struct xt_node *c, *tmp;
	
	/* Just any whitespace character is enough as a keepalive for XMPP sessions. */
	jabber_write( gc, "\n", 1 );
	
	/* Let's abuse this keepalive for garbage collection of the node cache too.
	   It runs every minute, so let's mark every node with a special flag the
	   first time we see it, and clean it up the second time (clean up all
	   packets with the flag set).
	   
	   node->flags is normally only used by xmltree itself for parsing/handling,
	   so it should be safe to use the variable for gc. */
	
	/* This horrible loop is explained in xmltree.c. Makes me wonder if maybe I
	   didn't choose the perfect data structure... */
	for( c = jd->node_cache->children; c; c =  c->next )
		if( !( c->flags & XT_SEEN ) )
			break;
	
	/* Now c points at the first unflagged node (or at NULL). Clean up
	   everything until that point. */
	while( jd->node_cache->children != c )
	{
		/*
		printf( "Cleaning up:\n" );
		xt_print( jd->node_cache->children );
		*/
		
		tmp = jd->node_cache->children->next;
		xt_free_node( jd->node_cache->children );
		jd->node_cache->children = tmp;
	}
	
	/* Now flag the ones that were still unflagged. */
	for( c = jd->node_cache->children; c; c = c->next )
	{
		/*
		printf( "Flagged:\n" );
		xt_print( c );
		*/
		
		c->flags |= XT_SEEN;
	}
}

static void jabber_add_permit( struct gaim_connection *gc, char *who )
{
	presence_send_request( gc, who, "subscribed" );
}

static void jabber_rem_permit( struct gaim_connection *gc, char *who )
{
	presence_send_request( gc, who, "unsubscribed" );
}

/* XMPP doesn't have both a block- and and allow-list, so these two functions
   will be no-ops: */
static void jabber_add_deny( struct gaim_connection *gc, char *who )
{
}

static void jabber_rem_deny( struct gaim_connection *gc, char *who )
{
}

void jabber_init()
{
	struct prpl *ret = g_new0( struct prpl, 1 );
	
	ret->name = "jabber";
	ret->login = jabber_login;
	ret->acc_init = jabber_acc_init;
	ret->close = jabber_close;
	ret->send_im = jabber_send_im;
	ret->away_states = jabber_away_states;
//	ret->get_status_string = jabber_get_status_string;
	ret->set_away = jabber_set_away;
//	ret->set_info = jabber_set_info;
//	ret->get_info = jabber_get_info;
	ret->add_buddy = jabber_add_buddy;
	ret->remove_buddy = jabber_remove_buddy;
//	ret->chat_send = jabber_chat_send;
//	ret->chat_invite = jabber_chat_invite;
//	ret->chat_leave = jabber_chat_leave;
//	ret->chat_open = jabber_chat_open;
	ret->keepalive = jabber_keepalive;
	ret->add_permit = jabber_add_permit;
	ret->rem_permit = jabber_rem_permit;
	ret->add_deny = jabber_add_deny;
	ret->rem_deny = jabber_rem_deny;
//	ret->send_typing = jabber_send_typing;
	ret->handle_cmp = g_strcasecmp;

	register_protocol( ret );
}
