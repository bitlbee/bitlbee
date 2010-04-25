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
#include "base64.h"
#include "misc.h"
#include "sha1.h"

#define CONSUMER_KEY "xsDNKJuNZYkZyMcu914uEA"
#define CONSUMER_SECRET "FCxqcr0pXKzsF9ajmP57S3VQ8V6Drk4o2QYtqMcOszo"
/* How can it be a secret if it's right here in the source code? No clue... */

#define HMAC_BLOCK_SIZE 64

struct oauth_state
{
};

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
	i = strlen( CONSUMER_SECRET ) + 1 + token_secret ? strlen( token_secret ) : 0;
	if( i > HMAC_BLOCK_SIZE )
	{
		sha1_init( &sha1 );
		sha1_append( &sha1, CONSUMER_SECRET, strlen( CONSUMER_SECRET ) );
		sha1_append( &sha1, "&", 1 );
		if( token_secret )
			sha1_append( &sha1, token_secret, strlen( token_secret ) );
		sha1_finish( &sha1, key );
	}
	else
	{
		g_snprintf( key, HMAC_BLOCK_SIZE + 1, "%s&%s",
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
	
	/* base64_encode it and we're done. */
	return base64_encode( hash, sha1_hash_size );
}
