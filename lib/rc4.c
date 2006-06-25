/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Simple (but secure) RC4 implementation for safer password storage.       *
*                                                                           *
*  Copyright 2006 Wilmer van der Gaast <wilmer@gaast.net>                   *
*                                                                           *
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

/* 
   This file implements RC4-encryption, which will mainly be used to save IM
   passwords safely in the new XML-format. Possibly other uses will come up
   later. It's supposed to be quite reliable (thanks to the use of a 6-byte
   IV/seed), certainly compared to the old format. The only realistic way to
   crack BitlBee passwords now is to use a sniffer to get your hands on the
   user's password.
   
   If you see that something's wrong in this implementation (I asked a
   couple of people to look at it already, but who knows), please tell me.
   
   The reason I chose for RC4 is because it's pretty simple but effective,
   so it will work without adding several KBs or an extra library dependency.
*/


#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include "rc4.h"

/* Add some seed to the password, to make sure we *never* use the same key.
   This defines how many byes we use as a seed. */
#define RC4_IV_LEN 6

/* To defend against a "Fluhrer, Mantin and Shamir attack", it is recommended
   to shuffle S[] just a bit more before you start to use it. This defines how
   many bytes we'll request before we'll really use them for encryption. */
#define RC4_CYCLES 1024

struct rc4_state *rc4_keymaker( unsigned char *key, int kl, int cycles )
{
	struct rc4_state *st;
	int i, j, tmp;
	
	st = g_malloc( sizeof( struct rc4_state ) );
	st->i = st->j = 0;
	for( i = 0; i < 256; i ++ )
		st->S[i] = i;
	
	if( kl <= 0 )
		kl = strlen( (char*) key );
	
	for( i = j = 0; i < 256; i ++ )
	{
		j = ( j + st->S[i] + key[i%kl] ) & 0xff;
		tmp = st->S[i];
		st->S[i] = st->S[j];
		st->S[j] = tmp;
	}
	
	for( i = 0; i < cycles; i ++ )
		rc4_getbyte( st );
	
	return st;
}

/*
   For those who don't know, RC4 is basically an algorithm that generates a
   stream of bytes after you give it a key. Just get a byte from it and xor
   it with your cleartext. To decrypt, just give it the same key again and
   start xorring.
   
   The function above initializes the RC4 byte generator, the next function
   can be used to get bytes from the generator (and shuffle things a bit).
*/

unsigned char rc4_getbyte( struct rc4_state *st )
{
	unsigned char tmp;
	
	/* Unfortunately the st-> stuff doesn't really improve readability here... */
	st->i ++;
	st->j += st->S[st->i];
	tmp = st->S[st->i];
	st->S[st->i] = st->S[st->j];
	st->S[st->j] = tmp;
	
	return st->S[(st->S[st->i] + st->S[st->j]) & 0xff];
}

/*
   The following two functions can be used for reliable encryption and
   decryption. Known plaintext attacks are prevented by adding some (6,
   by default) random bytes to the password before setting up the RC4
   structures. These 6 bytes are also saved in the results, because of
   course we'll need them in rc4_decode().
   
   Because the length of the resulting string is unknown to the caller,
   it should pass a char**. Since the encode/decode functions allocate
   memory for the string, make sure the char** points at a NULL-pointer
   (or at least to something you already free()d), or you'll leak
   memory. And of course, don't forget to free() the result when you
   don't need it anymore.
   
   Both functions return the number of bytes in the result string.
*/

int rc4_encode( unsigned char *clear, int clear_len, unsigned char **crypt, char *password )
{
	struct rc4_state *st;
	unsigned char *key;
	int key_len, i;
	
	key_len = strlen( password ) + RC4_IV_LEN;
	if( clear_len <= 0 )
		clear_len = strlen( (char*) clear );
	
	/* Prepare buffers and the key + IV */
	*crypt = g_malloc( clear_len + RC4_IV_LEN );
	key = g_malloc( key_len );
	strcpy( (char*) key, password );
	for( i = 0; i < RC4_IV_LEN; i ++ )
		key[key_len-RC4_IV_LEN+i] = crypt[0][i] = rand() & 0xff;
	
	/* Generate the initial S[] from the IVed key. */
	st = rc4_keymaker( key, key_len, RC4_CYCLES );
	g_free( key );
	
	for( i = 0; i < clear_len; i ++ )
		crypt[0][i+RC4_IV_LEN] = clear[i] ^ rc4_getbyte( st );
	
	g_free( st );
	
	return clear_len + RC4_IV_LEN;
}

int rc4_decode( unsigned char *crypt, int crypt_len, unsigned char **clear, char *password )
{
	struct rc4_state *st;
	unsigned char *key;
	int key_len, clear_len, i;
	
	key_len = strlen( password ) + RC4_IV_LEN;
	clear_len = crypt_len - RC4_IV_LEN;
	
	/* Prepare buffers and the key + IV */
	*clear = g_malloc( clear_len + 1 );
	key = g_malloc( key_len );
	strcpy( (char*) key, password );
	for( i = 0; i < RC4_IV_LEN; i ++ )
		key[key_len-RC4_IV_LEN+i] = crypt[i];
	
	/* Generate the initial S[] from the IVed key. */
	st = rc4_keymaker( key, key_len, RC4_CYCLES );
	g_free( key );
	
	for( i = 0; i < clear_len; i ++ )
		clear[0][i] = crypt[i+RC4_IV_LEN] ^ rc4_getbyte( st );
	clear[0][i] = 0; /* Nice to have for plaintexts. */
	
	g_free( st );
	
	return clear_len;
}
