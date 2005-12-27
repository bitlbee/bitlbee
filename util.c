  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2004 Wilmer van der Gaast and others                *
  \********************************************************************/

/*
 * nogaim
 *
 * Gaim without gaim - for BitlBee
 *
 * Copyright (C) 1998-1999, Mark Spencer <markster@marko.net>
 *                          (and possibly other members of the Gaim team)
 * Copyright 2002-2004 Wilmer van der Gaast <lintux@lintux.cx>
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

/* Parts from util.c from gaim needed by nogaim */
#define BITLBEE_CORE
#include "nogaim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <time.h>

char *utf8_to_str(const char *in)
{
	int n = 0, i = 0;
	int inlen;
	char *result;

	if (!in)
		return NULL;

	inlen = strlen(in);

	result = g_malloc(inlen + 1);

	while (n <= inlen - 1) {
		long c = (long)in[n];
		if (c < 0x80)
			result[i++] = (char)c;
		else {
			if ((c & 0xC0) == 0xC0)
				result[i++] =
				    (char)(((c & 0x03) << 6) | (((unsigned char)in[++n]) & 0x3F));
			else if ((c & 0xE0) == 0xE0) {
				if (n + 2 <= inlen) {
					result[i] =
					    (char)(((c & 0xF) << 4) | (((unsigned char)in[++n]) & 0x3F));
					result[i] =
					    (char)(((unsigned char)result[i]) |
						   (((unsigned char)in[++n]) & 0x3F));
					i++;
				} else
					n += 2;
			} else if ((c & 0xF0) == 0xF0)
				n += 3;
			else if ((c & 0xF8) == 0xF8)
				n += 4;
			else if ((c & 0xFC) == 0xFC)
				n += 5;
		}
		n++;
	}
	result[i] = '\0';

	return result;
}

char *str_to_utf8(const char *in)
{
	int n = 0, i = 0;
	int inlen;
	char *result = NULL;

	if (!in)
		return NULL;

	inlen = strlen(in);

	result = g_malloc(inlen * 2 + 1);

	while (n < inlen) {
		long c = (long)in[n];
		if (c == 27) {
			n += 2;
			if (in[n] == 'x')
				n++;
			if (in[n] == '3')
				n++;
			n += 2;
			continue;
		}
		/* why are we removing newlines and carriage returns?
		if ((c == 0x0D) || (c == 0x0A)) {
			n++;
			continue;
		}
		*/
		if (c < 128)
			result[i++] = (char)c;
		else {
			result[i++] = (char)((c >> 6) | 192);
			result[i++] = (char)((c & 63) | 128);
		}
		n++;
	}
	result[i] = '\0';

	return result;
}

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
	char is;
} htmlentity_t;

/* FIXME: This is ISO8859-1(5) centric, so might cause problems with other charsets. */

static htmlentity_t ent[] =
{
	{ "lt",     '<' },
	{ "gt",     '>' },
	{ "amp",    '&' },
	{ "quot",   '"' },
	{ "aacute", 'á' },
	{ "eacute", 'é' },
	{ "iacute", 'é' },
	{ "oacute", 'ó' },
	{ "uacute", 'ú' },
	{ "agrave", 'à' },
	{ "egrave", 'è' },
	{ "igrave", 'ì' },
	{ "ograve", 'ò' },
	{ "ugrave", 'ù' },
	{ "acirc",  'â' },
	{ "ecirc",  'ê' },
	{ "icirc",  'î' },
	{ "ocirc",  'ô' },
	{ "ucirc",  'û' },
	{ "nbsp",   ' ' },
	{ "",        0  }
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
					*(s++) = ent[i].is;
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
