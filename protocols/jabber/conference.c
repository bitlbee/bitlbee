/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Jabber module - Conference rooms                                         *
*                                                                           *
*  Copyright 2007 Wilmer van der Gaast <wilmer@gaast.net>                   *
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

struct groupchat *jabber_chat_join( struct im_connection *ic, char *room, char *nick, char *password )
{
	struct jabber_chat *jc;
	struct xt_node *node;
	struct groupchat *c;
	char *roomjid;
	
	roomjid = g_strdup_printf( "%s/%s", room, nick );
	node = xt_new_node( "x", NULL, NULL );
	xt_add_attr( node, "xmlns", XMLNS_MUC );
	node = jabber_make_packet( "presence", NULL, roomjid, node );
	
	if( !jabber_write_packet( ic, node ) )
	{
		g_free( roomjid );
		xt_free_node( node );
		return NULL;
	}
	xt_free_node( node );
	
	jc = g_new0( struct jabber_chat, 1 );
	jc->name = jabber_normalize( room );
	
	if( ( jc->me = jabber_buddy_add( ic, roomjid ) ) == NULL )
	{
		g_free( roomjid );
		g_free( jc->name );
		g_free( jc );
		return NULL;
	}
	
	/* roomjid isn't normalized yet, and we need an original version
	   of the nick to send a proper presence update. */
	jc->my_full_jid = roomjid;
	
	c = imcb_chat_new( ic, room );
	c->data = jc;
	
	return c;
}

void jabber_chat_free( struct groupchat *c )
{
	struct jabber_chat *jc = c->data;
	
	jabber_buddy_remove_bare( c->ic, jc->name );
	
	g_free( jc->my_full_jid );
	g_free( jc->name );
	g_free( jc );
	
	imcb_chat_free( c );
}

int jabber_chat_msg( struct groupchat *c, char *message, int flags )
{
	struct im_connection *ic = c->ic;
	struct jabber_chat *jc = c->data;
	struct xt_node *node;
	
	node = xt_new_node( "body", message, NULL );
	node = jabber_make_packet( "message", "groupchat", jc->name, node );
	
	if( !jabber_write_packet( ic, node ) )
	{
		xt_free_node( node );
		return 0;
	}
	xt_free_node( node );
	
	return 1;
}

int jabber_chat_leave( struct groupchat *c, const char *reason )
{
	struct im_connection *ic = c->ic;
	struct jabber_chat *jc = c->data;
	struct xt_node *node;
	
	node = xt_new_node( "x", NULL, NULL );
	xt_add_attr( node, "xmlns", XMLNS_MUC );
	node = jabber_make_packet( "presence", "unavailable", jc->my_full_jid, node );
	
	if( !jabber_write_packet( ic, node ) )
	{
		xt_free_node( node );
		return 0;
	}
	xt_free_node( node );
	
	return 1;
}

/* Not really the same syntax as the normal pkt_ functions, but this isn't
   called by the xmltree parser directly and this way I can add some extra
   parameters so we won't have to repeat too many things done by the caller
   already. */
void jabber_chat_pkt_presence( struct im_connection *ic, struct jabber_buddy *bud, struct xt_node *node )
{
	struct groupchat *chat;
	struct xt_node *c;
	char *type = xt_find_attr( node, "type" );
	struct jabber_chat *jc;
	char *s;
	
	if( ( chat = jabber_chat_by_name( ic, bud->bare_jid ) ) == NULL )
	{
		/* How could this happen?? We could do kill( self, 11 )
		   now or just wait for the OS to do it. :-) */
		return;
	}
	
	jc = chat->data;
	
	if( type == NULL && !( bud->flags & JBFLAG_IS_CHATROOM ) )
	{
		bud->flags |= JBFLAG_IS_CHATROOM;
		/* If this one wasn't set yet, this buddy just joined the chat.
		   Slightly hackish way of finding out eh? ;-) */
		
		/* This is pretty messy... Here it sets ext_jid to the real
		   JID of the participant. Works for non-anonymized channels.
		   Might break if someone joins a chat twice, though. */
		for( c = node->children; ( c = xt_find_node( c, "x" ) ); c = c->next )
			if( ( s = xt_find_attr( c, "xmlns" ) ) &&
			    ( strcmp( s, XMLNS_MUC_USER ) == 0 ) )
			{
				c = xt_find_node( c->children, "item" );
				if( ( s = xt_find_attr( c, "jid" ) ) )
				{
					/* Yay, found what we need. :-) */
					bud->ext_jid = jabber_normalize( s );
					break;
				}
			}
		
		/* Make up some other handle, if necessary. */
		if( bud->ext_jid == NULL )
		{
			if( bud == jc->me )
			{
				bud->ext_jid = jabber_normalize( ic->acc->user );
			}
			else
			{
				int i;
				
				/* Don't want the nick to be at the end, so let's
				   think of some slightly different notation to use
				   for anonymous groupchat participants in BitlBee. */
				bud->ext_jid = g_strdup_printf( "%s=%s", bud->resource, bud->bare_jid );
				
				/* And strip any unwanted characters. */
				for( i = 0; bud->resource[i]; i ++ )
					if( bud->ext_jid[i] == '=' || bud->ext_jid[i] == '@' )
						bud->ext_jid[i] = '_';
			}
			bud->flags |= JBFLAG_IS_ANONYMOUS;
		}
		
		if( bud != jc->me )
		{
			imcb_add_buddy( ic, bud->ext_jid, NULL );
			imcb_buddy_nick_hint( ic, bud->ext_jid, bud->resource );
		}
		
		s = strchr( bud->ext_jid, '/' );
		if( s ) *s = 0; /* Should NEVER be NULL, but who knows... */
		imcb_chat_add_buddy( chat, bud->ext_jid );
		if( s ) *s = '/';
	}
	else if( type ) /* type can only be NULL or "unavailable" in this function */
	{
		s = strchr( bud->ext_jid, '/' );
		if( s ) *s = 0;
		imcb_chat_remove_buddy( chat, bud->ext_jid, NULL );
		if( bud != jc->me && bud->flags & JBFLAG_IS_ANONYMOUS )
			imcb_remove_buddy( ic, bud->ext_jid, NULL );
		if( s ) *s = '/';
		
		if( bud == jc->me )
			jabber_chat_free( chat );
	}
}

void jabber_chat_pkt_message( struct im_connection *ic, struct jabber_buddy *bud, struct xt_node *node )
{
	struct xt_node *body = xt_find_node( node->children, "body" );
	struct groupchat *chat;
	char *s;
	
	if( bud == NULL )
	{
		s = xt_find_attr( node, "from" ); /* pkt_message() already NULL-checked this one. */
		if( strchr( s, '/' ) == NULL )
			/* This is fine, the groupchat itself isn't in jd->buddies. */
			imcb_log( ic, "System message from groupchat %s: %s", s, body? body->text : "NULL" );
		else
			/* This, however, isn't fine! */
			imcb_log( ic, "Groupchat message from unknown participant %s: %s", s, body ? body->text : "NULL" );
		
		return;
	}
	else if( ( chat = jabber_chat_by_name( ic, bud->bare_jid ) ) == NULL )
	{
		/* How could this happen?? We could do kill( self, 11 )
		   now or just wait for the OS to do it. :-) */
		return;
	}
	
	if( body && body->text_len > 0 )
	{
		s = strchr( bud->ext_jid, '/' );
		if( s ) *s = 0;
		imcb_chat_msg( chat, bud->ext_jid, body->text, 0, jabber_get_timestamp( node ) );
		if( s ) *s = '/';
	}
}
