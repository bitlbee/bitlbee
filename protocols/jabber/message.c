/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Jabber module - Handling of message(s) (tags), etc                       *
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

static unsigned int next_receipt_id = 1;

xt_status jabber_pkt_message( struct xt_node *node, gpointer data )
{
	struct im_connection *ic = data;
	struct jabber_data *jd = ic->proto_data;
	char *from = xt_find_attr( node, "from" );
	char *type = xt_find_attr( node, "type" );
	char *id = xt_find_attr( node, "id" );
	struct xt_node *body = xt_find_node( node->children, "body" ), *c;
	struct xt_node *request = xt_find_node( node->children, "request" );
	struct xt_node *received, *receipt;
	struct jabber_buddy *bud = NULL;
	char *s, *room = NULL, *reason = NULL;
	
	if( !from )
		return XT_HANDLED; /* Consider this packet corrupted. */

	if( request && id )
	{
		/* Send a message receipt (XEP-0184), looking like this:
		 * <message
		 *  from='kingrichard@royalty.england.lit/throne'
		 *  id='bi29sg183b4v'
		 *  to='northumberland@shakespeare.lit/westminster'>
		 *  <received xmlns='urn:xmpp:receipts' id='richard2-4.1.247'/>
		 * </message> */
		received = xt_new_node( "received", NULL, NULL );
		xt_add_attr( received, "xmlns", XMLNS_RECEIPTS );
		xt_add_attr( received, "id", id );
		receipt = jabber_make_packet( "message", NULL, from, received );
		xt_add_attr( receipt, "from", jd->me );

		char *id = g_strdup_printf( "%sRCPT%05x", JABBER_PACKET_ID, ( next_receipt_id++ ) & 0xfffff );
		xt_add_attr( receipt, "id", id );
		g_free( id );

		jabber_write_packet( ic, receipt );
		xt_free_node( receipt );
	}
	
	bud = jabber_buddy_by_jid( ic, from, GET_BUDDY_EXACT );
	
	if( type && strcmp( type, "error" ) == 0 )
	{
		/* Handle type=error packet. */
	}
	else if( type && from && strcmp( type, "groupchat" ) == 0 )
	{
		jabber_chat_pkt_message( ic, bud, node );
	}
	else /* "chat", "normal", "headline", no-type or whatever. Should all be pretty similar. */
	{
		GString *fullmsg = g_string_new( "" );

		for( c = node->children; ( c = xt_find_node( c, "x" ) ); c = c->next )
		{
			char *ns = xt_find_attr( c, "xmlns" );
			struct xt_node *inv;
			
			if( ns && strcmp( ns, XMLNS_MUC_USER ) == 0 &&
			    ( inv = xt_find_node( c->children, "invite" ) ) )
			{
				/* This is an invitation. Set some vars which
				   will be passed to imcb_chat_invite() below. */
				room = from;
				if( ( from = xt_find_attr( inv, "from" ) ) == NULL )
					from = room;
				if( ( inv = xt_find_node( inv->children, "reason" ) ) && inv->text_len > 0 )
					reason = inv->text;
			}
		}
		
		if( ( s = strchr( from, '/' ) ) )
		{
			if( bud )
			{
				bud->last_msg = time( NULL );
				from = bud->ext_jid ? bud->ext_jid : bud->bare_jid;
			}
			else
				*s = 0; /* We need to generate a bare JID now. */
		}
		
		if( type && strcmp( type, "headline" ) == 0 )
		{
			if( ( c = xt_find_node( node->children, "subject" ) ) && c->text_len > 0 )
				g_string_append_printf( fullmsg, "Headline: %s\n", c->text );
			
			/* <x xmlns="jabber:x:oob"><url>http://....</url></x> can contain a URL, it seems. */
			for( c = node->children; c; c = c->next )
			{
				struct xt_node *url;
				
				if( ( url = xt_find_node( c->children, "url" ) ) && url->text_len > 0 )
					g_string_append_printf( fullmsg, "URL: %s\n", url->text );
			}
		}
		else if( ( c = xt_find_node( node->children, "subject" ) ) && c->text_len > 0 &&
		         ( !bud || !( bud->flags & JBFLAG_HIDE_SUBJECT ) ) )
		{
			g_string_append_printf( fullmsg, "<< \002BitlBee\002 - Message with subject: %s >>\n", c->text );
			if( bud )
				bud->flags |= JBFLAG_HIDE_SUBJECT;
		}
		else if( bud && !c )
		{
			/* Yeah, possibly we're hiding changes to this field now. But nobody uses
			   this for anything useful anyway, except GMail when people reply to an
			   e-mail via chat, repeating the same subject all the time. I don't want
			   to have to remember full subject strings for everyone. */
			bud->flags &= ~JBFLAG_HIDE_SUBJECT;
		}
		
		if( body && body->text_len > 0 ) /* Could be just a typing notification. */
			fullmsg = g_string_append( fullmsg, body->text );
		
		if( fullmsg->len > 0 )
			imcb_buddy_msg( ic, from, fullmsg->str,
			                0, jabber_get_timestamp( node ) );
		if( room )
			imcb_chat_invite( ic, room, from, reason );
		
		g_string_free( fullmsg, TRUE );
		
		/* Handling of incoming typing notifications. */
		if( bud == NULL )
		{
			/* Can't handle these for unknown buddies. */
		}
		else if( xt_find_node( node->children, "composing" ) )
		{
			bud->flags |= JBFLAG_DOES_XEP85;
			imcb_buddy_typing( ic, from, OPT_TYPING );
		}
		/* No need to send a "stopped typing" signal when there's a message. */
		else if( xt_find_node( node->children, "active" ) && ( body == NULL ) )
		{
			bud->flags |= JBFLAG_DOES_XEP85;
			imcb_buddy_typing( ic, from, 0 );
		}
		else if( xt_find_node( node->children, "paused" ) )
		{
			bud->flags |= JBFLAG_DOES_XEP85;
			imcb_buddy_typing( ic, from, OPT_THINKING );
		}
		
		if( s )
			*s = '/'; /* And convert it back to a full JID. */
	}
	
	return XT_HANDLED;
}
