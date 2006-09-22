/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Jabber module - SASL authentication                                      *
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
#include "base64.h"

#define SASL_NS "urn:ietf:params:xml:ns:xmpp-sasl"

xt_status sasl_pkt_mechanisms( struct xt_node *node, gpointer data )
{
	struct gaim_connection *gc = data;
	struct jabber_data *jd = gc->proto_data;
	struct xt_node *c, *reply;
	char *s;
	int sup_plain = 0, sup_digest = 0;
	
	s = xt_find_attr( node, "xmlns" );
	if( !s || strcmp( s, SASL_NS ) != 0 )
	{
		signoff( gc );
		return XT_ABORT;
	}
	
	c = node->children;
	while( ( c = xt_find_node( c, "mechanism" ) ) )
	{
		if( c->text && g_strcasecmp( c->text, "PLAIN" ) == 0 )
			sup_plain = 1;
		if( c->text && g_strcasecmp( c->text, "DIGEST-MD5" ) == 0 )
			sup_digest = 1;
		
		c = c->next;
	}
	
	if( !sup_plain && !sup_digest )
	{
		signoff( gc );
		return XT_ABORT;
	}
	
	reply = xt_new_node( "auth", NULL, NULL );
	xt_add_attr( reply, "xmlns", SASL_NS );
	
	if( sup_plain )
	{
		int len;
		
		xt_add_attr( reply, "mechanism", "PLAIN" );
		
		/* With SASL PLAIN in XMPP, the text should be b64(\0user\0pass) */
		len = strlen( jd->username ) + strlen( gc->acc->pass ) + 2;
		s = g_malloc( len + 1 );
		s[0] = 0;
		strcpy( s + 1, jd->username );
		strcpy( s + 2 + strlen( jd->username ), gc->acc->pass );
		reply->text = base64_encode( s, len );
		reply->text_len = strlen( reply->text );
		g_free( s );
	}
	
	if( !jabber_write_packet( gc, reply ) )
	{
		xt_free_node( reply );
		return XT_ABORT;
	}
	xt_free_node( reply );
	
	/* To prevent classic authentication from happening. */
	jd->flags |= JFLAG_STREAM_STARTED;
	
	return XT_HANDLED;
}

xt_status sasl_pkt_challenge( struct xt_node *node, gpointer data )
{
}

xt_status sasl_pkt_result( struct xt_node *node, gpointer data )
{
	struct gaim_connection *gc = data;
	struct jabber_data *jd = gc->proto_data;
	char *s;
	
	s = xt_find_attr( node, "xmlns" );
	if( !s || strcmp( s, SASL_NS ) != 0 )
	{
		signoff( gc );
		return XT_ABORT;
	}
	
	if( strcmp( node->name, "success" ) == 0 )
	{
		set_login_progress( gc, 1, "Authentication finished" );
		jd->flags |= JFLAG_AUTHENTICATED | JFLAG_STREAM_RESTART;
	}
	else if( strcmp( node->name, "failure" ) == 0 )
	{
		hide_login_progress( gc, "Authentication failure" );
		signoff( gc );
		return XT_ABORT;
	}
	
	return XT_HANDLED;
}
