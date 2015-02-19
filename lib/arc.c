/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Simple (but secure) ArcFour implementation for safer password storage.   *
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

/*
   This file implements ArcFour-encryption, which will mainly be used to
   save IM passwords safely in the new XML-format. Possibly other uses will
   come up later. It's supposed to be quite reliable (thanks to the use of a
   6-byte IV/seed), certainly compared to the old format. The only realistic
   way to crack BitlBee passwords now is to use a sniffer to get your hands
   on the user's password.

   If you see that something's wrong in this implementation (I asked a
   couple of people to look at it already, but who knows), please tell me.

   The reason I picked ArcFour is because it's pretty simple but effective,
   so it will work without adding several KBs or an extra library dependency.

   (ArcFour is an RC4-compatible cipher. See for details:
   http://www.mozilla.org/projects/security/pki/nss/draft-kaukonen-cipher-arcfour-03.txt)
*/


#include <glib.h>
#include <gmodule.h>
#include <stdlib.h>
#include <string.h>
#include "misc.h"
#include "arc.h"

/* Add some seed to the password, to make sure we *never* use the same key.
   This defines how many bytes we use as a seed. */
#define ARC_IV_LEN 6

/* To defend against a "Fluhrer, Mantin and Shamir attack", it is recommended
   to shuffle S[] just a bit more before you start to use it. This defines how
   many bytes we'll request before we'll really use them for encryption. */
#define ARC_CYCLES 1024

struct arc_state *arc_keymaker(unsigned char *key, int kl, int cycles)
{
	struct arc_state *st;
	int i, j, tmp;
	unsigned char S2[256];

	st = g_malloc(sizeof(struct arc_state));
	st->i = st->j = 0;
	if (kl <= 0) {
		kl = strlen((char *) key);
	}

	for (i = 0; i < 256; i++) {
		st->S[i] = i;
		S2[i] = key[i % kl];
	}

	for (i = j = 0; i < 256; i++) {
		j = (j + st->S[i] + S2[i]) & 0xff;
		tmp = st->S[i];
		st->S[i] = st->S[j];
		st->S[j] = tmp;
	}

	memset(S2, 0, 256);
	i = j = 0;

	for (i = 0; i < cycles; i++) {
		arc_getbyte(st);
	}

	return st;
}

/*
   For those who don't know, ArcFour is basically an algorithm that generates
   a stream of bytes after you give it a key. Just get a byte from it and
   xor it with your cleartext. To decrypt, just give it the same key again
   and start xorring.

   The function above initializes the byte generator, the next function can
   be used to get bytes from the generator (and shuffle things a bit).
*/

unsigned char arc_getbyte(struct arc_state *st)
{
	unsigned char tmp;

	/* Unfortunately the st-> stuff doesn't really improve readability here... */
	st->i++;
	st->j += st->S[st->i];
	tmp = st->S[st->i];
	st->S[st->i] = st->S[st->j];
	st->S[st->j] = tmp;
	tmp = (st->S[st->i] + st->S[st->j]) & 0xff;

	return st->S[tmp];
}

/*
   The following two functions can be used for reliable encryption and
   decryption. Known plaintext attacks are prevented by adding some (6,
   by default) random bytes to the password before setting up the state
   structures. These 6 bytes are also saved in the results, because of
   course we'll need them in arc_decode().

   Because the length of the resulting string is unknown to the caller,
   it should pass a char**. Since the encode/decode functions allocate
   memory for the string, make sure the char** points at a NULL-pointer
   (or at least to something you already free()d), or you'll leak
   memory. And of course, don't forget to free() the result when you
   don't need it anymore.

   Both functions return the number of bytes in the result string.

   Note that if you use the pad_to argument, you will need zero-termi-
   nation to find back the original string length after decryption. So
   it shouldn't be used if your string contains \0s by itself!
*/

int arc_encode(char *clear, int clear_len, unsigned char **crypt, char *password, int pad_to)
{
	struct arc_state *st;
	unsigned char *key;
	char *padded = NULL;
	int key_len, i, padded_len;

	key_len = strlen(password) + ARC_IV_LEN;
	if (clear_len <= 0) {
		clear_len = strlen(clear);
	}

	/* Pad the string to the closest multiple of pad_to. This makes it
	   impossible to see the exact length of the password. */
	if (pad_to > 0 && (clear_len % pad_to) > 0) {
		padded_len = clear_len + pad_to - (clear_len % pad_to);
		padded = g_malloc(padded_len);
		memcpy(padded, clear, clear_len);

		/* First a \0 and then random data, so we don't have to do
		   anything special when decrypting. */
		padded[clear_len] = 0;
		random_bytes((unsigned char *) padded + clear_len + 1, padded_len - clear_len - 1);

		clear = padded;
		clear_len = padded_len;
	}

	/* Prepare buffers and the key + IV */
	*crypt = g_malloc(clear_len + ARC_IV_LEN);
	key = g_malloc(key_len);
	strcpy((char *) key, password);

	/* Add the salt. Save it for later (when decrypting) and, of course,
	   add it to the encryption key. */
	random_bytes(crypt[0], ARC_IV_LEN);
	memcpy(key + key_len - ARC_IV_LEN, crypt[0], ARC_IV_LEN);

	/* Generate the initial S[] from the IVed key. */
	st = arc_keymaker(key, key_len, ARC_CYCLES);
	g_free(key);

	for (i = 0; i < clear_len; i++) {
		crypt[0][i + ARC_IV_LEN] = clear[i] ^ arc_getbyte(st);
	}

	g_free(st);
	g_free(padded);

	return clear_len + ARC_IV_LEN;
}

int arc_decode(unsigned char *crypt, int crypt_len, char **clear, const char *password)
{
	struct arc_state *st;
	unsigned char *key;
	int key_len, clear_len, i;

	key_len = strlen(password) + ARC_IV_LEN;
	clear_len = crypt_len - ARC_IV_LEN;

	if (clear_len < 0) {
		*clear = g_strdup("");
		return -1;
	}

	/* Prepare buffers and the key + IV */
	*clear = g_malloc(clear_len + 1);
	key = g_malloc(key_len);
	strcpy((char *) key, password);
	for (i = 0; i < ARC_IV_LEN; i++) {
		key[key_len - ARC_IV_LEN + i] = crypt[i];
	}

	/* Generate the initial S[] from the IVed key. */
	st = arc_keymaker(key, key_len, ARC_CYCLES);
	g_free(key);

	for (i = 0; i < clear_len; i++) {
		clear[0][i] = crypt[i + ARC_IV_LEN] ^ arc_getbyte(st);
	}
	clear[0][i] = 0; /* Nice to have for plaintexts. */

	g_free(st);

	return clear_len;
}
