/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Jabber module - Misc. stuff                                              *
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

static int next_id = 1;

char *set_eval_resprio( set_t *set, char *value )
{
	account_t *acc = set->data;
	
	/* Only run this stuff if the account is online ATM. */
	if( acc->gc )
	{
		/* ... */
	}
	
	if( g_strcasecmp( set->key, "priority" ) == 0 )
		return set_eval_int( set, value );
	else
		return value;
}

char *set_eval_tls( set_t *set, char *value )
{
	if( g_strcasecmp( value, "try" ) == 0 )
		return value;
	else
		return set_eval_bool( set, value );
}

struct xt_node *jabber_make_packet( char *name, char *type, char *to, struct xt_node *children )
{
	struct xt_node *node;
	
	node = xt_new_node( name, NULL, children );
	
	if( type )
		xt_add_attr( node, "type", type );
	if( to )
		xt_add_attr( node, "to", to );
	
	return node;
}

/* Cache a node/packet for later use. Mainly useful for IQ packets if you need
   them when you receive the response. Use this BEFORE sending the packet so
   it'll get an id= tag, and do NOT free() the packet after writing it! */
void jabber_cache_packet( struct gaim_connection *gc, struct xt_node *node )
{
	struct jabber_data *jd = gc->proto_data;
	char *id = g_strdup_printf( "BeeX%04x", next_id++ );
	
	/* FIXME: Maybe start using g_error() here if nodes still have a parent, for example? */
	
	xt_add_attr( node, "id", id );
	xt_add_child( jd->node_cache, node );
	g_free( id );
}

/* Emptying this cache is a BIG TODO! */
struct xt_node *jabber_packet_from_cache( struct gaim_connection *gc, char *id )
{
	struct jabber_data *jd = gc->proto_data;
	struct xt_node *node;
	char *s;
	
	for( node = jd->node_cache->children; node; node = node->next )
		if( ( s = xt_find_attr( node, "id" ) ) && strcmp( id, s ) == 0 )
			break;
	
	return node;
}
