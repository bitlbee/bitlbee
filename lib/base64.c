/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Base64 handling functions. encode_real() is mostly based on the y64 en-  *
*  coder from libyahoo2. Moving it to a new file because it's getting big.  *
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

#include <glib.h>
#include <string.h>
#include "base64.h"

static const char real_b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";

char *tobase64(const char *text)
{
	return base64_encode((const unsigned char *)text, strlen(text));
}

char *base64_encode(const unsigned char *in, int len)
{
	char *out;
	
	out = g_malloc((len + 2)    /* the == padding */
	                    / 3     /* every 3-byte block */
	                    * 4     /* becomes a 4-byte one */
	                    + 1);   /* and of course, ASCIIZ! */
	
	base64_encode_real((unsigned char*) in, len, (unsigned char*) out, real_b64);
	
	return out;
}

int base64_encode_real(const unsigned char *in, int inlen, unsigned char *out, const char *b64digits)
{
	int outlen = 0;
	
	for (; inlen >= 3; inlen -= 3)
	{
		out[outlen++] = b64digits[in[0] >> 2];
		out[outlen++] = b64digits[((in[0]<<4) & 0x30) | (in[1]>>4)];
		out[outlen++] = b64digits[((in[1]<<2) & 0x3c) | (in[2]>>6)];
		out[outlen++] = b64digits[in[2] & 0x3f];
		in += 3;
	}
	if (inlen > 0)
	{
		out[outlen++] = b64digits[in[0] >> 2];
		if (inlen > 1)
		{
			out[outlen++] = b64digits[((in[0]<<4) & 0x30) | (in[1]>>4)];
			out[outlen++] = b64digits[((in[1]<<2) & 0x3c)];
		}
		else
		{
			out[outlen++] = b64digits[((in[0]<<4) & 0x30)];
			out[outlen++] = b64digits[64];
		}
		out[outlen++] = b64digits[64];
	}
	out[outlen] = 0;
	
	return outlen;
}

/* Just a simple wrapper, but usually not very convenient because of zero
   termination. */
char *frombase64(const char *in)
{
	unsigned char *out;
	
	base64_decode(in, &out);
	
	return (char*) out;
}

/* FIXME: Lookup table stuff is not threadsafe! (But for now BitlBee is not threaded.) */
int base64_decode(const char *in, unsigned char **out)
{
	static char b64rev[256] = { 0 };
	int len, i;
	
	/* Create a reverse-lookup for the Base64 sequence. */
	if( b64rev[0] == 0 )
	{
		memset( b64rev, 0xff, 256 );
		for( i = 0; i <= 64; i ++ )
			b64rev[(int)real_b64[i]] = i;
	}
	
	len = strlen( in );
	*out = g_malloc( ( len + 6 ) / 4 * 3 );
	len = base64_decode_real( (unsigned char*) in, *out, b64rev );
	*out = g_realloc( *out, len + 1 );
	out[0][len] = 0;	/* Zero termination can't hurt. */
	
	return len;
}

int base64_decode_real(const unsigned char *in, unsigned char *out, char *b64rev)
{
	int i, outlen = 0;
	
	for( i = 0; in[i] && in[i+1] && in[i+2] && in[i+3]; i += 4 )
	{
		int sx;
		
		sx = b64rev[(int)in[i+0]];
		if( sx >= 64 )
			break;
		out[outlen] = ( sx << 2 ) & 0xfc;
		
		sx = b64rev[(int)in[i+1]];
		if( sx >= 64 )
			break;
		out[outlen] |= ( sx >> 4 ) & 0x03;
		outlen ++;
		out[outlen] = ( sx << 4 ) & 0xf0;
		
		sx = b64rev[(int)in[i+2]];
		if( sx >= 64 )
			break;
		out[outlen] |= ( sx >> 2 ) & 0x0f;
		outlen ++;
		out[outlen] = ( sx << 6 ) & 0xc0;
		
		sx = b64rev[(int)in[i+3]];
		if( sx >= 64 )
			break;
		out[outlen] |= sx;
		outlen ++;
	}
	
	/* If sx > 64 the base64 string was damaged. Should we ignore this? */
	
	return outlen;
}
