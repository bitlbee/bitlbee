/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Jabber module - Misc. stuff                                              *
*                                                                           *
*  Copyright 2006-2010 Wilmer van der Gaast <wilmer@gaast.net>              
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
#include "md5.h"
#include "base64.h"

static unsigned int next_id = 1;

char *set_eval_priority( set_t *set, char *value )
{
	account_t *acc = set->data;
	int i;
	
	if( sscanf( value, "%d", &i ) == 1 )
	{
		/* Priority is a signed 8-bit integer, according to RFC 3921. */
		if( i < -128 || i > 127 )
			return SET_INVALID;
	}
	else
		return SET_INVALID;
	
	/* Only run this stuff if the account is online ATM,
	   and if the setting seems to be acceptable. */
	if( acc->ic )
	{
		/* Although set_eval functions usually are very nice and
		   convenient, they have one disadvantage: If I would just
		   call p_s_u() now to send the new prio setting, it would
		   send the old setting because the set->value gets changed
		   after the (this) eval returns a non-NULL value.
		   
		   So now I can choose between implementing post-set
		   functions next to evals, or just do this little hack: */
		
		g_free( set->value );
		set->value = g_strdup( value );
		
		/* (Yes, sorry, I prefer the hack. :-P) */
		
		presence_send_update( acc->ic );
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

struct xt_node *jabber_make_error_packet( struct xt_node *orig, char *err_cond, char *err_type, char *err_code )
{
	struct xt_node *node, *c;
	char *to;
	
	/* Create the "defined-condition" tag. */
	c = xt_new_node( err_cond, NULL, NULL );
	xt_add_attr( c, "xmlns", XMLNS_STANZA_ERROR );
	
	/* Put it in an <error> tag. */
	c = xt_new_node( "error", NULL, c );
	xt_add_attr( c, "type", err_type );
	
	/* Add the error code, if present */
	if (err_code)
		xt_add_attr( c, "code", err_code );
	
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
   it'll get a new id= tag, and do NOT free() the packet after sending it! */
void jabber_cache_add( struct im_connection *ic, struct xt_node *node, jabber_cache_event func )
{
	struct jabber_data *jd = ic->proto_data;
	struct jabber_cache_entry *entry = g_new0( struct jabber_cache_entry, 1 );
	md5_state_t id_hash;
	md5_byte_t id_sum[16];
	char *id, *asc_hash;
	
	next_id ++;
	
	id_hash = jd->cached_id_prefix;
	md5_append( &id_hash, (md5_byte_t*) &next_id, sizeof( next_id ) );
	md5_finish( &id_hash, id_sum );
	asc_hash = base64_encode( id_sum, 12 );
	
	id = g_strdup_printf( "%s%s", JABBER_CACHED_ID, asc_hash );
	xt_add_attr( node, "id", id );
	g_free( id );
	g_free( asc_hash );
	
	entry->node = node;
	entry->func = func;
	entry->saved_at = time( NULL );
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
void jabber_cache_clean( struct im_connection *ic )
{
	struct jabber_data *jd = ic->proto_data;
	time_t threshold = time( NULL ) - JABBER_CACHE_MAX_AGE;
	
	g_hash_table_foreach_remove( jd->node_cache, jabber_cache_clean_entry, &threshold );
}

gboolean jabber_cache_clean_entry( gpointer key, gpointer entry_, gpointer threshold_ )
{
	struct jabber_cache_entry *entry = entry_;
	time_t *threshold = threshold_;
	
	return entry->saved_at < *threshold;
}

xt_status jabber_cache_handle_packet( struct im_connection *ic, struct xt_node *node )
{
	struct jabber_data *jd = ic->proto_data;
	struct jabber_cache_entry *entry;
	char *s;
	
	if( ( s = xt_find_attr( node, "id" ) ) == NULL ||
	    strncmp( s, JABBER_CACHED_ID, strlen( JABBER_CACHED_ID ) ) != 0 )
	{
		/* Silently ignore it, without an ID (or a non-cache
		   ID) we don't know how to handle the packet and we
		   probably don't have to. */
		return XT_HANDLED;
	}
	
	entry = g_hash_table_lookup( jd->node_cache, s );
	
	if( entry == NULL )
	{
		/*
		There's no longer an easy way to see if we generated this
		one or someone else, and there's a ten-minute timeout anyway,
		so meh.
		
		imcb_log( ic, "Warning: Received %s-%s packet with unknown/expired ID %s!",
		              node->name, xt_find_attr( node, "type" ) ? : "(no type)", s );
		*/
	}
	else if( entry->func )
	{
		return entry->func( ic, node, entry->node );
	}
	
	return XT_HANDLED;
}

const struct jabber_away_state jabber_away_state_list[] =
{
	{ "away",  "Away" },
	{ "chat",  "Free for Chat" },   /* WTF actually uses this? */
	{ "dnd",   "Do not Disturb" },
	{ "xa",    "Extended Away" },
	{ "",      NULL }
};

const struct jabber_away_state *jabber_away_state_by_code( char *code )
{
	int i;
	
	if( code == NULL )
		return NULL;
	
	for( i = 0; jabber_away_state_list[i].full_name; i ++ )
		if( g_strcasecmp( jabber_away_state_list[i].code, code ) == 0 )
			return jabber_away_state_list + i;
	
	return NULL;
}

const struct jabber_away_state *jabber_away_state_by_name( char *name )
{
	int i;
	
	if( name == NULL )
		return NULL;
	
	for( i = 0; jabber_away_state_list[i].full_name; i ++ )
		if( g_strcasecmp( jabber_away_state_list[i].full_name, name ) == 0 )
			return jabber_away_state_list + i;
	
	return NULL;
}

struct jabber_buddy_ask_data
{
	struct im_connection *ic;
	char *handle;
	char *realname;
};

static void jabber_buddy_ask_yes( void *data )
{
	struct jabber_buddy_ask_data *bla = data;
	
	presence_send_request( bla->ic, bla->handle, "subscribed" );
	
	imcb_ask_add( bla->ic, bla->handle, NULL );
	
	g_free( bla->handle );
	g_free( bla );
}

static void jabber_buddy_ask_no( void *data )
{
	struct jabber_buddy_ask_data *bla = data;
	
	presence_send_request( bla->ic, bla->handle, "subscribed" );
	
	g_free( bla->handle );
	g_free( bla );
}

void jabber_buddy_ask( struct im_connection *ic, char *handle )
{
	struct jabber_buddy_ask_data *bla = g_new0( struct jabber_buddy_ask_data, 1 );
	char *buf;
	
	bla->ic = ic;
	bla->handle = g_strdup( handle );
	
	buf = g_strdup_printf( "The user %s wants to add you to his/her buddy list.", handle );
	imcb_ask( ic, buf, bla, jabber_buddy_ask_yes, jabber_buddy_ask_no );
	g_free( buf );
}

/* Returns a new string. Don't leak it! */
char *jabber_normalize( const char *orig )
{
	int len, i;
	char *new;
	
	len = strlen( orig );
	new = g_new( char, len + 1 );
	
	/* So it turns out the /resource part is case sensitive. Yeah, and
	   it's Unicode but feck Unicode. :-P So stop once we see a slash. */
	for( i = 0; i < len && orig[i] != '/' ; i ++ )
		new[i] = tolower( orig[i] );
	for( ; orig[i]; i ++ )
		new[i] = orig[i];
	
	new[i] = 0;
	return new;
}

/* Adds a buddy/resource to our list. Returns NULL if full_jid is not really a
   FULL jid or if we already have this buddy/resource. XXX: No, great, actually
   buddies from transports don't (usually) have resources. So we'll really have
   to deal with that properly. Set their ->resource property to NULL. Do *NOT*
   allow to mix this stuff, though... */
struct jabber_buddy *jabber_buddy_add( struct im_connection *ic, char *full_jid_ )
{
	struct jabber_data *jd = ic->proto_data;
	struct jabber_buddy *bud, *new, *bi;
	char *s, *full_jid;
	
	full_jid = jabber_normalize( full_jid_ );
	
	if( ( s = strchr( full_jid, '/' ) ) )
		*s = 0;
	
	new = g_new0( struct jabber_buddy, 1 );
	
	if( ( bud = g_hash_table_lookup( jd->buddies, full_jid ) ) )
	{
		/* The first entry is always a bare JID. If there are more, we
		   should ignore the first one here. */
		if( bud->next )
			bud = bud->next;
		
		/* If this is a transport buddy or whatever, it can't have more
		   than one instance, so this is always wrong: */
		if( s == NULL || bud->resource == NULL )
		{
			if( s ) *s = '/';
			g_free( new );
			g_free( full_jid );
			return NULL;
		}
		
		new->bare_jid = bud->bare_jid;
		
		/* We already have another resource for this buddy, add the
		   new one to the list. */
		for( bi = bud; bi; bi = bi->next )
		{
			/* Check for dupes. */
			if( strcmp( bi->resource, s + 1 ) == 0 )
			{
				*s = '/';
				g_free( new );
				g_free( full_jid );
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
		new->full_jid = new->bare_jid = g_strdup( full_jid );
		g_hash_table_insert( jd->buddies, new->bare_jid, new );
		
		if( s )
		{
			new->next = g_new0( struct jabber_buddy, 1 );
			new->next->bare_jid = new->bare_jid;
			new = new->next;
		}
	}
	
	if( s )
	{
		*s = '/';
		new->full_jid = full_jid;
		new->resource = strchr( new->full_jid, '/' ) + 1;
	}
	else
	{
		/* Let's waste some more bytes of RAM instead of to make
		   memory management a total disaster here. And it saves
		   me one g_free() call in this function. :-P */
		new->full_jid = full_jid;
	}
	
	return new;
}

/* Finds a buddy from our structures. Can find both full- and bare JIDs. When
   asked for a bare JID, it uses the "resource_select" setting to see which
   resource to pick. */
struct jabber_buddy *jabber_buddy_by_jid( struct im_connection *ic, char *jid_, get_buddy_flags_t flags )
{
	struct jabber_data *jd = ic->proto_data;
	struct jabber_buddy *bud, *head;
	char *s, *jid;
	
	jid = jabber_normalize( jid_ );
	
	if( ( s = strchr( jid, '/' ) ) )
	{
		int bare_exists = 0;
		
		*s = 0;
		if( ( bud = g_hash_table_lookup( jd->buddies, jid ) ) )
		{
			bare_exists = 1;
			
			if( bud->next )
				bud = bud->next;
			
			/* Just return the first one for this bare JID. */
			if( flags & GET_BUDDY_FIRST )
			{
				*s = '/';
				g_free( jid );
				return bud;
			}
			
			/* Is this one of those no-resource buddies? */
			if( bud->resource == NULL )
			{
				*s = '/';
				g_free( jid );
				return NULL;
			}
			
			/* See if there's an exact match. */
			for( ; bud; bud = bud->next )
				if( strcmp( bud->resource, s + 1 ) == 0 )
					break;
		}
		
		if( bud == NULL && ( flags & GET_BUDDY_CREAT ) &&
		    ( bare_exists || bee_user_by_handle( ic->bee, ic, jid ) ) )
		{
			*s = '/';
			bud = jabber_buddy_add( ic, jid );
		}
		
		g_free( jid );
		return bud;
	}
	else
	{
		struct jabber_buddy *best_prio, *best_time;
		char *set;
		
		head = g_hash_table_lookup( jd->buddies, jid );
		bud = ( head && head->next ) ? head->next : head;
		
		g_free( jid );
		
		if( bud == NULL )
			/* No match. Create it now? */
			return ( ( flags & GET_BUDDY_CREAT ) &&
			         bee_user_by_handle( ic->bee, ic, jid_ ) ) ?
			           jabber_buddy_add( ic, jid_ ) : NULL;
		else if( bud->resource && ( flags & GET_BUDDY_EXACT ) )
			/* We want an exact match, so in thise case there shouldn't be a /resource. */
			return NULL;
		else if( bud->resource == NULL || bud->next == NULL )
			/* No need for selection if there's only one option. */
			return bud;
		else if( flags & GET_BUDDY_FIRST )
			/* Looks like the caller doesn't care about details. */
			return bud;
		else if( flags & GET_BUDDY_BARE )
			return head;
		
		best_prio = best_time = bud;
		for( ; bud; bud = bud->next )
		{
			if( bud->priority > best_prio->priority )
				best_prio = bud;
			if( bud->last_msg > best_time->last_msg )
				best_time = bud;
		}
		
		if( ( set = set_getstr( &ic->acc->set, "resource_select" ) ) == NULL )
			return NULL;
		else if( strcmp( set, "priority" ) == 0 )
			return best_prio;
		else if( flags & GET_BUDDY_BARE_OK ) /* && strcmp( set, "activity" ) == 0 */
		{
			if( best_time->last_msg + set_getint( &ic->acc->set, "activity_timeout" ) >= time( NULL ) )
				return best_time;
			else
				return head;
		}
		else
			return best_time;
	}
}

/* I'm keeping a separate ext_jid attribute to save a JID that makes sense
   to export to BitlBee. This is mainly for groupchats right now. It's
   a bit of a hack, but I just think having the user nickname in the hostname
   part of the hostmask doesn't look nice on IRC. Normally you can convert
   a normal JID to ext_jid by swapping the part before and after the / and
   replacing the / with a =. But there should be some stripping (@s are
   allowed in Jabber nicks...). */
struct jabber_buddy *jabber_buddy_by_ext_jid( struct im_connection *ic, char *jid_, get_buddy_flags_t flags )
{
	struct jabber_buddy *bud;
	char *s, *jid;
	
	jid = jabber_normalize( jid_ );
	
	if( ( s = strchr( jid, '=' ) ) == NULL )
		return NULL;
	
	for( bud = jabber_buddy_by_jid( ic, s + 1, GET_BUDDY_FIRST ); bud; bud = bud->next )
	{
		/* Hmmm, could happen if not all people in the chat are anonymized? */
		if( bud->ext_jid == NULL )
			continue;
		
		if( strcmp( bud->ext_jid, jid ) == 0 )
			break;
	}
	
	g_free( jid );
	
	return bud;
}

/* Remove one specific full JID from our list. Use this when a buddy goes
   off-line (because (s)he can still be online from a different location.
   XXX: See above, we should accept bare JIDs too... */
int jabber_buddy_remove( struct im_connection *ic, char *full_jid_ )
{
	struct jabber_data *jd = ic->proto_data;
	struct jabber_buddy *bud, *prev = NULL, *bi;
	char *s, *full_jid;
	
	full_jid = jabber_normalize( full_jid_ );
	
	if( ( s = strchr( full_jid, '/' ) ) )
		*s = 0;
	
	if( ( bud = g_hash_table_lookup( jd->buddies, full_jid ) ) )
	{
		if( bud->next )
			bud = (prev=bud)->next;
		
		/* If there's only one item in the list (and if the resource
		   matches), removing it is simple. (And the hash reference
		   should be removed too!) */
		if( bud->next == NULL &&
		    ( ( s == NULL && bud->resource == NULL ) ||
		      ( bud->resource && s && strcmp( bud->resource, s + 1 ) == 0 ) ) )
		{
			return jabber_buddy_remove_bare( ic, full_jid );
		}
		else if( s == NULL || bud->resource == NULL )
		{
			/* Tried to remove a bare JID while this JID does seem
			   to have resources... (Or the opposite.) *sigh* */
			g_free( full_jid );
			return 0;
		}
		else
		{
			for( bi = bud; bi; bi = (prev=bi)->next )
				if( strcmp( bi->resource, s + 1 ) == 0 )
					break;
			
			g_free( full_jid );
			
			if( bi )
			{
				if( prev )
					prev->next = bi->next;
				else
					/* Don't think this should ever happen anymore. */
					g_hash_table_replace( jd->buddies, bi->bare_jid, bi->next );
				
				g_free( bi->ext_jid );
				g_free( bi->full_jid );
				g_free( bi->away_message );
				g_free( bi );
				
				return 1;
			}
			else
			{
				return 0;
			}
		}
	}
	else
	{
		g_free( full_jid );
		return 0;
	}
}

/* Remove a buddy completely; removes all resources that belong to the
   specified bare JID. Use this when removing someone from the contact
   list, for example. */
int jabber_buddy_remove_bare( struct im_connection *ic, char *bare_jid )
{
	struct jabber_data *jd = ic->proto_data;
	struct jabber_buddy *bud, *next;
	
	if( strchr( bare_jid, '/' ) )
		return 0;
	
	if( ( bud = jabber_buddy_by_jid( ic, bare_jid, GET_BUDDY_FIRST ) ) )
	{
		/* Most important: Remove the hash reference. We don't know
		   this buddy anymore. */
		g_hash_table_remove( jd->buddies, bud->bare_jid );
		g_free( bud->bare_jid );
		
		/* Deallocate the linked list of resources. */
		while( bud )
		{
			/* ext_jid && anonymous means that this buddy is
			   specific to one groupchat (the one we're
			   currently cleaning up) so it can be deleted
			   completely. */
			if( bud->ext_jid && bud->flags & JBFLAG_IS_ANONYMOUS )
				imcb_remove_buddy( ic, bud->ext_jid, NULL );
			
			next = bud->next;
			g_free( bud->ext_jid );
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

static gboolean jabber_buddy_remove_all_cb( gpointer key, gpointer value, gpointer data )
{
	struct jabber_buddy *bud, *next;
	
	bud = value;
	while( bud )
	{
		next = bud->next;
		g_free( bud->ext_jid );
		g_free( bud->full_jid );
		g_free( bud->away_message );
		g_free( bud );
		bud = next;
	}
	
	return TRUE;
}

void jabber_buddy_remove_all( struct im_connection *ic )
{
	struct jabber_data *jd = ic->proto_data;
	
	g_hash_table_foreach_remove( jd->buddies, jabber_buddy_remove_all_cb, NULL );
	g_hash_table_destroy( jd->buddies );
}

time_t jabber_get_timestamp( struct xt_node *xt )
{
	struct xt_node *c;
	char *s = NULL;
	struct tm tp;
	
	for( c = xt->children; ( c = xt_find_node( c, "x" ) ); c = c->next )
	{
		if( ( s = xt_find_attr( c, "xmlns" ) ) && strcmp( s, XMLNS_DELAY ) == 0 )
			break;
	}
	
	if( !c || !( s = xt_find_attr( c, "stamp" ) ) )
		return 0;
	
	memset( &tp, 0, sizeof( tp ) );
	if( sscanf( s, "%4d%2d%2dT%2d:%2d:%2d", &tp.tm_year, &tp.tm_mon, &tp.tm_mday,
	                                        &tp.tm_hour, &tp.tm_min, &tp.tm_sec ) != 6 )
		return 0;
	
	tp.tm_year -= 1900;
	tp.tm_mon --;
	
	return mktime_utc( &tp );
}

struct jabber_error *jabber_error_parse( struct xt_node *node, char *xmlns )
{
	struct jabber_error *err;
	struct xt_node *c;
	char *s;
	
	if( node == NULL )
		return NULL;
	
	err = g_new0( struct jabber_error, 1 );
	err->type = xt_find_attr( node, "type" );
	
	for( c = node->children; c; c = c->next )
	{
		if( !( s = xt_find_attr( c, "xmlns" ) ) ||
		    strcmp( s, xmlns ) != 0 )
			continue;
		
		if( strcmp( c->name, "text" ) != 0 )
		{
			err->code = c->name;
		}
		/* Only use the text if it doesn't have an xml:lang attribute,
		   if it's empty or if it's set to something English. */
		else if( !( s = xt_find_attr( c, "xml:lang" ) ) ||
		         !*s || strncmp( s, "en", 2 ) == 0 )
		{
			err->text = c->text;
		}
	}
	
	return err;
}

void jabber_error_free( struct jabber_error *err )
{
	g_free( err );
}
