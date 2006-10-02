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
	struct xt_node *query, *reply = NULL, *orig = NULL, *c;
	char *s, *type, *xmlns;
	int st;
	
	query = xt_find_node( node->children, "query" );
	type = xt_find_attr( node, "type" );
	
	if( !type )
		return XT_HANDLED;	/* Ignore it for now, don't know what's best... */
	
	xmlns = xt_find_attr( query, "xmlns" );
	
	if( ( s = xt_find_attr( node, "id" ) ) )
		orig = jabber_packet_from_cache( gc, s );
	
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
		jabber_cache_packet( gc, reply );
		st = jabber_write_packet( gc, reply );
		
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
		
		account_online( gc );
	}
	if( strcmp( type, "result" ) == 0 && xmlns && strcmp( xmlns, "jabber:iq:privacy" ) == 0 )
	{
		struct xt_node *node;
		
		/* When receiving a list of lists: */
		if( ( node = xt_find_node( query->children, "active" ) ) )
		{
			if( ( s = xt_find_attr( node, "name" ) ) )
			{
				set_t *set;
				
				g_free( jd->privacy_active );
				jd->privacy_active = g_strdup( s );
				
				/* Save it so the user can see it. */
				if( ( set = set_find( &gc->acc->set, "privacy_list" ) ) )
				{
					g_free( set->value );
					set->value = g_strdup( s );
				}
				
				if( !jabber_get_privacy( gc ) )
					return XT_ABORT;
			}
		}
		/* When receiving an actual list: */
		else if( ( node = xt_find_node( query->children, "list" ) ) )
		{
			xt_free_node( jd->privacy_list );
			jd->privacy_list = xt_dup( node );
		}
		else if( query->children == NULL )
		{
			/* What to do here if there is no privacy list defined yet... */
		}
	}
	else if( strcmp( type, "result" ) == 0 && orig )
	{
		struct xt_node *c;
		
		if( !( jd->flags & JFLAG_AUTHENTICATED ) &&
		    ( c = xt_find_node( orig->children, "query" ) ) &&
		    ( c = xt_find_node( c->children, "username" ) ) &&
		    c->text_len )
		{
			/* This happens when we just successfully authenticated
			   the old (non-SASL) way. */
			jd->flags |= JFLAG_AUTHENTICATED;
			if( !jabber_get_roster( gc ) || !jabber_get_privacy( gc ) )
				return XT_ABORT;
		}
		/* Tricky: Look for <bind> in the reply, because the server
		   should confirm the chosen resource string there. For
		   <session>, however, look in the cache, because the server
		   will probably not include it in its reply. */
		else if( ( c = xt_find_node( node->children, "bind" ) ) ||
		         ( c = xt_find_node( orig->children, "session" ) ) )
		{
			if( strcmp( c->name, "bind" ) == 0 )
			{
				c = xt_find_node( c->children, "jid" );
				if( c && c->text_len && ( s = strchr( c->text, '/' ) ) &&
				    strcmp( s + 1, set_getstr( &gc->acc->set, "resource" ) ) != 0 )
					serv_got_crap( gc, "Server changed session resource string to `%s'", s + 1 );
				
				jd->flags &= ~JFLAG_WAIT_BIND;
			}
			else if( strcmp( c->name, "session" ) == 0 )
				jd->flags &= ~JFLAG_WAIT_SESSION;
			
			if( ( jd->flags & ( JFLAG_WAIT_BIND | JFLAG_WAIT_SESSION ) ) == 0 )
			{
				if( !jabber_get_roster( gc ) || !jabber_get_privacy( gc ) )
					return XT_ABORT;
			}
		}
		else if( ( c = xt_find_node( orig->children, "query" ) ) &&
		         ( c = xt_find_node( c->children, "active" ) ) )
		{
			/* We just successfully activated a (different)
			   privacy list. Fetch it now. */
			g_free( jd->privacy_active );
			jd->privacy_active = g_strdup( xt_find_attr( c, "name" ) );
			
			if( !jabber_get_privacy( gc ) )
				return XT_ABORT;
		}
	}
	else if( strcmp( type, "error" ) == 0 )
	{
		if( !( jd->flags & JFLAG_AUTHENTICATED ) &&
		      orig &&
		    ( c = xt_find_node( orig->children, "query" ) ) &&
		    ( c = xt_find_node( c->children, "username" ) ) &&
		    c->text_len )
		{
			hide_login_progress( gc, "Authentication failure" );
			signoff( gc );
			return XT_ABORT;
		}
		else if( ( xmlns && strcmp( xmlns, "jabber:iq:privacy" ) == 0 ) ||
		         ( orig &&
		           ( c = xt_find_node( orig->children, "query" ) ) &&
		           ( s = xt_find_attr( c, "xmlns" ) ) &&
		           strcmp( s, "jabber:iq:privacy" ) == 0 ) )
		{
			/* All errors related to privacy lists. */
			if( ( c = xt_find_node( node->children, "error" ) ) == NULL )
			{
				hide_login_progress_error( gc, "Received malformed error packet" );
				signoff( gc );
				return XT_ABORT;
			}
			
			if( xt_find_node( c->children, "item-not-found" ) )
			{
				serv_got_crap( gc, "Error while activating privacy list, maybe it doesn't exist" );
				/* Should I do anything else here? */
			}
			else if( xt_find_node( c->children, "feature-not-implemented" ) )
			{
				jd->flags |= JFLAG_PRIVACY_BROKEN;
				/* Probably there's no need to inform the user.
				   We can do that if the user ever tries to use
				   the block/allow commands. */
			}
		}
	}
	
	return XT_HANDLED;
}

int jabber_start_iq_auth( struct gaim_connection *gc )
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

/* Request the privacy list from the server. We need this, because every
   time we remove/add something we have to send the whole new list to the
   server again... If no privacy list is specified yet, this function will
   first ask for the list of lists (XMPP supports multiple "privacy lists",
   don't ask me why), later we can then fetch the list we want to use. */
int jabber_get_privacy( struct gaim_connection *gc )
{
	struct jabber_data *jd = gc->proto_data;
	struct xt_node *node = NULL;
	char *name;
	int st;
	
	if( jd->privacy_active )
	{
		/* If we know what is the active list right now, fetch it. */
		node = xt_new_node( "list", NULL, NULL );
		xt_add_attr( node, "name", jd->privacy_active );
	}
	/* Okay, we don't know yet. If the user set a specific list, we'll
	   activate that one. Otherwise, we should figure out which list is
	   currently active. */
	else if( ( name = set_getstr( &gc->acc->set, "privacy_list" ) ) )
	{
		return jabber_set_privacy( gc, name );
	}
	/* else: sending this packet without a <list/> element will give
	   a list of available lists and information about the currently
	   active list. */
	
	node = xt_new_node( "query", NULL, node );
	xt_add_attr( node, "xmlns", "jabber:iq:privacy" );
	node = jabber_make_packet( "iq", "get", NULL, node );
	
	jabber_cache_packet( gc, node );
	st = jabber_write_packet( gc, node );
	
	return st;
}

int jabber_set_privacy( struct gaim_connection *gc, char *name )
{
	struct xt_node *node;
	
	node = xt_new_node( "active", NULL, NULL );
	xt_add_attr( node, "name", name );
	node = xt_new_node( "query", NULL, node );
	xt_add_attr( node, "xmlns", "jabber:iq:privacy" );
	
	node = jabber_make_packet( "iq", "set", NULL, node );
	jabber_cache_packet( gc, node );
	
	return jabber_write_packet( gc, node );
}

char *set_eval_privacy_list( set_t *set, char *value )
{
	account_t *acc = set->data;
	struct jabber_data *jd = acc->gc->proto_data;
	
	if( jd->flags & JFLAG_PRIVACY_BROKEN )
	{
		serv_got_crap( acc->gc, "Privacy lists not supported by this server" );
		return NULL;
	}
	
	/* If we're on-line, return NULL and let the server decide if the
	   chosen list is valid. If we're off-line, just accept it and we'll
	   see later (when we connect). */
	if( acc->gc )
		jabber_set_privacy( acc->gc, value );
	
	return acc->gc ? NULL : value;
}
