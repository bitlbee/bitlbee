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
	char *ret;
	
	if( strcmp( set->key, "priority" ) == 0 )
		ret = set_eval_int( set, value );
	else
		ret = value;
	
	/* Only run this stuff if the account is online ATM,
	   and if the setting seems to be acceptable. */
	if( acc->gc && ret )
	{
		if( strcmp( set->key, "priority" ) == 0 )
		{
			/* Although set_eval functions usually are very nice
			   and convenient, they have one disadvantage: If I
			   would just call p_s_u() now to send the new prio
			   setting, it would send the old setting because the
			   set->value gets changed when the eval returns a
			   non-NULL value.
			   
			   So now I can choose between implementing post-set
			   functions next to evals, or just do this little
			   hack: */
			g_free( set->value );
			set->value = g_strdup( ret );
			
			/* (Yes, sorry, I prefer the hack. :-P) */
			
			presence_send_update( acc->gc );
		}
		else
		{
		}
	}
	
	return ret;
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

const struct jabber_away_state jabber_away_state_list[] =
{
	{ "away",  "Away" },
	{ "chat",  "Free for Chat" },
	{ "dnd",   "Do not Disturb" },
	{ "xa",    "Extended Away" },
	{ "",      "Online" },
	{ "",      NULL }
};

const struct jabber_away_state *jabber_away_state_by_code( char *code )
{
	int i;
	
	for( i = 0; jabber_away_state_list[i].full_name; i ++ )
		if( g_strcasecmp( jabber_away_state_list[i].code, code ) == 0 )
			return jabber_away_state_list + i;
	
	return NULL;
}

const struct jabber_away_state *jabber_away_state_by_name( char *name )
{
	int i;
	
	for( i = 0; jabber_away_state_list[i].full_name; i ++ )
		if( g_strcasecmp( jabber_away_state_list[i].full_name, name ) == 0 )
			return jabber_away_state_list + i;
	
	return NULL;
}

struct jabber_buddy_ask_data
{
	struct gaim_connection *gc;
	char *handle;
	char *realname;
};

static void jabber_buddy_ask_yes( gpointer w, struct jabber_buddy_ask_data *bla )
{
	presence_send_request( bla->gc, bla->handle, "subscribed" );
	
	if( find_buddy( bla->gc, bla->handle ) == NULL )
		show_got_added( bla->gc, bla->handle, NULL );
	
	g_free( bla->handle );
	g_free( bla );
}

static void jabber_buddy_ask_no( gpointer w, struct jabber_buddy_ask_data *bla )
{
	presence_send_request( bla->gc, bla->handle, "subscribed" );
	
	g_free( bla->handle );
	g_free( bla );
}

void jabber_buddy_ask( struct gaim_connection *gc, char *handle )
{
	struct jabber_buddy_ask_data *bla = g_new0( struct jabber_buddy_ask_data, 1 );
	char *buf;
	
	bla->gc = gc;
	bla->handle = g_strdup( handle );
	
	buf = g_strdup_printf( "The user %s wants to add you to his/her buddy list.", handle );
	do_ask_dialog( gc, buf, bla, jabber_buddy_ask_yes, jabber_buddy_ask_no );
}
