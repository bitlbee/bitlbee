/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Jabber module - IQ packets                                               *
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

#include "jabber.h"

xt_status jabber_pkt_iq( struct xt_node *node, gpointer data )
{
	struct gaim_connection *gc = data;
	struct jabber_data *jd = gc->proto_data;
	struct xt_node *query, *reply = NULL;
	char *s, *type, *xmlns;
	int st;
	
	query = xt_find_node( node->children, "query" );
	type = xt_find_attr( node, "type" );
	
	if( !type )
		return XT_HANDLED;	/* Ignore it for now, don't know what's best... */
	
	xmlns = xt_find_attr( query, "xmlns" );
	
	if( strcmp( type, "result" ) == 0 && xmlns && strcmp( xmlns, "jabber:iq:auth" ) == 0 )
	{
		/* Time to authenticate ourselves! */
		reply = xt_new_node( "query", NULL, NULL );
		xt_add_attr( reply, "xmlns", "jabber:iq:auth" );
		xt_add_child( reply, xt_new_node( "username", jd->username, NULL ) );
		xt_add_child( reply, xt_new_node( "resource", set_getstr( &gc->acc->set, "resource" ), NULL ) );
		
		if( xt_find_node( query->children, "digest" ) && ( s = xt_find_attr( jd->xt->root, "id" ) ) )
		{
			/* We can do digest authentication, it seems, and of
			   course we prefer that. */
			SHA_CTX sha;
			char hash_hex[40];
			unsigned char hash[20];
			int i;
			
			shaInit( &sha );
			shaUpdate( &sha, (unsigned char*) s, strlen( s ) );
			shaUpdate( &sha, (unsigned char*) gc->acc->pass, strlen( gc->acc->pass ) );
			shaFinal( &sha, hash );
			
			for( i = 0; i < 20; i ++ )
				sprintf( hash_hex + i * 2, "%02x", hash[i] );
			
			xt_add_child( reply, xt_new_node( "digest", hash_hex, NULL ) );
		}
		else if( xt_find_node( query->children, "password" ) )
		{
			/* We'll have to stick with plaintext. Let's hope we're using SSL/TLS... */
			xt_add_child( reply, xt_new_node( "password", gc->acc->pass, NULL ) );
		}
		else
		{
			xt_free_node( reply );
			
			hide_login_progress( gc, "Can't find suitable authentication method" );
			signoff( gc );
			return XT_ABORT;
		}
		
		reply = jabber_make_packet( "iq", "set", NULL, reply );
		st = jabber_write_packet( gc, reply );
		xt_free_node( reply );
		
		return st ? XT_HANDLED : XT_ABORT;
	}
	if( strcmp( type, "result" ) == 0 && xmlns && strcmp( xmlns, "jabber:iq:roster" ) == 0 )
	{
		struct xt_node *node;
		
		node = query->children;
		while( ( node = xt_find_node( node, "item" ) ) )
		{
			char *jid = xt_find_attr( node, "jid" );
			char *name = xt_find_attr( node, "name" );
			char *sub = xt_find_attr( node, "subscription" );
			
			if( jid && sub && ( strcmp( sub, "both" ) == 0 || strcmp( sub, "to" ) == 0 ) )
				add_buddy( gc, NULL, jid, name );
			
			node = node->next;
		}
		
		presence_announce( gc );
	}
	else if( strcmp( type, "result" ) == 0 )
	{
		/* If we weren't authenticated yet, let's assume we are now.
		   There are cleaner ways to do this, probably, but well.. */
		if( !( jd->flags & JFLAG_AUTHENTICATED ) )
		{
			jd->flags |= JFLAG_AUTHENTICATED;
			if( !jabber_get_roster( gc ) )
				return XT_ABORT;
		}
	}
	else if( strcmp( type, "error" ) == 0 )
	{
		if( !( jd->flags & JFLAG_AUTHENTICATED ) )
		{
			hide_login_progress( gc, "Authentication failure" );
			signoff( gc );
			return XT_ABORT;
		}
	}
	
	return XT_HANDLED;
}

int jabber_start_auth( struct gaim_connection *gc )
{
	struct jabber_data *jd = gc->proto_data;
	struct xt_node *node;
	int st;
	
	node = xt_new_node( "query", NULL, xt_new_node( "username", jd->username, NULL ) );
	xt_add_attr( node, "xmlns", "jabber:iq:auth" );
	node = jabber_make_packet( "iq", "get", NULL, node );
	
	st = jabber_write_packet( gc, node );
	
	xt_free_node( node );
	return st;
}

int jabber_get_roster( struct gaim_connection *gc )
{
	struct xt_node *node;
	int st;
	
	set_login_progress( gc, 1, "Authenticated, requesting buddy list" );
	
	node = xt_new_node( "query", NULL, NULL );
	xt_add_attr( node, "xmlns", "jabber:iq:roster" );
	node = jabber_make_packet( "iq", "get", NULL, node );
	
	st = jabber_write_packet( gc, node );
	
	xt_free_node( node );
	return st;
}
