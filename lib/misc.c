  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2006 Wilmer van der Gaast and others                *
  \********************************************************************/

/*
 * Various utility functions. Some are copied from Gaim to support the
 * IM-modules, most are from BitlBee.
 *
 * Copyright (C) 1998-1999, Mark Spencer <markster@marko.net>
 *                          (and possibly other members of the Gaim team)
 * Copyright 2002-2006 Wilmer van der Gaast <wilmer@gaast.net>
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
#include "base64.h"
#include "md5.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <glib.h>
#include <time.h>

#ifdef HAVE_RESOLV_A
#include <arpa/nameser.h>
#include <resolv.h>
#endif

#include "md5.h"
#include "ssl_client.h"

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

time_t get_time(int year, int month, int day, int hour, int min, int sec)
{
	struct tm tm;

	memset(&tm, 0, sizeof(struct tm));
	tm.tm_year = year - 1900;
	tm.tm_mon = month - 1;
	tm.tm_mday = day;
	tm.tm_hour = hour;
	tm.tm_min = min;
	tm.tm_sec = sec >= 0 ? sec : time(NULL) % 60;
	
	return mktime(&tm);
}

time_t mktime_utc( struct tm *tp )
{
	struct tm utc;
	time_t res, tres;
	
	tp->tm_isdst = -1;
	res = mktime( tp );
	/* Problem is, mktime() just gave us the GMT timestamp for the
	   given local time... While the given time WAS NOT local. So
	   we should fix this now.
	   
	   Now I could choose between messing with environment variables
	   (kludgy) or using timegm() (not portable)... Or doing the
	   following, which I actually prefer...
	   
	   tzset() may also work but in other places I actually want to
	   use local time.
	   
	   FFFFFFFFFFFFFFFFFFFFFUUUUUUUUUUUUUUUUUUUU!! */
	gmtime_r( &res, &utc );
	utc.tm_isdst = -1;
	if( utc.tm_hour == tp->tm_hour && utc.tm_min == tp->tm_min )
		/* Sweet! We're in UTC right now... */
		return res;
	
	tres = mktime( &utc );
	res += res - tres;
	
	/* Yes, this is a hack. And it will go wrong around DST changes.
	   BUT this is more likely to be threadsafe than messing with
	   environment variables, and possibly more portable... */
	
	return res;
}

typedef struct htmlentity
{
	char code[7];
	char is[3];
} htmlentity_t;

static const htmlentity_t ent[] =
{
	{ "lt",     "<" },
	{ "gt",     ">" },
	{ "amp",    "&" },
	{ "apos",   "'" },
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
	char out[strlen(in)+1];
	char *s = out, *cs;
	int i, matched;
	int taglen;
	
	memset( out, 0, sizeof( out ) );
	
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
			
			taglen = in - cs - 1;   /* not <0 because the above loop runs at least once */
			if( *in )
			{
				if( g_strncasecmp( cs+1, "b", taglen) == 0 )
					*(s++) = '\x02';
				else if( g_strncasecmp( cs+1, "/b", taglen) == 0 )
					*(s++) = '\x02';
				else if( g_strncasecmp( cs+1, "i", taglen) == 0 )
					*(s++) = '\x1f';
				else if( g_strncasecmp( cs+1, "/i", taglen) == 0 )
					*(s++) = '\x1f';
				else if( g_strncasecmp( cs+1, "br", taglen) == 0 )
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
	char t[strlen(s)+1];
	int i, j;
	
	strcpy( t, s );
	for( i = j = 0; t[i]; i ++, j ++ )
	{
		/* Warning: isalnum() is locale-aware, so don't use it here! */
		if( ( t[i] >= 'A' && t[i] <= 'Z' ) ||
		    ( t[i] >= 'a' && t[i] <= 'z' ) ||
		    ( t[i] >= '0' && t[i] <= '9' ) ||
		    strchr( "._-~", t[i] ) )
		{
			s[j] = t[i];
		}
		else
		{
			sprintf( s + j, "%%%02X", ((unsigned char*)t)[i] );
			j += 2;
		}
	}
	s[j] = 0;
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
		return -1;
	
	inbytesleft = size ? size : strlen( src );
	outbytesleft = maxbuf - 1;
	res = g_iconv( cd, &inbuf, &inbytesleft, &outbuf, &outbytesleft );
	*outbuf = '\0';
	g_iconv_close( cd );
	
	if( res != 0 )
		return -1;
	else
		return outbuf - dst;
}

/* A pretty reliable random number generator. Tries to use the /dev/random
   devices first, and falls back to the random number generator from libc
   when it fails. Opens randomizer devices with O_NONBLOCK to make sure a
   lack of entropy won't halt BitlBee. */
void random_bytes( unsigned char *buf, int count )
{
#ifndef _WIN32
	static int use_dev = -1;
	
	/* Actually this probing code isn't really necessary, is it? */
	if( use_dev == -1 )
	{
		if( access( "/dev/random", R_OK ) == 0 || access( "/dev/urandom", R_OK ) == 0 )
			use_dev = 1;
		else
		{
			use_dev = 0;
			srand( ( getpid() << 16 ) ^ time( NULL ) );
		}
	}
	
	if( use_dev )
	{
		int fd;
		
		/* At least on Linux, /dev/random can block if there's not
		   enough entropy. We really don't want that, so if it can't
		   give anything, use /dev/urandom instead. */
		if( ( fd = open( "/dev/random", O_RDONLY | O_NONBLOCK ) ) >= 0 )
			if( read( fd, buf, count ) == count )
			{
				close( fd );
				return;
			}
		close( fd );
		
		/* urandom isn't supposed to block at all, but just to be
		   sure. If it blocks, we'll disable use_dev and use the libc
		   randomizer instead. */
		if( ( fd = open( "/dev/urandom", O_RDONLY | O_NONBLOCK ) ) >= 0 )
			if( read( fd, buf, count ) == count )
			{
				close( fd );
				return;
			}
		close( fd );
		
		/* If /dev/random blocks once, we'll still try to use it
		   again next time. If /dev/urandom also fails for some
		   reason, stick with libc during this session. */
		
		use_dev = 0;
		srand( ( getpid() << 16 ) ^ time( NULL ) );
	}
	
	if( !use_dev )
#endif
	{
		int i;
		
		/* Possibly the LSB of rand() isn't very random on some
		   platforms. Seems okay on at least Linux and OSX though. */
		for( i = 0; i < count; i ++ )
			buf[i] = rand() & 0xff;
	}
}

int is_bool( char *value )
{
	if( *value == 0 )
		return 0;
	
	if( ( g_strcasecmp( value, "true" ) == 0 ) || ( g_strcasecmp( value, "yes" ) == 0 ) || ( g_strcasecmp( value, "on" ) == 0 ) )
		return 1;
	if( ( g_strcasecmp( value, "false" ) == 0 ) || ( g_strcasecmp( value, "no" ) == 0 ) || ( g_strcasecmp( value, "off" ) == 0 ) )
		return 1;
	
	while( *value )
		if( !isdigit( *value ) )
			return 0;
		else
			value ++;
	
	return 1;
}

int bool2int( char *value )
{
	int i;
	
	if( ( g_strcasecmp( value, "true" ) == 0 ) || ( g_strcasecmp( value, "yes" ) == 0 ) || ( g_strcasecmp( value, "on" ) == 0 ) )
		return 1;
	if( ( g_strcasecmp( value, "false" ) == 0 ) || ( g_strcasecmp( value, "no" ) == 0 ) || ( g_strcasecmp( value, "off" ) == 0 ) )
		return 0;
	
	if( sscanf( value, "%d", &i ) == 1 )
		return i;
	
	return 0;
}

struct ns_srv_reply **srv_lookup( char *service, char *protocol, char *domain )
{	
	struct ns_srv_reply **replies = NULL;
#ifdef HAVE_RESOLV_A
	struct ns_srv_reply *reply = NULL;
	char name[1024];
	unsigned char querybuf[1024];
	const unsigned char *buf;
	ns_msg nsh;
	ns_rr rr;
	int i, n, len, size;
	
	g_snprintf( name, sizeof( name ), "_%s._%s.%s", service, protocol, domain );
	
	if( ( size = res_query( name, ns_c_in, ns_t_srv, querybuf, sizeof( querybuf ) ) ) <= 0 )
		return NULL;
	
	if( ns_initparse( querybuf, size, &nsh ) != 0 )
		return NULL;
	
	n = 0;
	while( ns_parserr( &nsh, ns_s_an, n, &rr ) == 0 )
	{
		size = ns_rr_rdlen( rr );
		buf = ns_rr_rdata( rr );
		
		len = 0;
		for( i = 6; i < size && buf[i]; i += buf[i] + 1 )
			len += buf[i] + 1;
		
		if( i > size )
			break;
		
		reply = g_malloc( sizeof( struct ns_srv_reply ) + len );
		memcpy( reply->name, buf + 7, len );
		
		for( i = buf[6]; i < len && buf[7+i]; i += buf[7+i] + 1 )
			reply->name[i] = '.';
		
		if( i > len )
		{
			g_free( reply );
			break;
		}
		
		reply->prio = ( buf[0] << 8 ) | buf[1];
		reply->weight = ( buf[2] << 8 ) | buf[3];
		reply->port = ( buf[4] << 8 ) | buf[5];
		
		n ++;
		replies = g_renew( struct ns_srv_reply *, replies, n + 1 );
		replies[n-1] = reply;
	}
	if( replies )
		replies[n] = NULL;
#endif
	
	return replies;
}

void srv_free( struct ns_srv_reply **srv )
{
	int i;
	
	if( srv == NULL )
		return;
	
	for( i = 0; srv[i]; i ++ )
		g_free( srv[i] );
	g_free( srv );
}

/* Word wrapping. Yes, I know this isn't UTF-8 clean. I'm willing to take the risk. */
char *word_wrap( const char *msg, int line_len )
{
	GString *ret = g_string_sized_new( strlen( msg ) + 16 );
	
	while( strlen( msg ) > line_len )
	{
		int i;
		
		/* First try to find out if there's a newline already. Don't
		   want to add more splits than necessary. */
		for( i = line_len; i > 0 && msg[i] != '\n'; i -- );
		if( msg[i] == '\n' )
		{
			g_string_append_len( ret, msg, i + 1 );
			msg += i + 1;
			continue;
		}
		
		for( i = line_len; i > 0; i -- )
		{
			if( msg[i] == '-' )
			{
				g_string_append_len( ret, msg, i + 1 );
				g_string_append_c( ret, '\n' );
				msg += i + 1;
				break;
			}
			else if( msg[i] == ' ' )
			{
				g_string_append_len( ret, msg, i );
				g_string_append_c( ret, '\n' );
				msg += i + 1;
				break;
			}
		}
		if( i == 0 )
		{
			g_string_append_len( ret, msg, line_len );
			g_string_append_c( ret, '\n' );
			msg += line_len;
		}
	}
	g_string_append( ret, msg );
	
	return g_string_free( ret, FALSE );
}

gboolean ssl_sockerr_again( void *ssl )
{
	if( ssl )
		return ssl_errno == SSL_AGAIN;
	else
		return sockerr_again();
}

/* Returns values: -1 == Failure (base64-decoded to something unexpected)
                    0 == Okay
                    1 == Password doesn't match the hash. */
int md5_verify_password( char *password, char *hash )
{
	md5_byte_t *pass_dec = NULL;
	md5_byte_t pass_md5[16];
	md5_state_t md5_state;
	int ret = -1, i;
	
	if( base64_decode( hash, &pass_dec ) == 21 )
	{
		md5_init( &md5_state );
		md5_append( &md5_state, (md5_byte_t*) password, strlen( password ) );
		md5_append( &md5_state, (md5_byte_t*) pass_dec + 16, 5 ); /* Hmmm, salt! */
		md5_finish( &md5_state, pass_md5 );
		
		for( i = 0; i < 16; i ++ )
		{
			if( pass_dec[i] != pass_md5[i] )
			{
				ret = 1;
				break;
			}
		}
		
		/* If we reached the end of the loop, it was a match! */
		if( i == 16 )
			ret = 0;
	}
	
	g_free( pass_dec );

	return ret;
}

/* Split commands (root-style, *not* IRC-style). Handles "quoting of"
   white\ space in 'various ways'. Returns a NULL-terminated static
   char** so watch out with nested use! Definitely not thread-safe. */
char **split_command_parts( char *command )
{
	static char *cmd[IRC_MAX_ARGS+1];
	char *s, q = 0;
	int k;
	
	memset( cmd, 0, sizeof( cmd ) );
	cmd[0] = command;
	k = 1;
	for( s = command; *s && k < IRC_MAX_ARGS; s ++ )
		if( *s == ' ' && !q )
		{
			*s = 0;
			while( *++s == ' ' );
			if( *s == '"' || *s == '\'' )
			{
				q = *s;
				s ++;
			}
			if( *s )
			{
				cmd[k++] = s;
				s --;
			}
			else
			{
				break;
			}
		}
		else if( *s == '\\' && ( ( !q && s[1] ) || ( q && q == s[1] ) ) )
		{
			char *cpy;
			
			for( cpy = s; *cpy; cpy ++ )
				cpy[0] = cpy[1];
		}
		else if( *s == q )
		{
			q = *s = 0;
		}
	
	/* Full zero-padding for easier argc checking. */
	while( k <= IRC_MAX_ARGS )
		cmd[k++] = NULL;
	
	return cmd;
}

char *get_rfc822_header( const char *text, const char *header, int len )
{
	int hlen = strlen( header ), i;
	const char *ret;
	
	if( text == NULL )
		return NULL;
	
	if( len == 0 )
		len = strlen( text );
	
	i = 0;
	while( ( i + hlen ) < len )
	{
		/* Maybe this is a bit over-commented, but I just hate this part... */
		if( g_strncasecmp( text + i, header, hlen ) == 0 )
		{
			/* Skip to the (probable) end of the header */
			i += hlen;
			
			/* Find the first non-[: \t] character */
			while( i < len && ( text[i] == ':' || text[i] == ' ' || text[i] == '\t' ) ) i ++;
			
			/* Make sure we're still inside the string */
			if( i >= len ) return( NULL );
			
			/* Save the position */
			ret = text + i;
			
			/* Search for the end of this line */
			while( i < len && text[i] != '\r' && text[i] != '\n' ) i ++;
			
			/* Make sure we're still inside the string */
			if( i >= len ) return( NULL );
			
			/* Copy the found data */
			return( g_strndup( ret, text + i - ret ) );
		}
		
		/* This wasn't the header we were looking for, skip to the next line. */
		while( i < len && ( text[i] != '\r' && text[i] != '\n' ) ) i ++;
		while( i < len && ( text[i] == '\r' || text[i] == '\n' ) ) i ++;
		
		/* End of headers? */
		if( ( i >= 4 && strncmp( text + i - 4, "\r\n\r\n", 4 ) == 0 ) ||
		    ( i >= 2 && ( strncmp( text + i - 2, "\n\n", 2 ) == 0 ||   
		                  strncmp( text + i - 2, "\r\r", 2 ) == 0 ) ) )
		{
			break;
		}
	}
	
	return NULL;
}
