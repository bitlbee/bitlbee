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
	
	s = set_add( &acc->set, "priority", "0", set_eval_resprio, acc );
	
	s = set_add( &acc->set, "resource", "BitlBee", set_eval_resprio, acc );
	
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
	
	if( set_getbool( &acc->set, "ssl" ) )
	{
		signoff( gc );
		/* TODO! */
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
	
	xt_free( jd->xt );
	
	g_free( jd->username );
	g_free( jd );
}

static int jabber_send_im( struct gaim_connection *gc, char *who, char *message, int len, int away )
{
	struct xt_node *node;
	int st;
	
	node = xt_new_node( "body", message, NULL );
	node = jabber_make_packet( "message", "chat", who, node );
	st = jabber_write_packet( gc, node );
	xt_free_node( node );
	
	return st;
}

/* TODO: For away state handling, implement some list like the one for MSN. */
static GList *jabber_away_states( struct gaim_connection *gc )
{
	GList *l = NULL;
	
	l = g_list_append( l, (void*) "Online" );
	l = g_list_append( l, (void*) "Away" );
	l = g_list_append( l, (void*) "Extended Away" );
	l = g_list_append( l, (void*) "Do Not Disturb" );
	
	return( l );
}

static void jabber_set_away( struct gaim_connection *gc, char *state, char *message )
{
	/* For now let's just always set state to "away" and send the message, if available. */
	presence_send( gc, NULL, g_strcasecmp( state, "Online" ) == 0 ? NULL : "away", message );
}

static void jabber_keepalive( struct gaim_connection *gc )
{
	/* Just any whitespace character is enough as a keepalive for XMPP sessions. */
	jabber_write( gc, "\n", 1 );
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
//	ret->add_buddy = jabber_add_buddy;
//	ret->remove_buddy = jabber_remove_buddy;
//	ret->chat_send = jabber_chat_send;
//	ret->chat_invite = jabber_chat_invite;
//	ret->chat_leave = jabber_chat_leave;
//	ret->chat_open = jabber_chat_open;
	ret->keepalive = jabber_keepalive;
//	ret->add_permit = jabber_add_permit;
//	ret->rem_permit = jabber_rem_permit;
//	ret->add_deny = jabber_add_deny;
//	ret->rem_deny = jabber_rem_deny;
//	ret->send_typing = jabber_send_typing;
	ret->handle_cmp = g_strcasecmp;

	register_protocol( ret );
}
