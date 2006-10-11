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
	struct xt_node *c, *reply = NULL;
	char *type, *s;
	int st;
	
	type = xt_find_attr( node, "type" );
	
	if( !type )
	{
		hide_login_progress_error( gc, "Received IQ packet without type." );
		signoff( gc );
		return XT_ABORT;
	}
	
	if( strcmp( type, "result" ) == 0 || strcmp( type, "error" ) == 0 )
	{
		struct jabber_cache_entry *entry;
		
		if( ( s = xt_find_attr( node, "id" ) ) == NULL )
		{
			/* Silently ignore it, without an ID we don't know
			   how to handle the packet, but it doesn't have
			   to be a serious problem. */
			return XT_HANDLED;
		}
		
		entry = g_hash_table_lookup( jd->node_cache, s );
		
		if( entry == NULL )
			serv_got_crap( gc, "WARNING: Received IQ %s packet with unknown/expired ID %s!", type, s );
		else if( entry->func )
			return entry->func( gc, node, entry->node );
	}
	else if( strcmp( type, "get" ) == 0 )
	{
		if( !( c = xt_find_node( node->children, "query" ) ) ||
		    !( s = xt_find_attr( c, "xmlns" ) ) )
		{
			serv_got_crap( gc, "WARNING: Received incomplete IQ-get packet" );
			return XT_HANDLED;
		}
		
		reply = xt_new_node( "query", NULL, NULL );
		xt_add_attr( reply, "xmlns", s );
		
		/* Of course this is a very essential query to support. ;-) */
		if( strcmp( s, "jabber:iq:version" ) == 0 )
		{
			xt_add_child( reply, xt_new_node( "name", "BitlBee", NULL ) );
			xt_add_child( reply, xt_new_node( "version", BITLBEE_VERSION, NULL ) );
			xt_add_child( reply, xt_new_node( "os", ARCH, NULL ) );
		}
		else if( strcmp( s, "http://jabber.org/protocol/disco#info" ) == 0 )
		{
			c = xt_new_node( "identity", NULL, NULL );
			xt_add_attr( c, "category", "client" );
			xt_add_attr( c, "type", "pc" );
			xt_add_attr( c, "name", "BitlBee" );
			xt_add_child( reply, c );
			
			c = xt_new_node( "feature", NULL, NULL );
			xt_add_attr( c, "var", "jabber:iq:version" );
			xt_add_child( reply, c );
			
			c = xt_new_node( "feature", NULL, NULL );
			xt_add_attr( c, "var", "http://jabber.org/protocol/chatstates" );
			xt_add_child( reply, c );
			
			/* Later this can be useful to announce things like
			   MUC support. */
		}
		else
		{
			xt_free_node( reply );
			reply = NULL;
		}
		
		/* If we recognized the xmlns and managed to generate a reply,
		   finish and send it. */
		if( reply )
		{
			reply = jabber_make_packet( "iq", "result", xt_find_attr( node, "from" ), reply );
			if( ( s = xt_find_attr( node, "id" ) ) )
				xt_add_attr( reply, "id", s );
			
			st = jabber_write_packet( gc, reply );
			xt_free_node( reply );
			if( !st )
				return XT_ABORT;
		}
	}
	
	return XT_HANDLED;
}

static xt_status jabber_do_iq_auth( struct gaim_connection *gc, struct xt_node *node, struct xt_node *orig );
static xt_status jabber_finish_iq_auth( struct gaim_connection *gc, struct xt_node *node, struct xt_node *orig );

int jabber_init_iq_auth( struct gaim_connection *gc )
{
	struct jabber_data *jd = gc->proto_data;
	struct xt_node *node;
	int st;
	
	node = xt_new_node( "query", NULL, xt_new_node( "username", jd->username, NULL ) );
	xt_add_attr( node, "xmlns", "jabber:iq:auth" );
	node = jabber_make_packet( "iq", "get", NULL, node );
	
	jabber_cache_add( gc, node, jabber_do_iq_auth );
	st = jabber_write_packet( gc, node );
	
	return st;
}

static xt_status jabber_do_iq_auth( struct gaim_connection *gc, struct xt_node *node, struct xt_node *orig )
{
	struct jabber_data *jd = gc->proto_data;
	struct xt_node *reply, *query;
	xt_status st;
	char *s;
	
	query = xt_find_node( node->children, "query" );
	
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
	jabber_cache_add( gc, reply, jabber_finish_iq_auth );
	st = jabber_write_packet( gc, reply );
	
	return st ? XT_HANDLED : XT_ABORT;
}

static xt_status jabber_finish_iq_auth( struct gaim_connection *gc, struct xt_node *node, struct xt_node *orig )
{
	char *type = xt_find_attr( node, "type" );
	struct jabber_data *jd = gc->proto_data;
	
	if( strcmp( type, "error" ) == 0 )
	{
		hide_login_progress( gc, "Authentication failure" );
		signoff( gc );
		return XT_ABORT;
	}
	else if( strcmp( type, "result" ) == 0 )
	{
		/* This happens when we just successfully authenticated the
		   old (non-SASL) way. */
		jd->flags |= JFLAG_AUTHENTICATED;
		if( !jabber_get_roster( gc ) )
			return XT_ABORT;
	}
	
	return XT_HANDLED;
}

xt_status jabber_pkt_bind_sess( struct gaim_connection *gc, struct xt_node *node, struct xt_node *orig )
{
	struct jabber_data *jd = gc->proto_data;
	struct xt_node *c;
	char *s;
	
	if( ( c = xt_find_node( node->children, "bind" ) ) )
	{
		c = xt_find_node( c->children, "jid" );
		if( c && c->text_len && ( s = strchr( c->text, '/' ) ) &&
		    strcmp( s + 1, set_getstr( &gc->acc->set, "resource" ) ) != 0 )
			serv_got_crap( gc, "Server changed session resource string to `%s'", s + 1 );
		
		jd->flags &= ~JFLAG_WAIT_BIND;
	}
	else
	{
		jd->flags &= ~JFLAG_WAIT_SESSION;
	}
	
	if( ( jd->flags & ( JFLAG_WAIT_BIND | JFLAG_WAIT_SESSION ) ) == 0 )
	{
		if( !jabber_get_roster( gc ) )
			return XT_ABORT;
	}
	
	return XT_HANDLED;
}

static xt_status jabber_parse_roster( struct gaim_connection *gc, struct xt_node *node, struct xt_node *orig );

int jabber_get_roster( struct gaim_connection *gc )
{
	struct xt_node *node;
	int st;
	
	set_login_progress( gc, 1, "Authenticated, requesting buddy list" );
	
	node = xt_new_node( "query", NULL, NULL );
	xt_add_attr( node, "xmlns", "jabber:iq:roster" );
	node = jabber_make_packet( "iq", "get", NULL, node );
	
	jabber_cache_add( gc, node, jabber_parse_roster );
	st = jabber_write_packet( gc, node );
	
	return st;
}

static xt_status jabber_parse_roster( struct gaim_connection *gc, struct xt_node *node, struct xt_node *orig )
{
	struct xt_node *query, *c;
	
	query = xt_find_node( node->children, "query" );
	
	c = query->children;
	while( ( c = xt_find_node( c, "item" ) ) )
	{
		char *jid = xt_find_attr( c, "jid" );
		char *name = xt_find_attr( c, "name" );
		char *sub = xt_find_attr( c, "subscription" );
		
		if( jid && sub && ( strcmp( sub, "both" ) == 0 || strcmp( sub, "to" ) == 0 ) )
			add_buddy( gc, NULL, jid, name );
		
		c = c->next;
	}
	
	account_online( gc );
	
	return XT_HANDLED;
}

int jabber_add_to_roster( struct gaim_connection *gc, char *handle, char *name )
{
	struct xt_node *node;
	int st;
	
	/* Build the item entry */
	node = xt_new_node( "item", NULL, NULL );
	xt_add_attr( node, "jid", handle );
	if( name )
		xt_add_attr( node, "name", name );
	
	/* And pack it into a roster-add packet */
	node = xt_new_node( "query", NULL, node );
	xt_add_attr( node, "xmlns", "jabber:iq:roster" );
	node = jabber_make_packet( "iq", "set", NULL, node );
	
	st = jabber_write_packet( gc, node );
	
	xt_free_node( node );
	return st;
}

int jabber_remove_from_roster( struct gaim_connection *gc, char *handle )
{
	struct xt_node *node;
	int st;
	
	/* Build the item entry */
	node = xt_new_node( "item", NULL, NULL );
	xt_add_attr( node, "jid", handle );
	xt_add_attr( node, "subscription", "remove" );
	
	/* And pack it into a roster-add packet */
	node = xt_new_node( "query", NULL, node );
	xt_add_attr( node, "xmlns", "jabber:iq:roster" );
	node = jabber_make_packet( "iq", "set", NULL, node );
	
	st = jabber_write_packet( gc, node );
	
	xt_free_node( node );
	return st;
}
