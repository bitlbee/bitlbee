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
	
	if( !sasl_supported( gc ) )
	{
		/* Should abort this now, since we should already be doing
		   IQ authentication. Strange things happen when you try
		   to do both... */
		serv_got_crap( gc, "XMPP 1.0 non-compliant server seems to support SASL, please report this as a BitlBee bug!" );
		return XT_HANDLED;
	}
	
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
		hide_login_progress( gc, "No known SASL authentication schemes supported" );
		signoff( gc );
		return XT_ABORT;
	}
	
	reply = xt_new_node( "auth", NULL, NULL );
	xt_add_attr( reply, "xmlns", SASL_NS );
	
	if( sup_digest )
	{
		xt_add_attr( reply, "mechanism", "DIGEST-MD5" );
		
		/* The rest will be done later, when we receive a <challenge/>. */
	}
	else if( sup_plain )
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

static char *sasl_get_part( char *data, char *field )
{
	int i, len;
	
	len = strlen( field );
	
	if( g_strncasecmp( data, field, len ) == 0 && data[len] == '=' )
	{
		i = strlen( field ) + 1;
	}
	else
	{
		for( i = 0; data[i]; i ++ )
		{
			/* If we have a ", skip until it's closed again. */
			if( data[i] == '"' )
			{
				i ++;
				while( data[i] != '"' || data[i-1] == '\\' )
					i ++;
			}
			
			/* If we got a comma, we got a new field. Check it. */
			if( data[i] == ',' &&
			    g_strncasecmp( data + i + 1, field, len ) == 0 &&
			    data[i+len+1] == '=' )
			{
				i += len + 2;
				break;
			}
		}
	}
	
	if( data[i] == '"' )
	{
		int j;
		char *ret;
		
		i ++;
		len = 0;
		while( data[i+len] != '"' || data[i+len-1] == '\\' )
			len ++;
		
		ret = g_strndup( data + i, len );
		for( i = j = 0; ret[i]; i ++ )
		{
			if( ret[i] == '\\' )
			{
				ret[j++] = ret[++i];
			}
			else
			{
				ret[j++] = ret[i];
			}
		}
		ret[j] = 0;
		
		return ret;
	}
	else if( data[i] )
	{
		len = 0;
		while( data[i+len] && data[i+len] != ',' )
			len ++;
		
		return g_strndup( data + i, len );
	}
	else
	{
		return NULL;
	}
}

xt_status sasl_pkt_challenge( struct xt_node *node, gpointer data )
{
	struct gaim_connection *gc = data;
	struct jabber_data *jd = gc->proto_data;
	struct xt_node *reply = NULL;
	char *nonce = NULL, *realm = NULL, *cnonce = NULL, cnonce_bin[30];
	char *digest_uri = NULL;
	char *dec = NULL;
	char *s = NULL;
	xt_status ret = XT_ABORT;
	
	if( node->text_len == 0 )
		goto error;
	
	dec = frombase64( node->text );
	
	if( !( s = sasl_get_part( dec, "rspauth" ) ) )
	{
		/* See RFC 2831 for for information. */
		md5_state_t A1, A2, H;
		md5_byte_t A1r[16], A2r[16], Hr[16];
		char A1h[33], A2h[33], Hh[33];
		int i;
		
		nonce = sasl_get_part( dec, "nonce" );
		realm = sasl_get_part( dec, "realm" );
		
		if( !nonce )
			goto error;
		
		/* Jabber.Org considers the realm part optional and doesn't
		   specify one. Oh well, actually they're right, but still,
		   don't know if this is right... */
		if( !realm )
			realm = g_strdup( jd->server );
		
		random_bytes( (unsigned char *) cnonce_bin, sizeof( cnonce_bin ) );
		cnonce = base64_encode( cnonce_bin, sizeof( cnonce_bin ) );
		digest_uri = g_strdup_printf( "%s/%s", "xmpp", jd->server );
		
		/* Generate the MD5 hash of username:realm:password,
		   I decided to call it H. */
		md5_init( &H );
		s = g_strdup_printf( "%s:%s:%s", jd->username, realm, gc->acc->pass );
		md5_append( &H, (unsigned char *) s, strlen( s ) );
		g_free( s );
		md5_finish( &H, Hr );
		
		/* Now generate the hex. MD5 hash of H:nonce:cnonce, called A1. */
		md5_init( &A1 );
		s = g_strdup_printf( ":%s:%s", nonce, cnonce );
		md5_append( &A1, Hr, 16 );
		md5_append( &A1, (unsigned char *) s, strlen( s ) );
		g_free( s );
		md5_finish( &A1, A1r );
		for( i = 0; i < 16; i ++ )
			sprintf( A1h + i * 2, "%02x", A1r[i] );
		
		/* A2... */
		md5_init( &A2 );
		s = g_strdup_printf( "%s:%s", "AUTHENTICATE", digest_uri );
		md5_append( &A2, (unsigned char *) s, strlen( s ) );
		g_free( s );
		md5_finish( &A2, A2r );
		for( i = 0; i < 16; i ++ )
			sprintf( A2h + i * 2, "%02x", A2r[i] );
		
		/* Final result: A1:nonce:00000001:cnonce:auth:A2. Let's reuse H for it. */
		md5_init( &H );
		s = g_strdup_printf( "%s:%s:%s:%s:%s:%s", A1h, nonce, "00000001", cnonce, "auth", A2h );
		md5_append( &H, (unsigned char *) s, strlen( s ) );
		g_free( s );
		md5_finish( &H, Hr );
		for( i = 0; i < 16; i ++ )
			sprintf( Hh + i * 2, "%02x", Hr[i] );
		
		/* Now build the SASL response string: */
		g_free( dec );
		dec = g_strdup_printf( "username=\"%s\",realm=\"%s\",nonce=\"%s\",cnonce=\"%s\","
		                       "nc=%08x,qop=auth,digest-uri=\"%s\",response=%s,charset=%s",
		                       jd->username, realm, nonce, cnonce, 1, digest_uri, Hh, "utf-8" );
		s = tobase64( dec );
	}
	else
	{
		/* We found rspauth, but don't really care... */
		g_free( s );
		s = NULL;
	}
	
	reply = xt_new_node( "response", s, NULL );
	xt_add_attr( reply, "xmlns", SASL_NS );
	
	if( !jabber_write_packet( gc, reply ) )
		goto silent_error;
	
	ret = XT_HANDLED;
	goto silent_error;

error:
	hide_login_progress( gc, "Incorrect SASL challenge received" );
	signoff( gc );

silent_error:
	g_free( digest_uri );
	g_free( cnonce );
	g_free( nonce );
	g_free( realm );
	g_free( dec );
	g_free( s );
	xt_free_node( reply );
	
	return ret;
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

/* This one is needed to judge if we'll do authentication using IQ or SASL.
   It's done by checking if the <stream:stream> from the server has a
   version attribute. I don't know if this is the right way though... */
gboolean sasl_supported( struct gaim_connection *gc )
{
	struct jabber_data *jd = gc->proto_data;
	
	return ( (void*) ( jd->xt && jd->xt->root && xt_find_attr( jd->xt->root, "version" ) ) ) != NULL;
}
