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
}

static void jabber_close( struct gaim_connection *gc )
{
}

static int jabber_send_im( struct gaim_connection *gc, char *who, char *message, int len, int away )
{
}

void jabber_init()
{
	struct prpl *ret = g_new0(struct prpl, 1);
	
	ret->name = "jabber";
	ret->login = jabber_login;
	ret->acc_init = jabber_acc_init;
	ret->close = jabber_close;
	ret->send_im = jabber_send_im;
//	ret->away_states = jabber_away_states;
//	ret->get_status_string = jabber_get_status_string;
//	ret->set_away = jabber_set_away;
//	ret->set_info = jabber_set_info;
//	ret->get_info = jabber_get_info;
//	ret->add_buddy = jabber_add_buddy;
//	ret->remove_buddy = jabber_remove_buddy;
//	ret->chat_send = jabber_chat_send;
//	ret->chat_invite = jabber_chat_invite;
//	ret->chat_leave = jabber_chat_leave;
//	ret->chat_open = jabber_chat_open;
//	ret->keepalive = jabber_keepalive;
//	ret->add_permit = jabber_add_permit;
//	ret->rem_permit = jabber_rem_permit;
//	ret->add_deny = jabber_add_deny;
//	ret->rem_deny = jabber_rem_deny;
//	ret->send_typing = jabber_send_typing;
	ret->handle_cmp = g_strcasecmp;

	register_protocol(ret);
}

static xt_status jabber_end_of_stream( struct xt_node *node, gpointer data )
{
	return XT_ABORT;
}

static xt_status jabber_pkt_misc( struct xt_node *node, gpointer data )
{
	printf( "Received unknown packet:\n" );
	xt_print( node );
	
	return XT_HANDLED;
}

static const struct xt_handler_entry jabber_handlers[] = {
	{ "stream:stream",      "<root>",               jabber_end_of_stream },
	{ "iq",                 "stream:stream",        jabber_pkt_iq },
	{ "message",            "stream:stream",        jabber_pkt_message },
	{ "presence",           "stream:stream",        jabber_pkt_presence },
	{ NULL,                 "stream:stream",        jabber_pkt_misc },
	{ NULL,                 NULL,                   NULL }
};

#if 0
int main( int argc, char *argv[] )
{
	struct xt_parser *xt = xt_new( NULL );
	struct xt_node *msg;
	int i;
	char buf[512];
	
	msg = xt_new_node( "message", NULL, xt_new_node( "body", "blaataap-test", NULL ) );
	xt_add_child( msg, xt_new_node( "html", NULL, xt_new_node( "body", "<b>blaataap in html</b>", NULL ) ) );
	xt_add_attr( msg, "xmlns", "jabber:client" );
	xt_add_attr( xt_find_node( msg->children, "html" ), "xmlns", "html rotte zooi" );
	printf( "%s\n", xt_to_string( msg ) );
	
	while( ( i = read( 0, buf, 512 ) ) > 0 )
	{
		if( xt_feed( xt, buf, i ) < 1 )
			break;
	}
	xt->handlers = jabber_handlers;
	xt_handle( xt, NULL );
	
	xt_cleanup( xt, NULL );
	printf( "%d\n", xt->root );
	
	xt_free( xt );
}
#endif