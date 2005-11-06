  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2001-2004 Wilmer van der Gaast and others                *
  \********************************************************************/

/* URL/mirror stuff - Stolen from Axel                                  */

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

#include "url.h"

/* Convert an URL to a url_t structure					*/
int url_set( url_t *url, char *set_url )
{
	char s[MAX_STRING];
	char *i, *j;
	
	/* protocol://							*/
	if( ( i = strstr( set_url, "://" ) ) == NULL )
	{
		url->proto = PROTO_DEFAULT;
		strncpy( s, set_url, MAX_STRING );
	}
	else
	{
		if( g_strncasecmp( set_url, "http", i - set_url ) == 0 )
			url->proto = PROTO_HTTP;
		else if( g_strncasecmp( set_url, "socks4", i - set_url ) == 0 )
			url->proto = PROTO_SOCKS4;
		else if( g_strncasecmp( set_url, "socks5", i - set_url ) == 0 )
			url->proto = PROTO_SOCKS5;
		else
		{
			return( 0 );
		}
		strncpy( s, i + 3, MAX_STRING );
	}
	
	/* Split							*/
	if( ( i = strchr( s, '/' ) ) == NULL )
	{
		strcpy( url->dir, "/" );
	}
	else
	{
		*i = 0;
		g_snprintf( url->dir, MAX_STRING, "/%s", i + 1 );
		if( url->proto == PROTO_HTTP )
			http_encode( url->dir );
	}
	strncpy( url->host, s, MAX_STRING );
	j = strchr( url->dir, '?' );
	if( j != NULL )
		*j = 0;
	i = strrchr( url->dir, '/' );
	*i = 0;
	if( j != NULL )
		*j = '?';
	if( i == NULL )
	{
		strcpy( url->file, url->dir );
		strcpy( url->dir, "/" );
	}
	else
	{
		strcpy( url->file, i + 1 );
		strcat( url->dir, "/" );
	}
	
	/* Check for username in host field				*/
	if( strrchr( url->host, '@' ) != NULL )
	{
		strncpy( url->user, url->host, MAX_STRING );
		i = strrchr( url->user, '@' );
		*i = 0;
		strcpy( url->host, i + 1 );
		*url->pass = 0;
	}
	/* If not: Fill in defaults					*/
	else
	{
		if( url->proto == PROTO_FTP )
		{
			strcpy( url->user, "anonymous" );
			strcpy( url->pass, "-p.artmaps@lintux.cx" );
		}
		else
		{
			*url->user = *url->pass = 0;
		}
	}
	
	/* Password?							*/
	if( ( i = strchr( url->user, ':' ) ) != NULL )
	{
		*i = 0;
		strcpy( url->pass, i + 1 );
	}
	/* Port number?							*/
	if( ( i = strchr( url->host, ':' ) ) != NULL )
	{
		*i = 0;
		sscanf( i + 1, "%i", &url->port );
	}
	/* Take default port numbers from /etc/services			*/
	else
	{
		if( url->proto == PROTO_HTTP )
			url->port = 8080;
		else if( url->proto == PROTO_SOCKS4 || url->proto == PROTO_SOCKS4 )
			url->port = 1080;
	}
	
	return( url->port > 0 );
}
