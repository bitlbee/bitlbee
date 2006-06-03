  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2004 Wilmer van der Gaast and others                *
  \********************************************************************/

/*
 * Various utility functions. Some are copied from Gaim to support the
 * IM-modules, most are from BitlBee.
 *
 * Copyright (C) 1998-1999, Mark Spencer <markster@marko.net>
 *                          (and possibly other members of the Gaim team)
 * Copyright 2002-2005 Wilmer van der Gaast <wilmer@gaast.net>
 */

/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License with
  the Debian GNU/Linux distribution in /usr/share/common-licenses/GPL;
  if not, write to the Free Software Foundation, Inc., 59 Temple Place,
  Suite 330, Boston, MA  02111-1307  USA
*/

#define BITLBEE_CORE
#include "nogaim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <glib.h>
#include <time.h>

void strip_linefeed(gchar *text)
{
	int i, j;
	gchar *text2 = g_malloc(strlen(text) + 1);

	for (i = 0, j = 0; text[i]; i++)
		if (text[i] != '\r')
			text2[j++] = text[i];
	text2[j] = '\0';

	strcpy(text, text2);
	g_free(text2);
}

char *add_cr(char *text)
{
	char *ret = NULL;
	int count = 0, j;
	unsigned int i;

	if (text[0] == '\n')
		count++;
	for (i = 1; i < strlen(text); i++)
		if (text[i] == '\n' && text[i - 1] != '\r')
			count++;

	if (count == 0)
		return g_strdup(text);

	ret = g_malloc0(strlen(text) + count + 1);

	i = 0; j = 0;
	if (text[i] == '\n')
		ret[j++] = '\r';
	ret[j++] = text[i++];
	for (; i < strlen(text); i++) {
		if (text[i] == '\n' && text[i - 1] != '\r')
			ret[j++] = '\r';
		ret[j++] = text[i];
	}

	return ret;
}

static char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz" "0123456789+/";

/* XXX Find bug */
char *tobase64(const char *text)
{
	char *out = NULL;
	const char *c;
	unsigned int tmp = 0;
	int len = 0, n = 0;

	c = text;

	while (*c) {
		tmp = tmp << 8;
		tmp += *c;
		n++;

		if (n == 3) {
			out = g_realloc(out, len + 4);
			out[len] = alphabet[(tmp >> 18) & 0x3f];
			out[len + 1] = alphabet[(tmp >> 12) & 0x3f];
			out[len + 2] = alphabet[(tmp >> 6) & 0x3f];
			out[len + 3] = alphabet[tmp & 0x3f];
			len += 4;
			tmp = 0;
			n = 0;
		}
		c++;
	}
	switch (n) {

	case 2:
		tmp <<= 8;
		out = g_realloc(out, len + 5);
		out[len] = alphabet[(tmp >> 18) & 0x3f];
		out[len + 1] = alphabet[(tmp >> 12) & 0x3f];
		out[len + 2] = alphabet[(tmp >> 6) & 0x3f];
		out[len + 3] = '=';
		out[len + 4] = 0;
		break;
	case 1:
		tmp <<= 16;
		out = g_realloc(out, len + 5);
		out[len] = alphabet[(tmp >> 18) & 0x3f];
		out[len + 1] = alphabet[(tmp >> 12) & 0x3f];
		out[len + 2] = '=';
		out[len + 3] = '=';
		out[len + 4] = 0;
		break;
	case 0:
		out = g_realloc(out, len + 1);
		out[len] = 0;
		break;
	}
	return out;
}

char *normalize(const char *s)
{
	static char buf[BUF_LEN];
	char *t, *u;
	int x = 0;

	g_return_val_if_fail((s != NULL), NULL);

	u = t = g_strdup(s);

	strcpy(t, s);
	g_strdown(t);

	while (*t && (x < BUF_LEN - 1)) {
		if (*t != ' ') {
			buf[x] = *t;
			x++;
		}
		t++;
	}
	buf[x] = '\0';
	g_free(u);
	return buf;
}

time_t get_time(int year, int month, int day, int hour, int min, int sec)
{
	struct tm tm;

	tm.tm_year = year - 1900;
	tm.tm_mon = month - 1;
	tm.tm_mday = day;
	tm.tm_hour = hour;
	tm.tm_min = min;
	tm.tm_sec = sec >= 0 ? sec : time(NULL) % 60;
	return mktime(&tm);
}

typedef struct htmlentity
{
	char code[8];
	char is[4];
} htmlentity_t;

/* FIXME: This is ISO8859-1(5) centric, so might cause problems with other charsets. */

static const htmlentity_t ent[] =
{
	{ "lt",     "<" },
	{ "gt",     ">" },
	{ "amp",    "&" },
	{ "quot",   "\"" },
	{ "aacute", "á" },
	{ "eacute", "é" },
	{ "iacute", "é" },
	{ "oacute", "ó" },
	{ "uacute", "ú" },
	{ "agrave", "à" },
	{ "egrave", "è" },
	{ "igrave", "ì" },
	{ "ograve", "ò" },
	{ "ugrave", "ù" },
	{ "acirc",  "â" },
	{ "ecirc",  "ê" },
	{ "icirc",  "î" },
	{ "ocirc",  "ô" },
	{ "ucirc",  "û" },
	{ "auml",   "ä" },
	{ "euml",   "ë" },
	{ "iuml",   "ï" },
	{ "ouml",   "ö" },
	{ "uuml",   "ü" },
	{ "nbsp",   " " },
	{ "",        ""  }
};

void strip_html( char *in )
{
	char *start = in;
	char *out = g_malloc( strlen( in ) + 1 );
	char *s = out, *cs;
	int i, matched;
	
	memset( out, 0, strlen( in ) + 1 );
	
	while( *in )
	{
		if( *in == '<' && ( isalpha( *(in+1) ) || *(in+1) == '/' ) )
		{
			/* If in points at a < and in+1 points at a letter or a slash, this is probably
			   a HTML-tag. Try to find a closing > and continue there. If the > can't be
			   found, assume that it wasn't a HTML-tag after all. */
			
			cs = in;
			
			while( *in && *in != '>' )
				in ++;
			
			if( *in )
			{
				if( g_strncasecmp( cs+1, "br", 2) == 0 )
					*(s++) = '\n';
				in ++;
			}
			else
			{
				in = cs;
				*(s++) = *(in++);
			}
		}
		else if( *in == '&' )
		{
			cs = ++in;
			while( *in && isalpha( *in ) )
				in ++;
			
			if( *in == ';' ) in ++;
			matched = 0;
			
			for( i = 0; *ent[i].code; i ++ )
				if( g_strncasecmp( ent[i].code, cs, strlen( ent[i].code ) ) == 0 )
				{
					int j;
					
					for( j = 0; ent[i].is[j]; j ++ )
						*(s++) = ent[i].is[j];
					
					matched = 1;
					break;
				}

			/* None of the entities were matched, so return the string */
			if( !matched )
			{
				in = cs - 1;
				*(s++) = *(in++);
			}
		}
		else
		{
			*(s++) = *(in++);
		}
	}
	
	strcpy( start, out );
	g_free( out );
}

char *escape_html( const char *html )
{
	const char *c = html;
	GString *ret;
	char *str;
	
	if( html == NULL )
		return( NULL );
	
	ret = g_string_new( "" );
	
	while( *c )
	{
		switch( *c )
		{
			case '&':
				ret = g_string_append( ret, "&amp;" );
				break;
			case '<':
				ret = g_string_append( ret, "&lt;" );
				break;
			case '>':
				ret = g_string_append( ret, "&gt;" );
				break;
			case '"':
				ret = g_string_append( ret, "&quot;" );
				break;
			default:
				ret = g_string_append_c( ret, *c );
		}
		c ++;
	}
	
	str = ret->str;
	g_string_free( ret, FALSE );
	return( str );
}

void info_string_append(GString *str, char *newline, char *name, char *value)
{
	if( value && value[0] )
		g_string_sprintfa( str, "%s%s: %s", newline, name, value );
}

/* Decode%20a%20file%20name						*/
void http_decode( char *s )
{
	char *t;
	int i, j, k;
	
	t = g_new( char, strlen( s ) + 1 );
	
	for( i = j = 0; s[i]; i ++, j ++ )
	{
		if( s[i] == '%' )
		{
			if( sscanf( s + i + 1, "%2x", &k ) )
			{
				t[j] = k;
				i += 2;
			}
			else
			{
				*t = 0;
				break;
			}
		}
		else
		{
			t[j] = s[i];
		}
	}
	t[j] = 0;
	
	strcpy( s, t );
	g_free( t );
}

/* Warning: This one explodes the string. Worst-cases can make the string 3x its original size! */
/* This fuction is safe, but make sure you call it safely as well! */
void http_encode( char *s )
{
	char *t;
	int i, j;
	
	t = g_strdup( s );
	
	for( i = j = 0; t[i]; i ++, j ++ )
	{
		/* if( t[i] <= ' ' || ((unsigned char *)t)[i] >= 128 || t[i] == '%' ) */
		if( !isalnum( t[i] ) )
		{
			sprintf( s + j, "%%%02X", ((unsigned char*)t)[i] );
			j += 2;
		}
		else
		{
			s[j] = t[i];
		}
	}
	s[j] = 0;
	
	g_free( t );
}

/* Strip newlines from a string. Modifies the string passed to it. */ 
char *strip_newlines( char *source )
{
	int i;	

	for( i = 0; source[i] != '\0'; i ++ )
		if( source[i] == '\n' || source[i] == '\r' )
			source[i] = ' ';
	
	return source;
}

#ifdef IPV6
/* Wrap an IPv4 address into IPv6 space. Not thread-safe... */
char *ipv6_wrap( char *src )
{
	static char dst[64];
	int i;
	
	for( i = 0; src[i]; i ++ )
		if( ( src[i] < '0' || src[i] > '9' ) && src[i] != '.' )
			break;
	
	/* Hmm, it's not even an IP... */
	if( src[i] )
		return src;
	
	g_snprintf( dst, sizeof( dst ), "::ffff:%s", src );
	
	return dst;
}

/* Unwrap an IPv4 address into IPv6 space. Thread-safe, because it's very simple. :-) */
char *ipv6_unwrap( char *src )
{
	int i;
	
	if( g_strncasecmp( src, "::ffff:", 7 ) != 0 )
		return src;
	
	for( i = 7; src[i]; i ++ )
		if( ( src[i] < '0' || src[i] > '9' ) && src[i] != '.' )
			break;
	
	/* Hmm, it's not even an IP... */
	if( src[i] )
		return src;
	
	return ( src + 7 );
}
#endif

/* Convert from one charset to another.
   
   from_cs, to_cs: Source and destination charsets
   src, dst: Source and destination strings
   size: Size if src. 0 == use strlen(). strlen() is not reliable for UNICODE/UTF16 strings though.
   maxbuf: Maximum number of bytes to write to dst
   
   Returns the number of bytes written to maxbuf or -1 on an error.
*/
signed int do_iconv( char *from_cs, char *to_cs, char *src, char *dst, size_t size, size_t maxbuf )
{
	GIConv cd;
	size_t res;
	size_t inbytesleft, outbytesleft;
	char *inbuf = src;
	char *outbuf = dst;
	
	cd = g_iconv_open( to_cs, from_cs );
	if( cd == (GIConv) -1 )
		return( -1 );
	
	inbytesleft = size ? size : strlen( src );
	outbytesleft = maxbuf - 1;
	res = g_iconv( cd, &inbuf, &inbytesleft, &outbuf, &outbytesleft );
	*outbuf = '\0';
	g_iconv_close( cd );
	
	if( res == (size_t) -1 )
		return( -1 );
	else
		return( outbuf - dst );
}

char *set_eval_charset( irc_t *irc, set_t *set, char *value )
{
	GIConv cd;

	if ( g_strncasecmp( value, "none", 4 ) == 0 )
		return( value );

	cd = g_iconv_open( "UTF-8", value );
	if( cd == (GIConv) -1 )
		return( NULL );

	g_iconv_close( cd );
	return( value );
}
