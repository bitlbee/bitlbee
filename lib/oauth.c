/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Simple OAuth client (consumer) implementation.                           *
*                                                                           *
*  Copyright 2010 Wilmer van der Gaast <wilmer@gaast.net>                   *
*                                                                           *
*  This library is free software; you can redistribute it and/or            *
*  modify it under the terms of the GNU Lesser General Public               *
*  License as published by the Free Software Foundation, version            *
*  2.1.                                                                     *
*                                                                           *
*  This library is distributed in the hope that it will be useful,          *
*  but WITHOUT ANY WARRANTY; without even the implied warranty of           *
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU        *
*  Lesser General Public License for more details.                          *
*                                                                           *
*  You should have received a copy of the GNU Lesser General Public License *
*  along with this library; if not, write to the Free Software Foundation,  *
*  Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA           *
*                                                                           *
\***************************************************************************/

#include <glib.h>
#include <gmodule.h>
#include <stdlib.h>
#include <string.h>
#include "http_client.h"
#include "base64.h"
#include "misc.h"
#include "sha1.h"
#include "url.h"
#include "oauth.h"

#define CONSUMER_KEY "xsDNKJuNZYkZyMcu914uEA"
#define CONSUMER_SECRET "FCxqcr0pXKzsF9ajmP57S3VQ8V6Drk4o2QYtqMcOszo"
/* How can it be a secret if it's right here in the source code? No clue... */

#define HMAC_BLOCK_SIZE 64

static char *oauth_sign( const char *method, const char *url,
                         const char *params, const char *token_secret )
{
	sha1_state_t sha1;
	uint8_t hash[sha1_hash_size];
	uint8_t key[HMAC_BLOCK_SIZE+1];
	char *s;
	int i;
	
	/* Create K. If our current key is >64 chars we have to hash it,
	   otherwise just pad. */
	memset( key, 0, HMAC_BLOCK_SIZE );
	i = strlen( CONSUMER_SECRET ) + 1 + ( token_secret ? strlen( token_secret ) : 0 );
	if( i > HMAC_BLOCK_SIZE )
	{
		sha1_init( &sha1 );
		sha1_append( &sha1, (uint8_t*) CONSUMER_SECRET, strlen( CONSUMER_SECRET ) );
		sha1_append( &sha1, (uint8_t*) "&", 1 );
		if( token_secret )
			sha1_append( &sha1, (uint8_t*) token_secret, strlen( token_secret ) );
		sha1_finish( &sha1, key );
	}
	else
	{
		g_snprintf( (gchar*) key, HMAC_BLOCK_SIZE + 1, "%s&%s",
		            CONSUMER_SECRET, token_secret ? : "" );
	}
	
	/* Inner part: H(K XOR 0x36, text) */
	sha1_init( &sha1 );
	
	for( i = 0; i < HMAC_BLOCK_SIZE; i ++ )
		key[i] ^= 0x36;
	sha1_append( &sha1, key, HMAC_BLOCK_SIZE );
	
	/* OAuth: text = method&url&params, all http_encoded. */
	sha1_append( &sha1, (const uint8_t*) method, strlen( method ) );
	sha1_append( &sha1, (const uint8_t*) "&", 1 );
	
	s = g_new0( char, strlen( url ) * 3 + 1 );
	strcpy( s, url );
	http_encode( s );
	sha1_append( &sha1, (const uint8_t*) s, strlen( s ) );
	sha1_append( &sha1, (const uint8_t*) "&", 1 );
	g_free( s );
	
	s = g_new0( char, strlen( params ) * 3 + 1 );
	strcpy( s, params );
	http_encode( s );
	sha1_append( &sha1, (const uint8_t*) s, strlen( s ) );
	g_free( s );
	
	sha1_finish( &sha1, hash );
	
	/* Final result: H(K XOR 0x5C, inner stuff) */
	sha1_init( &sha1 );
	for( i = 0; i < HMAC_BLOCK_SIZE; i ++ )
		key[i] ^= 0x36 ^ 0x5c;
	sha1_append( &sha1, key, HMAC_BLOCK_SIZE );
	sha1_append( &sha1, hash, sha1_hash_size );
	sha1_finish( &sha1, hash );
	
	/* base64_encode + HTTP escape it (both consumers 
	   need it that away) and we're done. */
	s = base64_encode( hash, sha1_hash_size );
	s = g_realloc( s, strlen( s ) * 3 + 1 );
	http_encode( s );
	
	return s;
}

static char *oauth_nonce()
{
	unsigned char bytes[9];
	
	random_bytes( bytes, sizeof( bytes ) );
	return base64_encode( bytes, sizeof( bytes ) );
}

void oauth_params_add( GSList **params, const char *key, const char *value )
{
	char *item;
	
	item = g_strdup_printf( "%s=%s", key, value );
	*params = g_slist_insert_sorted( *params, item, (GCompareFunc) strcmp );
}

void oauth_params_del( GSList **params, const char *key )
{
	int key_len = strlen( key );
	GSList *l, *n;
	
	for( l = *params; l; l = n )
	{
		n = l->next;
		
		if( strncmp( (char*) l->data, key, key_len ) == 0 &&
		    ((char*)l->data)[key_len] == '=' )
		{
			g_free( l->data );
			*params = g_slist_remove( *params, l->data );
		}
	}
}

void oauth_params_set( GSList **params, const char *key, const char *value )
{
	oauth_params_del( params, key );
	oauth_params_add( params, key, value );
}

const char *oauth_params_get( GSList **params, const char *key )
{
	int key_len = strlen( key );
	GSList *l;
	
	for( l = *params; l; l = l->next )
	{
		if( strncmp( (char*) l->data, key, key_len ) == 0 &&
		    ((char*)l->data)[key_len] == '=' )
			return (const char*) l->data + key_len + 1;
	}
	
	return NULL;
}

static void oauth_params_parse( GSList **params, char *in )
{
	char *amp, *eq, *s;
	
	while( in && *in )
	{
		eq = strchr( in, '=' );
		if( !eq )
			break;
		
		*eq = '\0';
		if( ( amp = strchr( eq + 1, '&' ) ) )
			*amp = '\0';
		
		s = g_strdup( eq + 1 );
		http_decode( s );
		oauth_params_add( params, in, s );
		g_free( s );
		
		*eq = '=';
		if( amp == NULL )
			break;
		
		*amp = '&';
		in = amp + 1;
	}
}

void oauth_params_free( GSList **params )
{
	while( params && *params )
	{
		g_free( (*params)->data );
		*params = g_slist_remove( *params, (*params)->data );
	}
}

char *oauth_params_string( GSList *params )
{
	GSList *l;
	GString *str = g_string_new( "" );
	
	for( l = params; l; l = l->next )
	{
		char *s, *eq;
		
		s = g_malloc( strlen( l->data ) * 3 + 1 );
		strcpy( s, l->data );
		if( ( eq = strchr( s, '=' ) ) )
			http_encode( eq + 1 );
		g_string_append( str, s );
		g_free( s );
		
		if( l->next )
			g_string_append_c( str, '&' );
	}
	
	return g_string_free( str, FALSE );
}

void oauth_info_free( struct oauth_info *info )
{
	if( info )
	{
		g_free( info->auth_params );
		g_free( info->request_token );
		g_free( info->access_token );
		g_free( info );
	}
}

static void oauth_add_default_params( GSList **params )
{
	char *s;
	
	oauth_params_set( params, "oauth_consumer_key", CONSUMER_KEY );
	oauth_params_set( params, "oauth_signature_method", "HMAC-SHA1" );
	
	s = g_strdup_printf( "%d", (int) time( NULL ) );
	oauth_params_set( params, "oauth_timestamp", s );
	g_free( s );
	
	s = oauth_nonce();
	oauth_params_set( params, "oauth_nonce", s );
	g_free( s );
	
	oauth_params_set( params, "oauth_version", "1.0" );
}

static void *oauth_post_request( const char *url, GSList **params_, http_input_function func, void *data )
{
	GSList *params = NULL;
	char *s, *params_s, *post;
	void *req;
	url_t url_p;
	
	if( !url_set( &url_p, url ) )
	{
		oauth_params_free( params_ );
		return NULL;
	}
	
	if( params_ )
		params = *params_;
	
	oauth_add_default_params( &params );
	
	params_s = oauth_params_string( params );
	oauth_params_free( params_ );
	
	s = oauth_sign( "POST", url, params_s, NULL );
	post = g_strdup_printf( "%s&oauth_signature=%s", params_s, s );
	g_free( params_s );
	g_free( s );
	
	s = g_strdup_printf( "POST %s HTTP/1.0\r\n"
	                     "Host: %s\r\n"
	                     "Content-Type: application/x-www-form-urlencoded\r\n"
	                     "Content-Length: %zd\r\n"
	                     "\r\n"
	                     "%s", url_p.file, url_p.host, strlen( post ), post );
	g_free( post );
	
	req = http_dorequest( url_p.host, url_p.port, url_p.proto == PROTO_HTTPS,
	                      s, func, data );
	g_free( s );
	
	return req;
}

static void oauth_request_token_done( struct http_request *req );

struct oauth_info *oauth_request_token( const char *url, oauth_cb func, void *data )
{
	struct oauth_info *st = g_new0( struct oauth_info, 1 );
	GSList *params = NULL;
	
	st->func = func;
	st->data = data;
	
	oauth_params_add( &params, "oauth_callback", "oob" );
	
	if( !oauth_post_request( url, &params, oauth_request_token_done, st ) )
	{
		oauth_info_free( st );
		return NULL;
	}
	
	return st;
}

static void oauth_request_token_done( struct http_request *req )
{
	struct oauth_info *st = req->data;
	
	st->http = req;
	
	if( req->status_code == 200 )
	{
		GSList *params = NULL;
		
		st->auth_params = g_strdup( req->reply_body );
		oauth_params_parse( &params, st->auth_params );
		st->request_token = g_strdup( oauth_params_get( &params, "oauth_token" ) );
		oauth_params_free( &params );
	}
	
	st->stage = OAUTH_REQUEST_TOKEN;
	if( !st->func( st ) )
		oauth_info_free( st );
}

static void oauth_access_token_done( struct http_request *req );

void oauth_access_token( const char *url, const char *pin, struct oauth_info *st )
{
	GSList *params = NULL;
	
	oauth_params_add( &params, "oauth_token", st->request_token );
	oauth_params_add( &params, "oauth_verifier", pin );
	
	if( !oauth_post_request( url, &params, oauth_access_token_done, st ) )
		oauth_info_free( st );
}

static void oauth_access_token_done( struct http_request *req )
{
	struct oauth_info *st = req->data;
	
	if( req->status_code == 200 )
	{
		GSList *params = NULL;
		const char *token, *token_secret;
		
		oauth_params_parse( &params, req->reply_body );
		token = oauth_params_get( &params, "oauth_token" );
		token_secret = oauth_params_get( &params, "oauth_token_secret" );
		st->access_token = g_strdup_printf(
			"oauth_token=%s&oauth_token_secret=%s", token, token_secret );
		oauth_params_free( &params );
	}
	
	st->stage = OAUTH_ACCESS_TOKEN;
	st->func( st );
	oauth_info_free( st );
}

char *oauth_http_header( char *access_token, const char *method, const char *url, char *args )
{
	GSList *params = NULL, *l;
	char *token_secret, *sig, *params_s, *s;
	GString *ret = NULL;
	
	/* First, get the two pieces of info from the access token that we need. */
	oauth_params_parse( &params, access_token );
	if( params == NULL )
		goto err;
	
	/* Pick out the token secret, we shouldn't include it but use it for signing. */
	token_secret = g_strdup( oauth_params_get( &params, "oauth_token_secret" ) );
	if( token_secret == NULL )
		goto err;
	oauth_params_del( &params, "oauth_token_secret" );
	
	oauth_add_default_params( &params );
	
	/* Start building the OAuth header. 'key="value", '... */
	ret = g_string_new( "OAuth " );
	for( l = params; l; l = l->next )
	{
		char *kv = l->data;
		char *eq = strchr( kv, '=' );
		char esc[strlen(kv)*3+1];
		
		if( eq == NULL )
			break; /* WTF */
		
		strcpy( esc, eq + 1 );
		http_encode( esc );
		
		g_string_append_len( ret, kv, eq - kv + 1 );
		g_string_append_c( ret, '"' );
		g_string_append( ret, esc );
		g_string_append( ret, "\", " );
	}
	
	/* Now, before generating the signature, add GET/POST arguments to params
	   since they should be included in the base signature string (but not in
	   the HTTP header). */
	if( args )
		oauth_params_parse( &params, args );
	if( ( s = strchr( url, '?' ) ) )
	{
		s = g_strdup( s + 1 );
		oauth_params_parse( &params, s + 1 );
		g_free( s );
	}
	
	/* Append the signature and we're done! */
	params_s = oauth_params_string( params );
	sig = oauth_sign( method, url, params_s, token_secret );
	g_string_append_printf( ret, "oauth_signature=\"%s\"", sig );
	g_free( params_s );
	
err:
	oauth_params_free( &params );
	g_free( sig );
	g_free( token_secret );
	
	return ret ? g_string_free( ret, FALSE ) : NULL;
}
