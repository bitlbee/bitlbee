/** passport.c
 *
 * Functions to login to Microsoft Passport service for Messenger
 * Copyright (C) 2004-2008 Wilmer van der Gaast <wilmer@gaast.net>
 *
 * This program is free software; you can redistribute it and/or modify             
 * it under the terms of the GNU General Public License version 2                   
 * as published by the Free Software Foundation                                     
 *                                                                                   
 * This program is distributed in the hope that is will be useful,                  
 * bit WITHOU ANY WARRANTY; without even the implied warranty of                   
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                    
 * GNU General Public License for more details.                                     
 *                                                                                   
 * You should have received a copy of the GNU General Public License                
 * along with this program; if not, write to the Free Software                      
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA          
 *
 */

#include "http_client.h"
#include "passport.h"
#include "msn.h"
#include "bitlbee.h"
#include "url.h"
#include "misc.h"
#include "xmltree.h"
#include <ctype.h>
#include <errno.h>

static int passport_get_token_real( struct msn_auth_data *mad );
static void passport_get_token_ready( struct http_request *req );

int passport_get_token( gpointer func, gpointer data, char *username, char *password, char *cookie )
{
	struct msn_auth_data *mad = g_new0( struct msn_auth_data, 1 );
	int i;
	
	mad->username = g_strdup( username );
	mad->password = g_strdup( password );
	mad->cookie = g_strdup( cookie );
	
	mad->callback = func;
	mad->data = data;
	
	mad->url = g_strdup( SOAP_AUTHENTICATION_URL );
	mad->ttl = 3; /* Max. # of redirects. */
	
	/* HTTP-escape stuff and s/,/&/ */
	http_decode( mad->cookie );
	for( i = 0; mad->cookie[i]; i ++ )
		if( mad->cookie[i] == ',' )
			mad->cookie[i] = '&';
	
	/* Microsoft doesn't allow password longer than 16 chars and silently
	   fails authentication if you give the "full version" of your passwd. */
	if( strlen( mad->password ) > MAX_PASSPORT_PWLEN )
		mad->password[MAX_PASSPORT_PWLEN] = 0;
	
	return passport_get_token_real( mad );
}

static int passport_get_token_real( struct msn_auth_data *mad )
{
	char *post_payload, *post_request;
	struct http_request *req;
	url_t url;
	
	url_set( &url, mad->url );
	
	post_payload = g_markup_printf_escaped( SOAP_AUTHENTICATION_PAYLOAD,
	                                        mad->username,
	                                        mad->password,
	                                        mad->cookie );
	
	post_request = g_strdup_printf( SOAP_AUTHENTICATION_REQUEST,
	                                url.file, url.host,
	                                (int) strlen( post_payload ),
	                                post_payload );
	                                
	req = http_dorequest( url.host, url.port, 1, post_request,
	                      passport_get_token_ready, mad );
	
	g_free( post_request );
	g_free( post_payload );
	
	return req != NULL;
}

static xt_status passport_xt_extract_token( struct xt_node *node, gpointer data );
static xt_status passport_xt_handle_fault( struct xt_node *node, gpointer data );

static const struct xt_handler_entry passport_xt_handlers[] = {
	{ "wsse:BinarySecurityToken", "wst:RequestedSecurityToken", passport_xt_extract_token },
	{ "S:Fault",                  "S:Envelope",                 passport_xt_handle_fault  },
	{ NULL,                       NULL,                         NULL                      }
};

static void passport_get_token_ready( struct http_request *req )
{
	struct msn_auth_data *mad = req->data;
	struct xt_parser *parser;
	
	g_free( mad->url );
	g_free( mad->error );
	mad->url = mad->error = NULL;
	
	if( req->status_code == 200 )
	{
		parser = xt_new( passport_xt_handlers, mad );
		xt_feed( parser, req->reply_body, req->body_size );
		xt_handle( parser, NULL, -1 );
		xt_free( parser );
	}
	else
	{
		mad->error = g_strdup_printf( "HTTP error %d (%s)", req->status_code,
		                              req->status_string ? req->status_string : "unknown" );
	}
	
	if( mad->error == NULL && mad->token == NULL )
		mad->error = g_strdup( "Could not parse Passport server response" );
	
	if( mad->url && mad->token == NULL )
	{
		passport_get_token_real( mad );
	}
	else
	{
		mad->callback( mad );
		
		g_free( mad->url );
		g_free( mad->username );
		g_free( mad->password );
		g_free( mad->cookie );
		g_free( mad->token );
		g_free( mad->error );
		g_free( mad );
	}
}

static xt_status passport_xt_extract_token( struct xt_node *node, gpointer data )
{
	struct msn_auth_data *mad = data;
	char *s;
	
	if( ( s = xt_find_attr( node, "Id" ) ) &&
	    ( strncmp( s, "Compact", 7 ) == 0 ||
	      strncmp( s, "PPToken", 7 ) == 0 ) )
		mad->token = g_memdup( node->text, node->text_len + 1 );
	
	return XT_HANDLED;
}

static xt_status passport_xt_handle_fault( struct xt_node *node, gpointer data )
{
	struct msn_auth_data *mad = data;
	struct xt_node *code = xt_find_node( node->children, "faultcode" );
	struct xt_node *string = xt_find_node( node->children, "faultstring" );
	struct xt_node *redirect = xt_find_node( node->children, "psf:redirectUrl" );
	
	if( redirect && redirect->text_len && mad->ttl-- > 0 )
		mad->url = g_memdup( redirect->text, redirect->text_len + 1 );
	
	if( code == NULL || code->text_len == 0 )
		mad->error = g_strdup( "Unknown error" );
	else
		mad->error = g_strdup_printf( "%s (%s)", code->text, string && string->text_len ?
		                              string->text : "no description available" );
	
	return XT_HANDLED;
}
