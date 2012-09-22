  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2004 Wilmer van der Gaast and others                *
  \********************************************************************/

/* Misc. functions                                                      */

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

#ifndef _MISC_H
#define _MISC_H

#include <gmodule.h>
#include <time.h>

struct ns_srv_reply
{
	int prio;
	int weight;
	int port;
	char name[];
};

G_MODULE_EXPORT void strip_linefeed( gchar *text );
G_MODULE_EXPORT char *add_cr( char *text );
G_MODULE_EXPORT char *strip_newlines(char *source);

G_MODULE_EXPORT time_t get_time( int year, int month, int day, int hour, int min, int sec );
G_MODULE_EXPORT time_t mktime_utc( struct tm *tp );
double gettime( void );

G_MODULE_EXPORT void strip_html( char *msg );
G_MODULE_EXPORT char *escape_html( const char *html );
G_MODULE_EXPORT void http_decode( char *s );
G_MODULE_EXPORT void http_encode( char *s );

G_MODULE_EXPORT char *ipv6_wrap( char *src );
G_MODULE_EXPORT char *ipv6_unwrap( char *src );

G_MODULE_EXPORT signed int do_iconv( char *from_cs, char *to_cs, char *src, char *dst, size_t size, size_t maxbuf );

G_MODULE_EXPORT void random_bytes( unsigned char *buf, int count );

G_MODULE_EXPORT int is_bool( char *value );
G_MODULE_EXPORT int bool2int( char *value );

G_MODULE_EXPORT struct ns_srv_reply **srv_lookup( char *service, char *protocol, char *domain );
G_MODULE_EXPORT void srv_free( struct ns_srv_reply **srv );

G_MODULE_EXPORT char *word_wrap( const char *msg, int line_len );
G_MODULE_EXPORT gboolean ssl_sockerr_again( void *ssl );
G_MODULE_EXPORT int md5_verify_password( char *password, char *hash );
G_MODULE_EXPORT char **split_command_parts( char *command );
G_MODULE_EXPORT char *get_rfc822_header( const char *text, const char *header, int len );

#endif
