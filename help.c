  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2004 Wilmer van der Gaast and others                *
  \********************************************************************/

/* Help file control                                                    */

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
#include "bitlbee.h"
#undef read 
#undef write

#define BUFSIZE 1100

help_t *help_init( help_t **help )
{
	int i, buflen = 0;
	help_t *h;
	char *s, *t;
	time_t mtime;
	struct stat stat[1];
	
	*help = h = g_new0 ( help_t, 1 );
	
	h->fd = open( global.helpfile, O_RDONLY
#ifdef _WIN32
				  | O_BINARY
#endif
				  );
	
	if( h->fd == -1 )
	{
		g_free( h );
		return( *help = NULL );
	}
	
	if( fstat( h->fd, stat ) != 0 )
	{
		g_free( h );
		return( *help = NULL );
	}
	mtime = stat->st_mtime;
	
	s = g_new (char, BUFSIZE + 1 );
	s[BUFSIZE] = 0;
	
	while( ( ( i = read( h->fd, s + buflen, BUFSIZE - buflen ) ) > 0 ) ||
	       ( i == 0 && strstr( s, "\n%\n" ) ) )
	{
		buflen += i;
		memset( s + buflen, 0, BUFSIZE - buflen );
		if( !( t = strstr( s, "\n%\n" ) ) || s[0] != '?' )
		{
			/* FIXME: Clean up */
//			help_close( *help );
			*help = NULL;
			g_free( s );
			return( NULL );
		}
		i = strchr( s, '\n' ) - s;
		
		if( h->string )
		{
			h = h->next = g_new0( help_t, 1 );
		}
		h->string = g_new ( char, i );
		
		strncpy( h->string, s + 1, i - 1 );
		h->string[i-1] = 0;
		h->fd = (*help)->fd;
		h->offset.file_offset = lseek( h->fd, 0, SEEK_CUR ) - buflen + i + 1;
		h->length = t - s - i - 1;
		h->mtime = mtime;
		
		buflen -= ( t + 3 - s );
		t = g_strdup( t + 3 );
		g_free( s );
		s = g_renew( char, t, BUFSIZE + 1 );
		s[BUFSIZE] = 0;
	}
	
	g_free( s );
	
	return( *help );
}

char *help_get( help_t **help, char *string )
{
	time_t mtime;
	struct stat stat[1];
	help_t *h;

	h=*help;	

	while( h )
	{
		if( g_strcasecmp( h->string, string ) == 0 ) break;
		h = h->next;
	}
	if( h )
	{
		char *s = g_new( char, h->length + 1 );
		
		if( fstat( h->fd, stat ) != 0 )
		{
			g_free( h );
			*help=NULL;
			return( NULL );
		}
		mtime = stat->st_mtime;
		
		if( mtime > h->mtime ) {
			return( NULL );
			return( g_strdup( "Help file changed during this session. Please restart to get help back." ) );
		}
		s[h->length] = 0;
		if( h->fd >= 0 )
		{
			lseek( h->fd, h->offset.file_offset, SEEK_SET );
			read( h->fd, s, h->length );
		}
		else
		{
			strncpy( s, h->offset.mem_offset, h->length );
		}
		return( s );
	}
	
	return( NULL );
}
