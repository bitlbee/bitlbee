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

static xt_status jabber_parse_roster( struct gaim_connection *gc, struct xt_node *node, struct xt_node *orig );
static xt_status jabber_iq_display_vcard( struct gaim_connection *gc, struct xt_node *node, struct xt_node *orig );

xt_status jabber_pkt_iq( struct xt_node *node, gpointer data )
{
	struct gaim_connection *gc = data;
	struct jabber_data *jd = gc->proto_data;
	struct xt_node *c, *reply = NULL;
	char *type, *s;
	int st, pack = 1;
	
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
			serv_got_crap( gc, "WARNING: Received IQ-%s packet with unknown/expired ID %s!", type, s );
		else if( entry->func )
			return entry->func( gc, node, entry->node );
	}
	else if( strcmp( type, "get" ) == 0 )
	{
		if( !( c = xt_find_node( node->children, "query" ) ) ||
		    !( s = xt_find_attr( c, "xmlns" ) ) )
		{
			serv_got_crap( gc, "WARNING: Received incomplete IQ-%s packet", type );
			return XT_HANDLED;
		}
		
		reply = xt_new_node( "query", NULL, NULL );
		xt_add_attr( reply, "xmlns", s );
		
		/* Of course this is a very essential query to support. ;-) */
		if( strcmp( s, XMLNS_VERSION ) == 0 )
		{
			xt_add_child( reply, xt_new_node( "name", "BitlBee", NULL ) );
			xt_add_child( reply, xt_new_node( "version", BITLBEE_VERSION, NULL ) );
			xt_add_child( reply, xt_new_node( "os", ARCH, NULL ) );
		}
		else if( strcmp( s, XMLNS_TIME ) == 0 )
		{
			time_t time_ep;
			char buf[1024];
			
			buf[sizeof(buf)-1] = 0;
			time_ep = time( NULL );
			
			strftime( buf, sizeof( buf ) - 1, "%Y%m%dT%H:%M:%S", gmtime( &time_ep ) );
			xt_add_child( reply, xt_new_node( "utc", buf, NULL ) );
			
			strftime( buf, sizeof( buf ) - 1, "%Z", localtime( &time_ep ) );
			xt_add_child( reply, xt_new_node( "tz", buf, NULL ) );
		}
		else if( strcmp( s, XMLNS_DISCOVER ) == 0 )
		{
			c = xt_new_node( "identity", NULL, NULL );
			xt_add_attr( c, "category", "client" );
			xt_add_attr( c, "type", "pc" );
			xt_add_attr( c, "name", "BitlBee" );
			xt_add_child( reply, c );
			
			c = xt_new_node( "feature", NULL, NULL );
			xt_add_attr( c, "var", XMLNS_VERSION );
			xt_add_child( reply, c );
			
			c = xt_new_node( "feature", NULL, NULL );
			xt_add_attr( c, "var", XMLNS_TIME );
			xt_add_child( reply, c );
			
			c = xt_new_node( "feature", NULL, NULL );
			xt_add_attr( c, "var", XMLNS_CHATSTATES );
			xt_add_child( reply, c );
			
			/* Later this can be useful to announce things like
			   MUC support. */
		}
		else
		{
			xt_free_node( reply );
			reply = jabber_make_error_packet( node, "feature-not-implemented", "cancel" );
			pack = 0;
		}
	}
	else if( strcmp( type, "set" ) == 0 )
	{
		if( !( c = xt_find_node( node->children, "query" ) ) ||
		    !( s = xt_find_attr( c, "xmlns" ) ) )
		{
			serv_got_crap( gc, "WARNING: Received incomplete IQ-%s packet", type );
			return XT_HANDLED;
		}
		
		/* This is a roster push. XMPP servers send this when someone
		   was added to (or removed from) the buddy list. AFAIK they're
		   sent even if we added this buddy in our own session. */
		if( strcmp( s, XMLNS_ROSTER ) == 0 )
		{
			int bare_len = strlen( gc->acc->user );
			
			if( ( s = xt_find_attr( node, "from" ) ) == NULL ||
			    ( strncmp( s, gc->acc->user, bare_len ) == 0 &&
			      ( s[bare_len] == 0 || s[bare_len] == '/' ) ) )
			{
				jabber_parse_roster( gc, node, NULL );
				
				/* Should we generate a reply here? Don't think it's
				   very important... */
			}
			else
			{
				serv_got_crap( gc, "WARNING: %s tried to fake a roster push!", s ? s : "(unknown)" );
				
				xt_free_node( reply );
				reply = jabber_make_error_packet( node, "not-allowed", "cancel" );
				pack = 0;
			}
		}
		else
		{
			xt_free_node( reply );
			reply = jabber_make_error_packet( node, "feature-not-implemented", "cancel" );
			pack = 0;
		}
	}
	
	/* If we recognized the xmlns and managed to generate a reply,
	   finish and send it. */
	if( reply )
	{
		/* Normally we still have to pack it into an iq-result
		   packet, but for errors, for example, we don't. */
		if( pack )
		{
			reply = jabber_make_packet( "iq", "result", xt_find_attr( node, "from" ), reply );
			if( ( s = xt_find_attr( node, "id" ) ) )
				xt_add_attr( reply, "id", s );
		}
		
		st = jabber_write_packet( gc, reply );
		xt_free_node( reply );
		if( !st )
			return XT_ABORT;
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
	xt_add_attr( node, "xmlns", XMLNS_AUTH );
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
	
	if( !( query = xt_find_node( node->children, "query" ) ) )
	{
		serv_got_crap( gc, "WARNING: Received incomplete IQ packet while authenticating" );
		signoff( gc );
		return XT_HANDLED;
	}
	
	/* Time to authenticate ourselves! */
	reply = xt_new_node( "query", NULL, NULL );
	xt_add_attr( reply, "xmlns", XMLNS_AUTH );
	xt_add_child( reply, xt_new_node( "username", jd->username, NULL ) );
	xt_add_child( reply, xt_new_node( "resource", set_getstr( &gc->acc->set, "resource" ), NULL ) );
	
	if( xt_find_node( query->children, "digest" ) && ( s = xt_find_attr( jd->xt->root, "id" ) ) )
	{
		/* We can do digest authentication, it seems, and of
		   course we prefer that. */
		SHA_CTX sha;
		char hash_hex[41];
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
	struct jabber_data *jd = gc->proto_data;
	char *type;
	
	if( !( type = xt_find_attr( node, "type" ) ) )
	{
		serv_got_crap( gc, "WARNING: Received incomplete IQ packet while authenticating" );
		signoff( gc );
		return XT_HANDLED;
	}
	
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

int jabber_get_roster( struct gaim_connection *gc )
{
	struct xt_node *node;
	int st;
	
	set_login_progress( gc, 1, "Authenticated, requesting buddy list" );
	
	node = xt_new_node( "query", NULL, NULL );
	xt_add_attr( node, "xmlns", XMLNS_ROSTER );
	node = jabber_make_packet( "iq", "get", NULL, node );
	
	jabber_cache_add( gc, node, jabber_parse_roster );
	st = jabber_write_packet( gc, node );
	
	return st;
}

static xt_status jabber_parse_roster( struct gaim_connection *gc, struct xt_node *node, struct xt_node *orig )
{
	struct xt_node *query, *c;
	int initial = ( orig != NULL );
	
	if( !( query = xt_find_node( node->children, "query" ) ) )
	{
		serv_got_crap( gc, "WARNING: Received NULL roster packet" );
		return XT_HANDLED;
	}
	
	c = query->children;
	while( ( c = xt_find_node( c, "item" ) ) )
	{
		char *jid = xt_find_attr( c, "jid" );
		char *name = xt_find_attr( c, "name" );
		char *sub = xt_find_attr( c, "subscription" );
		
		if( !jid || !sub )
		{
			/* Maybe warn. But how likely is this to happen in the first place? */
		}
		else if( initial )
		{
			if( ( strcmp( sub, "both" ) == 0 || strcmp( sub, "to" ) == 0 ) )
				add_buddy( gc, NULL, jid, name );
		}
		else
		{
			/* This is a roster push item. Find out what changed exactly. */
			if( ( strcmp( sub, "both" ) == 0 || strcmp( sub, "to" ) == 0 ) )
			{
				if( find_buddy( gc, jid ) == NULL )
					add_buddy( gc, NULL, jid, name );
				else if( name )
					serv_buddy_rename( gc, jid, name );
			}
			else if( strcmp( sub, "remove" ) == 0 )
			{
				/* Don't have any API call for this yet! So let's
				   just try to handle this as well as we can. */
				jabber_buddy_remove_bare( gc, jid );
				serv_got_update( gc, jid, 0, 0, 0, 0, 0, 0 );
				/* FIXME! */
			}
		}
		
		c = c->next;
	}
	
	if( initial )
		account_online( gc );
	
	return XT_HANDLED;
}

int jabber_get_vcard( struct gaim_connection *gc, char *bare_jid )
{
	struct xt_node *node;
	
	if( strchr( bare_jid, '/' ) )
		return 1;	/* This was an error, but return 0 should only be done if the connection died... */
	
	node = xt_new_node( "vCard", NULL, NULL );
	xt_add_attr( node, "xmlns", XMLNS_VCARD );
	node = jabber_make_packet( "iq", "get", bare_jid, node );
	
	jabber_cache_add( gc, node, jabber_iq_display_vcard );
	return jabber_write_packet( gc, node );
}

static xt_status jabber_iq_display_vcard( struct gaim_connection *gc, struct xt_node *node, struct xt_node *orig )
{
	struct xt_node *vc, *c, *sc; /* subchild, gc is already in use ;-) */
	GString *reply;
	char *s;
	
	if( ( s = xt_find_attr( node, "type" ) ) == NULL ||
	    strcmp( s, "result" ) != 0 ||
	    ( vc = xt_find_node( node->children, "vCard" ) ) == NULL )
	{
		s = xt_find_attr( orig, "to" ); /* If this returns NULL something's wrong.. */
		serv_got_crap( gc, "Could not retrieve vCard of %s", s ? s : "(NULL)" );
		return XT_HANDLED;
	}
	
	s = xt_find_attr( orig, "to" );
	reply = g_string_new( "vCard information for " );
	reply = g_string_append( reply, s ? s : "(NULL)" );
	reply = g_string_append( reply, ":\n" );
	
	/* I hate this format, I really do... */
	
	if( ( c = xt_find_node( vc->children, "FN" ) ) && c->text_len )
		g_string_append_printf( reply, "Name: %s\n", c->text );
	
	if( ( c = xt_find_node( vc->children, "N" ) ) && c->children )
	{
		reply = g_string_append( reply, "Full name:" );
		
		if( ( sc = xt_find_node( c->children, "PREFIX" ) ) && sc->text_len )
			g_string_append_printf( reply, " %s", sc->text );
		if( ( sc = xt_find_node( c->children, "GIVEN" ) ) && sc->text_len )
			g_string_append_printf( reply, " %s", sc->text );
		if( ( sc = xt_find_node( c->children, "MIDDLE" ) ) && sc->text_len )
			g_string_append_printf( reply, " %s", sc->text );
		if( ( sc = xt_find_node( c->children, "FAMILY" ) ) && sc->text_len )
			g_string_append_printf( reply, " %s", sc->text );
		if( ( sc = xt_find_node( c->children, "SUFFIX" ) ) && sc->text_len )
			g_string_append_printf( reply, " %s", sc->text );
		
		reply = g_string_append_c( reply, '\n' );
	}
	
	if( ( c = xt_find_node( vc->children, "NICKNAME" ) ) && c->text_len )
		g_string_append_printf( reply, "Nickname: %s\n", c->text );
	
	if( ( c = xt_find_node( vc->children, "BDAY" ) ) && c->text_len )
		g_string_append_printf( reply, "Date of birth: %s\n", c->text );
	
	/* Slightly alternative use of for... ;-) */
	for( c = vc->children; ( c = xt_find_node( c, "EMAIL" ) ); c = c->next )
	{
		if( ( sc = xt_find_node( c->children, "USERID" ) ) == NULL || sc->text_len == 0 )
			continue;
		
		if( xt_find_node( c->children, "HOME" ) )
			s = "Home";
		else if( xt_find_node( c->children, "WORK" ) )
			s = "Work";
		else
			s = "Misc.";
		
		g_string_append_printf( reply, "%s e-mail address: %s\n", s, sc->text );
	}
	
	if( ( c = xt_find_node( vc->children, "URL" ) ) && c->text_len )
		g_string_append_printf( reply, "Homepage: %s\n", c->text );
	
	/* Slightly alternative use of for... ;-) */
	for( c = vc->children; ( c = xt_find_node( c, "ADR" ) ); c = c->next )
	{
		if( xt_find_node( c->children, "HOME" ) )
			s = "Home";
		else if( xt_find_node( c->children, "WORK" ) )
			s = "Work";
		else
			s = "Misc.";
		
		g_string_append_printf( reply, "%s address: ", s );
		
		if( ( sc = xt_find_node( c->children, "STREET" ) ) && sc->text_len )
			g_string_append_printf( reply, "%s ", sc->text );
		if( ( sc = xt_find_node( c->children, "EXTADR" ) ) && sc->text_len )
			g_string_append_printf( reply, "%s, ", sc->text );
		if( ( sc = xt_find_node( c->children, "PCODE" ) ) && sc->text_len )
			g_string_append_printf( reply, "%s, ", sc->text );
		if( ( sc = xt_find_node( c->children, "LOCALITY" ) ) && sc->text_len )
			g_string_append_printf( reply, "%s, ", sc->text );
		if( ( sc = xt_find_node( c->children, "REGION" ) ) && sc->text_len )
			g_string_append_printf( reply, "%s, ", sc->text );
		if( ( sc = xt_find_node( c->children, "CTRY" ) ) && sc->text_len )
			g_string_append_printf( reply, "%s", sc->text );
		
		if( reply->str[reply->len-2] == ',' )
			reply = g_string_truncate( reply, reply->len-2 );
		
		reply = g_string_append_c( reply, '\n' );
	}
	
	for( c = vc->children; ( c = xt_find_node( c, "TEL" ) ); c = c->next )
	{
		if( ( sc = xt_find_node( c->children, "NUMBER" ) ) == NULL || sc->text_len == 0 )
			continue;
		
		if( xt_find_node( c->children, "HOME" ) )
			s = "Home";
		else if( xt_find_node( c->children, "WORK" ) )
			s = "Work";
		else
			s = "Misc.";
		
		g_string_append_printf( reply, "%s phone number: %s\n", s, sc->text );
	}
	
	if( ( c = xt_find_node( vc->children, "DESC" ) ) && c->text_len )
		g_string_append_printf( reply, "Other information:\n%s", c->text );
	
	/* *sigh* */
	
	serv_got_crap( gc, reply->str );
	g_string_free( reply, TRUE );
	
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
	xt_add_attr( node, "xmlns", XMLNS_ROSTER );
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
	xt_add_attr( node, "xmlns", XMLNS_ROSTER );
	node = jabber_make_packet( "iq", "set", NULL, node );
	
	st = jabber_write_packet( gc, node );
	
	xt_free_node( node );
	return st;
}
