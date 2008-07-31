/* passport.c
 *
 * Functions to login to microsoft passport service for Messenger
 * Copyright (C) 2004 Wouter Paesen <wouter@blue-gate.be>
 * Copyright (C) 2004 Wilmer van der Gaast <wilmer@gaast.net>
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
#include <ctype.h>
#include <errno.h>

#define MSN_BUF_LEN 8192

static char *prd_cached = NULL;

static int passport_get_id_real( gpointer func, gpointer data, char *header );
static void passport_get_id_ready( struct http_request *req );

static int passport_retrieve_dalogin( gpointer data, gpointer func, char *header );
static void passport_retrieve_dalogin_ready( struct http_request *req );

static char *passport_create_header( char *cookie, char *email, char *pwd );
static void destroy_reply( struct passport_reply *rep );

int passport_get_id( gpointer func, gpointer data, char *username, char *password, char *cookie )
{
	char *header = passport_create_header( cookie, username, password );
	
	if( prd_cached == NULL )
		return passport_retrieve_dalogin( func, data, header );
	else
		return passport_get_id_real( func, data, header );
}

static int passport_get_id_real( gpointer func, gpointer data, char *header )
{
	struct passport_reply *rep;
	char *server, *dummy, *reqs;
	struct http_request *req;
	
	rep = g_new0( struct passport_reply, 1 );
	rep->data = data;
	rep->func = func;
	rep->header = header;
	
	server = g_strdup( prd_cached );
	dummy = strchr( server, '/' );
	
	if( dummy == NULL )
	{
		destroy_reply( rep );
		return( 0 );
	}
	
	reqs = g_strdup_printf( "GET %s HTTP/1.0\r\n%s\r\n\r\n", dummy, header );
	
	*dummy = 0;
	req = http_dorequest( server, 443, 1, reqs, passport_get_id_ready, rep );
	
	g_free( server );
	g_free( reqs );
	
	if( req == NULL )
		destroy_reply( rep );
	
	return( req != NULL );
}

static void passport_get_id_ready( struct http_request *req )
{
	struct passport_reply *rep = req->data;
	
	if( !g_slist_find( msn_connections, rep->data ) )
	{
		destroy_reply( rep );
		return;
	}
	
	if( req->finished && req->reply_headers && req->status_code == 200 )
	{
		char *dummy;
		
		if( ( dummy = strstr( req->reply_headers, "from-PP='" ) ) )
		{
			char *responseend;
			
			dummy += strlen( "from-PP='" );
			responseend = strchr( dummy, '\'' );
			if( responseend )
				*responseend = 0;
			
			rep->result = g_strdup( dummy );
		}
		else
		{
			rep->error_string = g_strdup( "Could not parse Passport server response" );
		}
	}
	else
	{
		rep->error_string = g_strdup_printf( "HTTP error: %s",
		                      req->status_string ? req->status_string : "Unknown error" );
	}
	
	rep->func( rep );
	destroy_reply( rep );
}

static char *passport_create_header( char *cookie, char *email, char *pwd )
{
	char *buffer;
	char *currenttoken;
	char *email_enc, *pwd_enc;
	
	currenttoken = strstr( cookie, "lc=" );
	if( currenttoken == NULL )
		return NULL;
	
	email_enc = g_new0( char, strlen( email ) * 3 + 1 );
	strcpy( email_enc, email );
	http_encode( email_enc );
	
	pwd_enc = g_new0( char, strlen( pwd ) * 3 + 1 );
	g_snprintf( pwd_enc, 17, "%s", pwd ); /* Passwords >16 chars never succeed. (Bug #360) */
	strcpy( pwd_enc, pwd );
	http_encode( pwd_enc );
	
	buffer = g_strdup_printf( "Authorization: Passport1.4 OrgVerb=GET,"
	                          "OrgURL=http%%3A%%2F%%2Fmessenger%%2Emsn%%2Ecom,"
	                          "sign-in=%s,pwd=%s,%s", email_enc, pwd_enc,
	                          currenttoken );
	
	g_free( email_enc );
	g_free( pwd_enc );
	
	return buffer;
}

static int passport_retrieve_dalogin( gpointer func, gpointer data, char *header )
{
	struct passport_reply *rep = g_new0( struct passport_reply, 1 );
	struct http_request *req;
	
	rep->data = data;
	rep->func = func;
	rep->header = header;
	
	req = http_dorequest_url( "https://nexus.passport.com/rdr/pprdr.asp", passport_retrieve_dalogin_ready, rep );
	
	if( !req )
		destroy_reply( rep );
	
	return( req != NULL );
}

static void passport_retrieve_dalogin_ready( struct http_request *req )
{
	struct passport_reply *rep = req->data;
	char *dalogin;
	char *urlend;
	
	if( !g_slist_find( msn_connections, rep->data ) )
	{
		destroy_reply( rep );
		return;
	}
	
	if( !req->finished || !req->reply_headers || req->status_code != 200 )
	{
		rep->error_string = g_strdup_printf( "HTTP error while fetching DALogin: %s",
		                        req->status_string ? req->status_string : "Unknown error" );
		goto failure;
	}
	
	dalogin = strstr( req->reply_headers, "DALogin=" );	
	
	if( !dalogin )
	{
		rep->error_string = g_strdup( "Parse error while fetching DALogin" );
		goto failure;
	}
	
	dalogin += strlen( "DALogin=" );
	urlend = strchr( dalogin, ',' );
	if( urlend )
		*urlend = 0;
	
	/* strip the http(s):// part from the url */
	urlend = strstr( urlend, "://" );
	if( urlend )
		dalogin = urlend + strlen( "://" );
	
	if( prd_cached == NULL )
		prd_cached = g_strdup( dalogin );
	
	if( passport_get_id_real( rep->func, rep->data, rep->header ) )
	{
		rep->header = NULL;
		destroy_reply( rep );
		return;
	}
	
failure:	
	rep->func( rep );
	destroy_reply( rep );
}

static void destroy_reply( struct passport_reply *rep )
{
	g_free( rep->result );
	g_free( rep->header );
	g_free( rep->error_string );
	g_free( rep );
}
