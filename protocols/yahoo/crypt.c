/* One way encryption based on MD5 sum.
   Copyright (C) 1996, 1997, 1999, 2000 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Ulrich Drepper <drepper@cygnus.com>, 1996.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, write to the Free
   Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307 USA.  */

/* warmenhoven took this file and made it work with the md5.[ch] we
 * already had. isn't that lovely. people should just use linux or
 * freebsd, crypt works properly on those systems. i hate solaris */

#if HAVE_CONFIG_H
#  include <config.h>
#endif

#if HAVE_STRING_H
#  include <string.h>
#elif HAVE_STRINGS_H
#  include <strings.h>
#endif

#include <stdlib.h>
#include "yahoo_util.h"

#include "md5.h"

/* Define our magic string to mark salt for MD5 "encryption"
   replacement.  This is meant to be the same as for other MD5 based
   encryption implementations.  */
static const char md5_salt_prefix[] = "$1$";

/* Table with characters for base64 transformation.  */
static const char b64t[64] =
"./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

char *yahoo_crypt(char *key, char *salt)
{
	char *buffer = NULL;
	int buflen = 0;
	int needed = 3 + strlen (salt) + 1 + 26 + 1;

	md5_byte_t alt_result[16];
	md5_state_t ctx;
	md5_state_t alt_ctx;
	size_t salt_len;
	size_t key_len;
	size_t cnt;
	char *cp;

	if (buflen < needed) {
		buflen = needed;
		if ((buffer = realloc(buffer, buflen)) == NULL)
			return NULL;
	}

	/* Find beginning of salt string.  The prefix should normally always
	   be present.  Just in case it is not.  */
	if (strncmp (md5_salt_prefix, salt, sizeof (md5_salt_prefix) - 1) == 0)
		/* Skip salt prefix.  */
		salt += sizeof (md5_salt_prefix) - 1;

	salt_len = MIN (strcspn (salt, "$"), 8);
	key_len = strlen (key);

	/* Prepare for the real work.  */
	md5_init(&ctx);

	/* Add the key string.  */
	md5_append(&ctx, (md5_byte_t *)key, key_len);

	/* Because the SALT argument need not always have the salt prefix we
	   add it separately.  */
	md5_append(&ctx, (md5_byte_t *)md5_salt_prefix, sizeof (md5_salt_prefix) - 1);

	/* The last part is the salt string.  This must be at most 8
	   characters and it ends at the first `$' character (for
	   compatibility which existing solutions).  */
	md5_append(&ctx, (md5_byte_t *)salt, salt_len);

	/* Compute alternate MD5 sum with input KEY, SALT, and KEY.  The
	   final result will be added to the first context.  */
	md5_init(&alt_ctx);

	/* Add key.  */
	md5_append(&alt_ctx, (md5_byte_t *)key, key_len);

	/* Add salt.  */
	md5_append(&alt_ctx, (md5_byte_t *)salt, salt_len);

	/* Add key again.  */
	md5_append(&alt_ctx, (md5_byte_t *)key, key_len);

	/* Now get result of this (16 bytes) and add it to the other
	   context.  */
	md5_finish(&alt_ctx, alt_result);

	/* Add for any character in the key one byte of the alternate sum.  */
	for (cnt = key_len; cnt > 16; cnt -= 16)
		md5_append(&ctx, alt_result, 16);
	md5_append(&ctx, alt_result, cnt);

	/* For the following code we need a NUL byte.  */
	alt_result[0] = '\0';

	/* The original implementation now does something weird: for every 1
	   bit in the key the first 0 is added to the buffer, for every 0
	   bit the first character of the key.  This does not seem to be
	   what was intended but we have to follow this to be compatible.  */
	for (cnt = key_len; cnt > 0; cnt >>= 1)
		md5_append(&ctx, (cnt & 1) != 0 ? alt_result : (md5_byte_t *)key, 1);

	/* Create intermediate result.  */
	md5_finish(&ctx, alt_result);

	/* Now comes another weirdness.  In fear of password crackers here
	   comes a quite long loop which just processes the output of the
	   previous round again.  We cannot ignore this here.  */
	for (cnt = 0; cnt < 1000; ++cnt) {
		/* New context.  */
		md5_init(&ctx);

		/* Add key or last result.  */
		if ((cnt & 1) != 0)
			md5_append(&ctx, (md5_byte_t *)key, key_len);
		else
			md5_append(&ctx, alt_result, 16);

		/* Add salt for numbers not divisible by 3.  */
		if (cnt % 3 != 0)
			md5_append(&ctx, (md5_byte_t *)salt, salt_len);

		/* Add key for numbers not divisible by 7.  */
		if (cnt % 7 != 0)
			md5_append(&ctx, (md5_byte_t *)key, key_len);

		/* Add key or last result.  */
		if ((cnt & 1) != 0)
			md5_append(&ctx, alt_result, 16);
		else
			md5_append(&ctx, (md5_byte_t *)key, key_len);

		/* Create intermediate result.  */
		md5_finish(&ctx, alt_result);
	}

	/* Now we can construct the result string.  It consists of three
	   parts.  */

	strncpy(buffer, md5_salt_prefix, MAX (0, buflen));
	cp = buffer + strlen(buffer);
	buflen -= sizeof (md5_salt_prefix);

	strncpy(cp, salt, MIN ((size_t) buflen, salt_len));
	cp = cp + strlen(cp);
	buflen -= MIN ((size_t) buflen, salt_len);

	if (buflen > 0) {
		*cp++ = '$';
		--buflen;
	}

#define b64_from_24bit(B2, B1, B0, N) \
	do { \
		unsigned int w = ((B2) << 16) | ((B1) << 8) | (B0); \
		int n = (N); \
		while (n-- > 0 && buflen > 0) { \
			*cp++ = b64t[w & 0x3f]; \
			--buflen; \
			w >>= 6; \
		}\
	} while (0)

	b64_from_24bit (alt_result[0], alt_result[6], alt_result[12], 4);
	b64_from_24bit (alt_result[1], alt_result[7], alt_result[13], 4);
	b64_from_24bit (alt_result[2], alt_result[8], alt_result[14], 4);
	b64_from_24bit (alt_result[3], alt_result[9], alt_result[15], 4);
	b64_from_24bit (alt_result[4], alt_result[10], alt_result[5], 4);
	b64_from_24bit (0, 0, alt_result[11], 2);
	if (buflen <= 0) {
		FREE(buffer);
	} else
		*cp = '\0';	/* Terminate the string.  */

	/* Clear the buffer for the intermediate result so that people
	   attaching to processes or reading core dumps cannot get any
	   information.  We do it in this way to clear correct_words[]
	   inside the MD5 implementation as well.  */
	md5_init(&ctx);
	md5_finish(&ctx, alt_result);
	memset (&ctx, '\0', sizeof (ctx));
	memset (&alt_ctx, '\0', sizeof (alt_ctx));

	return buffer;
}
