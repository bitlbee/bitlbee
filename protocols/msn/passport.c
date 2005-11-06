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

#include "ssl_client.h"
#include "passport.h"
#include "msn.h"
#include "bitlbee.h"
#include <ctype.h>
#include <errno.h>

#define MSN_BUF_LEN 8192

static char *prd_cached = NULL;

static char *passport_create_header( char *reply, char *email, char *pwd );
static int passport_retrieve_dalogin( gpointer data, gpointer func, char *header );
static void passport_retrieve_dalogin_connected( gpointer data, void *ssl, GaimInputCondition cond );
static int passport_get_id_from( gpointer data, gpointer func, char *header_i, char *url );
static void passport_get_id_connected( gpointer data, void *ssl, GaimInputCondition cond );
static void destroy_reply( struct passport_reply *rep );


int passport_get_id( gpointer data, char *username, char *password, char *cookie, gpointer func )
{
	char *header = passport_create_header( cookie, username, password );
	
	if( prd_cached )
	{
		int st;
		
		st = passport_get_id_from( data, func, header, prd_cached );
		g_free( header );
		return( st );
	}
	else
	{
		return( passport_retrieve_dalogin( data, func, header ) );
	}
}


static char *passport_create_header( char *reply, char *email, char *pwd )
{
	char *buffer = g_new0( char, 2048 );
	char *currenttoken;
	char *email_enc, *pwd_enc;
	
	email_enc = g_new0( char, strlen( email ) * 3 + 1 );
	strcpy( email_enc, email );
	http_encode( email_enc );
	
	pwd_enc = g_new0( char, strlen( pwd ) * 3 + 1 );
	strcpy( pwd_enc, pwd );
	http_encode( pwd_enc );
	
	currenttoken = strstr( reply, "lc=" );
	if( currenttoken == NULL )
		return( NULL );
	
	g_snprintf( buffer, 2048,
	            "Authorization: Passport1.4 OrgVerb=GET,"
	            "OrgURL=http%%3A%%2F%%2Fmessenger%%2Emsn%%2Ecom,"
	            "sign-in=%s,pwd=%s,%s", email_enc, pwd_enc,
	            currenttoken );
	
	g_free( email_enc );
	g_free( pwd_enc );
	
	return( buffer );
}


static int passport_retrieve_dalogin( gpointer data, gpointer func, char *header )
{
	struct passport_reply *rep = g_new0( struct passport_reply, 1 );
	void *ssl;
	
	rep->data = data;
	rep->func = func;
	rep->header = header;
	
	ssl = ssl_connect( "nexus.passport.com", 443, passport_retrieve_dalogin_connected, rep );
	
	if( !ssl )
		destroy_reply( rep );
	
	return( ssl != NULL );
}

#define PPR_BUFFERSIZE 2048
#define PPR_REQUEST "GET /rdr/pprdr.asp HTTP/1.0\r\n\r\n"
static void passport_retrieve_dalogin_connected( gpointer data, void *ssl, GaimInputCondition cond )
{
	int ret;
	char buffer[PPR_BUFFERSIZE+1];
	struct passport_reply *rep = data;
	
	if( !g_slist_find( msn_connections, rep->data ) )
	{
		if( ssl ) ssl_disconnect( ssl );
		destroy_reply( rep );
		return;
	}
	
	if( !ssl )
	{
		rep->func( rep );
		destroy_reply( rep );
		return;
	}
	
	ssl_write( ssl, PPR_REQUEST, strlen( PPR_REQUEST ) );
	
	if( ( ret = ssl_read( ssl, buffer, PPR_BUFFERSIZE ) ) <= 0 )
	{
		goto failure;
	}

	{
		char *dalogin = strstr( buffer, "DALogin=" );
		char *urlend;
		
		if( !dalogin )
			goto failure;
		
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
	}
	
	if( passport_get_id_from( rep->data, rep->func, rep->header, prd_cached ) )
	{
		ssl_disconnect( ssl );
		destroy_reply( rep );
		return;
	}
	
failure:	
	ssl_disconnect( ssl );
	rep->func( rep );
	destroy_reply( rep );
}


static int passport_get_id_from( gpointer data, gpointer func, char *header_i, char *url )
{
	struct passport_reply *rep = g_new0( struct passport_reply, 1 );
	char server[512], *dummy;
	void *ssl;
	
	rep->data = data;
	rep->func = func;
	rep->redirects = 4;
	
	strncpy( server, url, 512 );
	dummy = strchr( server, '/' );
	if( dummy )
		*dummy = 0;
	
	ssl = ssl_connect( server, 443, passport_get_id_connected, rep );
	
	if( ssl )
	{
		rep->header = g_strdup( header_i );
		rep->url = g_strdup( url );
	}
	else
	{
		destroy_reply( rep );
	}
	
	return( ssl != NULL );
}

#define PPG_BUFFERSIZE 4096
static void passport_get_id_connected( gpointer data, void *ssl, GaimInputCondition cond )
{
	struct passport_reply *rep = data;
	char server[512], buffer[PPG_BUFFERSIZE+1], *dummy;
	int ret;
	
	if( !g_slist_find( msn_connections, rep->data ) )
	{
		if( ssl ) ssl_disconnect( ssl );
		destroy_reply( rep );
		return;
	}
	
	if( !ssl )
	{
		rep->func( rep );
		destroy_reply( rep );
		return;
	}
	
	memset( buffer, 0, PPG_BUFFERSIZE + 1 );
	
	strncpy( server, rep->url, 512 );
	dummy = strchr( server, '/' );
	if( dummy == NULL )
		goto end;
	
	g_snprintf( buffer, PPG_BUFFERSIZE - 1, "GET %s HTTP/1.0\r\n"
	            "%s\r\n\r\n", dummy, rep->header );
	
	ssl_write( ssl, buffer, strlen( buffer ) );
	memset( buffer, 0, PPG_BUFFERSIZE + 1 );
	
	{
		char *buffer2 = buffer;
		
		while( ( ( ret = ssl_read( ssl, buffer2, 512 ) ) > 0 ) &&
		       ( buffer + PPG_BUFFERSIZE - buffer2 - ret - 512 >= 0 ) )
		{
			buffer2 += ret;
		}
	}
	
	if( *buffer == 0 )
		goto end;
	
	if( ( dummy = strstr( buffer, "Location:" ) ) )
	{
		char *urlend;
		
		rep->redirects --;
		if( rep->redirects == 0 )
			goto end;
		
		dummy += strlen( "Location:" );
		while( isspace( *dummy ) ) dummy ++;
		urlend = dummy;
		while( !isspace( *urlend ) ) urlend ++;
		*urlend = 0;
		if( ( urlend = strstr( dummy, "://" ) ) )
			dummy = urlend + strlen( "://" );
		
		g_free( rep->url );
		rep->url = g_strdup( dummy );
		
		strncpy( server, dummy, sizeof( server ) - 1 );
		dummy = strchr( server, '/' );
		if( dummy ) *dummy = 0;
		
		ssl_disconnect( ssl );
		
		if( ssl_connect( server, 443, passport_get_id_connected, rep ) )
		{
			return;
		}
		else
		{
			rep->func( rep );
			destroy_reply( rep );
			return;
		}
	}
	else if( strstr( buffer, "200 OK" ) )
	{
		if( ( dummy = strstr( buffer, "from-PP='" ) ) )
		{
			char *responseend;
			
			dummy += strlen( "from-PP='" );
			responseend = strchr( dummy, '\'' );
			if( responseend )
				*responseend = 0;
			
			rep->result = g_strdup( dummy );
		}
	}
	
end:
	ssl_disconnect( ssl );
	rep->func( rep );
	destroy_reply( rep );
}


static void destroy_reply( struct passport_reply *rep )
{
	if( rep->result ) g_free( rep->result );
	if( rep->url ) g_free( rep->url );
	if( rep->header ) g_free( rep->header );
	if( rep ) g_free( rep );
}
