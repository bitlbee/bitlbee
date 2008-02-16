/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Jabber module - Handling of presence (tags), etc                         *
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

xt_status jabber_pkt_presence( struct xt_node *node, gpointer data )
{
	struct im_connection *ic = data;
	char *from = xt_find_attr( node, "from" );
	char *type = xt_find_attr( node, "type" );	/* NULL should mean the person is online. */
	struct xt_node *c, *cap;
	struct jabber_buddy *bud, *send_presence = NULL;
	int is_chat = 0;
	char *s;
	
	if( !from )
		return XT_HANDLED;
	
	if( ( s = strchr( from, '/' ) ) )
	{
		*s = 0;
		if( jabber_chat_by_jid( ic, from ) )
			is_chat = 1;
		*s = '/';
	}
	
	if( type == NULL )
	{
		if( !( bud = jabber_buddy_by_jid( ic, from, GET_BUDDY_EXACT | GET_BUDDY_CREAT ) ) )
		{
			if( set_getbool( &ic->irc->set, "debug" ) )
				imcb_log( ic, "Warning: Could not handle presence information from JID: %s", from );
			return XT_HANDLED;
		}
		
		g_free( bud->away_message );
		if( ( c = xt_find_node( node->children, "status" ) ) && c->text_len > 0 )
			bud->away_message = g_strdup( c->text );
		else
			bud->away_message = NULL;
		
		if( ( c = xt_find_node( node->children, "show" ) ) && c->text_len > 0 )
		{
			bud->away_state = (void*) jabber_away_state_by_code( c->text );
		}
		else
		{
			bud->away_state = NULL;
			/* Let's only set last_act if there's *no* away state,
			   since it could be some auto-away thingy. */
			bud->last_act = time( NULL );
		}
		
		if( ( c = xt_find_node( node->children, "priority" ) ) && c->text_len > 0 )
			bud->priority = atoi( c->text );
		else
			bud->priority = 0;
		
		if( bud && ( cap = xt_find_node( node->children, "c" ) ) &&
		    ( s = xt_find_attr( cap, "xmlns" ) ) && strcmp( s, XMLNS_CAPS ) == 0 )
		{
			/* This <presence> stanza includes an XEP-0115
			   capabilities part. Not too interesting, but we can
			   see if it has an ext= attribute. */
			s = xt_find_attr( cap, "ext" );
			if( s && ( strstr( s, "cstates" ) || strstr( s, "chatstate" ) ) )
				bud->flags |= JBFLAG_DOES_XEP85;
			
			/* This field can contain more information like xhtml
			   support, but we don't support that ourselves.
			   Officially the ext= tag was deprecated, but enough
			   clients do send it.
			   
			   (I'm aware that this is not the right way to use
			   this field.) See for an explanation of ext=:
			   http://www.xmpp.org/extensions/attic/xep-0115-1.3.html*/
		}
		
		if( is_chat )
			jabber_chat_pkt_presence( ic, bud, node );
		else
			send_presence = jabber_buddy_by_jid( ic, bud->bare_jid, 0 );
	}
	else if( strcmp( type, "unavailable" ) == 0 )
	{
		if( ( bud = jabber_buddy_by_jid( ic, from, 0 ) ) == NULL )
		{
			if( set_getbool( &ic->irc->set, "debug" ) )
				imcb_log( ic, "Warning: Received presence information from unknown JID: %s", from );
			return XT_HANDLED;
		}
		
		/* Handle this before we delete the JID. */
		if( is_chat )
		{
			jabber_chat_pkt_presence( ic, bud, node );
		}
		
		if( strchr( from, '/' ) == NULL )
			/* Sometimes servers send a type="unavailable" from a
			   bare JID, which should mean that suddenly all
			   resources for this JID disappeared. */
			jabber_buddy_remove_bare( ic, from );
		else
			jabber_buddy_remove( ic, from );
		
		if( is_chat )
		{
			/* Nothing else to do for now? */
		}
		else if( ( s = strchr( from, '/' ) ) )
		{
			*s = 0;
		
			/* If another resource is still available, send its presence
			   information. */
			if( ( send_presence = jabber_buddy_by_jid( ic, from, 0 ) ) == NULL )
			{
				/* Otherwise, count him/her as offline now. */
				imcb_buddy_status( ic, from, 0, NULL, NULL );
			}
			
			*s = '/';
		}
		else
		{
			imcb_buddy_status( ic, from, 0, NULL, NULL );
		}
	}
	else if( strcmp( type, "subscribe" ) == 0 )
	{
		jabber_buddy_ask( ic, from );
	}
	else if( strcmp( type, "subscribed" ) == 0 )
	{
		/* Not sure about this one, actually... */
		imcb_log( ic, "%s just accepted your authorization request", from );
	}
	else if( strcmp( type, "unsubscribe" ) == 0 || strcmp( type, "unsubscribed" ) == 0 )
	{
		/* Do nothing here. Plenty of control freaks or over-curious
		   souls get excited when they can see who still has them in
		   their buddy list and who finally removed them. Somehow I
		   got the impression that those are the people who get
		   removed from many buddy lists for "some" reason...
		   
		   If you're one of those people, this is your chance to write
		   your first line of code in C... */
	}
	else if( strcmp( type, "error" ) == 0 )
	{
		return jabber_cache_handle_packet( ic, node );
		
		/*
		struct jabber_error *err;
		if( ( c = xt_find_node( node->children, "error" ) ) )
		{
			err = jabber_error_parse( c, XMLNS_STANZA_ERROR );
			imcb_error( ic, "Stanza (%s) error: %s%s%s", node->name,
			            err->code, err->text ? ": " : "",
			            err->text ? err->text : "" );
			jabber_error_free( err );
		} */
	}

	if( send_presence )
	{
		int is_away = 0;

		if( send_presence->away_state && !( *send_presence->away_state->code == 0 ||
		    strcmp( send_presence->away_state->code, "chat" ) == 0 ) )
			is_away = OPT_AWAY;

		imcb_buddy_status( ic, send_presence->bare_jid, OPT_LOGGED_IN | is_away,
		                   ( is_away && send_presence->away_state ) ?
		                   send_presence->away_state->full_name : NULL,
		                   send_presence->away_message );
	}
	
	return XT_HANDLED;
}

/* Whenever presence information is updated, call this function to inform the
   server. */
int presence_send_update( struct im_connection *ic )
{
	struct jabber_data *jd = ic->proto_data;
	struct xt_node *node, *cap;
	char *show = jd->away_state->code;
	char *status = jd->away_message;
	struct groupchat *c;
	int st;
	
	node = jabber_make_packet( "presence", NULL, NULL, NULL );
	xt_add_child( node, xt_new_node( "priority", set_getstr( &ic->acc->set, "priority" ), NULL ) );
	if( show && *show )
		xt_add_child( node, xt_new_node( "show", show, NULL ) );
	if( status )
		xt_add_child( node, xt_new_node( "status", status, NULL ) );
	
	/* This makes the packet slightly bigger, but clients interested in
	   capabilities can now cache the discovery info. This reduces the
	   usual post-login iq-flood. See XEP-0115. At least libpurple and
	   Trillian seem to do this right. */
	cap = xt_new_node( "c", NULL, NULL );
	xt_add_attr( cap, "xmlns", XMLNS_CAPS );
	xt_add_attr( cap, "node", "http://bitlbee.org/xmpp/caps" );
	xt_add_attr( cap, "ver", BITLBEE_VERSION ); /* The XEP wants this hashed, but nobody's doing that. */
	xt_add_child( node, cap );
	
	st = jabber_write_packet( ic, node );
	
	/* Have to send this update to all groupchats too, the server won't
	   do this automatically. */
	for( c = ic->groupchats; c && st; c = c->next )
	{
		struct jabber_chat *jc = c->data;
		
		xt_add_attr( node, "to", jc->my_full_jid );
		st = jabber_write_packet( ic, node );
	}
	
	xt_free_node( node );
	return st;
}

/* Send a subscribe/unsubscribe request to a buddy. */
int presence_send_request( struct im_connection *ic, char *handle, char *request )
{
	struct xt_node *node;
	int st;
	
	node = jabber_make_packet( "presence", NULL, NULL, NULL );
	xt_add_attr( node, "to", handle );
	xt_add_attr( node, "type", request );
	
	st = jabber_write_packet( ic, node );
	
	xt_free_node( node );
	return st;
}
