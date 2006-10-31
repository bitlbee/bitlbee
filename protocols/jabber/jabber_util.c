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

static unsigned int next_id = 1;

char *set_eval_priority( set_t *set, char *value )
{
	account_t *acc = set->data;
	int i;
	
	if( sscanf( value, "%d", &i ) == 1 )
	{
		/* Priority is a signed 8-bit integer, according to RFC 3921. */
		if( i < -128 || i > 127 )
			return NULL;
	}
	else
		return NULL;
	
	/* Only run this stuff if the account is online ATM,
	   and if the setting seems to be acceptable. */
	if( acc->gc )
	{
		/* Although set_eval functions usually are very nice and
		   convenient, they have one disadvantage: If I would just
		   call p_s_u() now to send the new prio setting, it would
		   send the old setting because the set->value gets changed
		   when the eval returns a non-NULL value.
		   
		   So now I can choose between implementing post-set
		   functions next to evals, or just do this little hack: */
		
		g_free( set->value );
		set->value = g_strdup( value );
		
		/* (Yes, sorry, I prefer the hack. :-P) */
		
		presence_send_update( acc->gc );
	}
	
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
	
	/* IQ packets should always have an ID, so let's generate one. It
	   might get overwritten by jabber_cache_add() if this packet has
	   to be saved until we receive a response. Cached packets get
	   slightly different IDs so we can recognize them. */
	if( strcmp( name, "iq" ) == 0 )
	{
		char *id = g_strdup_printf( "%s%05x", JABBER_PACKET_ID, ( next_id++ ) & 0xfffff );
		xt_add_attr( node, "id", id );
		g_free( id );
	}
	
	return node;
}

struct xt_node *jabber_make_error_packet( struct xt_node *orig, char *err_cond, char *err_type )
{
	struct xt_node *node, *c;
	char *to;
	
	/* Create the "defined-condition" tag. */
	c = xt_new_node( err_cond, NULL, NULL );
	xt_add_attr( c, "xmlns", XMLNS_STANZA_ERROR );
	
	/* Put it in an <error> tag. */
	c = xt_new_node( "error", NULL, c );
	xt_add_attr( c, "type", err_type );
	
	/* To make the actual error packet, we copy the original packet and
	   add our <error>/type="error" tag. Including the original packet
	   is recommended, so let's just do it. */
	node = xt_dup( orig );
	xt_add_child( node, c );
	xt_add_attr( node, "type", "error" );
	
	/* Return to sender. */
	if( ( to = xt_find_attr( node, "from" ) ) )
	{
		xt_add_attr( node, "to", to );
		xt_remove_attr( node, "from" );
	}
		
	return node;
}

/* Cache a node/packet for later use. Mainly useful for IQ packets if you need
   them when you receive the response. Use this BEFORE sending the packet so
   it'll get a new id= tag, and do NOT free() the packet after writing it! */
void jabber_cache_add( struct gaim_connection *gc, struct xt_node *node, jabber_cache_event func )
{
	struct jabber_data *jd = gc->proto_data;
	char *id = g_strdup_printf( "%s%05x", JABBER_CACHED_ID, ( next_id++ ) & 0xfffff );
	struct jabber_cache_entry *entry = g_new0( struct jabber_cache_entry, 1 );
	
	xt_add_attr( node, "id", id );
	g_free( id );
	
	entry->node = node;
	entry->func = func;
	g_hash_table_insert( jd->node_cache, xt_find_attr( node, "id" ), entry );
}

void jabber_cache_entry_free( gpointer data )
{
	struct jabber_cache_entry *entry = data;
	
	xt_free_node( entry->node );
	g_free( entry );
}

gboolean jabber_cache_clean_entry( gpointer key, gpointer entry, gpointer nullpointer );

/* This one should be called from time to time (from keepalive, in this case)
   to make sure things don't stay in the node cache forever. By marking nodes
   during the first run and deleting marked nodes during a next run, every
   node should be available in the cache for at least a minute (assuming the
   function is indeed called every minute). */
void jabber_cache_clean( struct gaim_connection *gc )
{
	struct jabber_data *jd = gc->proto_data;
	
	g_hash_table_foreach_remove( jd->node_cache, jabber_cache_clean_entry, NULL );
}

gboolean jabber_cache_clean_entry( gpointer key, gpointer entry_, gpointer nullpointer )
{
	struct jabber_cache_entry *entry = entry_;
	struct xt_node *node = entry->node;
	
	if( node->flags & XT_SEEN )
		return TRUE;
	else
	{
		node->flags |= XT_SEEN;
		return FALSE;
	}
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
	g_free( buf );
}

/* Adds a buddy/resource to our list. Returns NULL if full_jid is not really a
   FULL jid or if we already have this buddy/resource. */
struct jabber_buddy *jabber_buddy_add( struct gaim_connection *gc, char *full_jid )
{
	struct jabber_data *jd = gc->proto_data;
	struct jabber_buddy *bud, *new, *bi;
	char *s;
	
	if( !( s = strchr( full_jid, '/' ) ) )
		return NULL;
	
	new = g_new0( struct jabber_buddy, 1 );
	
	*s = 0;
	if( ( bud = g_hash_table_lookup( jd->buddies, full_jid ) ) )
	{
		new->handle = bud->handle;
		
		/* We already have another resource for this buddy, add the
		   new one to the list. */
		for( bi = bud; bi; bi = bi->next )
		{
			/* Check for dupes. Resource seem to be case sensitive. */
			if( strcmp( bi->resource, s + 1 ) == 0 )
			{
				*s = '/';
				g_free( new );
				return NULL;
			}
			/* Append the new item to the list. */
			else if( bi->next == NULL )
			{
				bi->next = new;
				break;
			}
		}
	}
	else
	{
		new->handle = g_strdup( full_jid );
		g_hash_table_insert( jd->buddies, new->handle, new );
	}
	
	*s = '/';
	new->full_jid = g_strdup( full_jid );
	new->resource = strchr( new->full_jid, '/' ) + 1;
	
	return new;
}

/* Finds a buddy from our structures. Can find both full- and bare JIDs. When
   asked for a bare JID, it uses the "resource_select" setting to see which
   resource to pick. */
struct jabber_buddy *jabber_buddy_by_jid( struct gaim_connection *gc, char *jid )
{
	struct jabber_data *jd = gc->proto_data;
	struct jabber_buddy *bud;
	char *s;
	
	if( ( s = strchr( jid, '/' ) ) )
	{
		*s = 0;
		if( ( bud = g_hash_table_lookup( jd->buddies, jid ) ) )
			for( ; bud; bud = bud->next )
				if( strcmp( bud->resource, s + 1 ) == 0 )
					break;
	}
	else
	{
		struct jabber_buddy *best_prio, *best_time;
		char *set;
		
		best_prio = best_time = bud = g_hash_table_lookup( jd->buddies, jid );
		for( ; bud; bud = bud->next )
		{
			if( bud->priority > best_prio->priority )
				best_prio = bud;
			if( bud->last_act > best_time->last_act )
				best_time = bud;
		}
		
		if( ( set = set_getstr( &gc->acc->set, "resource_select" ) ) == NULL )
			return NULL;
		else if( strcmp( set, "activity" ) == 0 )
			return best_time;
		else /* if( strcmp( set, "priority" ) == 0 ) */
			return best_prio;
	}
	
	*s = '/';
	return bud;
}

/* Remove one specific full JID from our list. Use this when a buddy goes
   off-line (because (s)he can still be online from a different location. */
int jabber_buddy_remove( struct gaim_connection *gc, char *full_jid )
{
	struct jabber_data *jd = gc->proto_data;
	struct jabber_buddy *bud, *prev, *bi;
	char *s;
	
	if( !( s = strchr( full_jid, '/' ) ) )
		return 0;
	
	*s = 0;
	if( ( bud = g_hash_table_lookup( jd->buddies, full_jid ) ) )
	{
		/* If there's only one item in the list (and if the resource
		   matches), removing it is simple. (And the hash reference
		   should be removed too!) */
		if( bud->next == NULL && strcmp( bud->resource, s + 1 ) == 0 )
		{
			g_hash_table_remove( jd->buddies, bud->handle );
			g_free( bud->handle );
			g_free( bud->full_jid );
			g_free( bud->away_message );
			g_free( bud );
		}
		else
		{
			for( bi = bud, prev = NULL; bi; bi = (prev=bi)->next )
				if( strcmp( bi->resource, s + 1 ) == 0 )
					break;
			
			if( bi )
			{
				if( prev )
					prev->next = bi->next;
				else
					/* The hash table should point at the second
					   item, because we're removing the first. */
					g_hash_table_replace( jd->buddies, bi->handle, bi->next );
				
				g_free( bi->full_jid );
				g_free( bi->away_message );
				g_free( bi );
			}
			else
			{
				*s = '/';
				return 0;
			}
		}
		
		*s = '/';
		return 1;
	}
	else
	{
		*s = '/';
		return 0;
	}
}

/* Remove a buddy completely; removes all resources that belong to the
   specified bare JID. Use this when removing someone from the contact
   list, for example. */
int jabber_buddy_remove_bare( struct gaim_connection *gc, char *bare_jid )
{
	struct jabber_data *jd = gc->proto_data;
	struct jabber_buddy *bud, *next;
	
	if( strchr( bare_jid, '/' ) )
		return 0;
	
	if( ( bud = g_hash_table_lookup( jd->buddies, bare_jid ) ) )
	{
		/* Most important: Remove the hash reference. We don't know
		   this buddy anymore. */
		g_hash_table_remove( jd->buddies, bud->handle );
		
		/* Deallocate the linked list of resources. */
		while( bud )
		{
			next = bud->next;
			g_free( bud->full_jid );
			g_free( bud->away_message );
			g_free( bud );
			bud = next;
		}
		
		return 1;
	}
	else
	{
		return 0;
	}
}
