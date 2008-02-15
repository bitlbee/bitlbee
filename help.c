  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2005 Wilmer van der Gaast and others                *
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

help_t *help_init( help_t **help, const char *helpfile )
{
	int i, buflen = 0;
	help_t *h;
	char *s, *t;
	time_t mtime;
	struct stat stat[1];
	
	*help = h = g_new0 ( help_t, 1 );
	
	h->fd = open( helpfile, O_RDONLY
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
			help_free( help );
			g_free( s );
			return NULL;
		}
		i = strchr( s, '\n' ) - s;
		
		if( h->title )
		{
			h = h->next = g_new0( help_t, 1 );
		}
		h->title = g_new ( char, i );
		
		strncpy( h->title, s + 1, i - 1 );
		h->title[i-1] = 0;
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

void help_free( help_t **help )
{
	help_t *h, *oh;
	int last_fd = -1; /* Weak de-dupe */
	
	if( help == NULL || *help == NULL )
		return;
	
	h = *help;
	while( h )
	{
		if( h->fd != last_fd )
		{
			close( h->fd );
			last_fd = h->fd;
		}
		g_free( h->title );
		h = (oh=h)->next;
		g_free( oh );
	}
	
	*help = NULL;
}

char *help_get( help_t **help, char *title )
{
	time_t mtime;
	struct stat stat[1];
	help_t *h;

	for( h = *help; h; h = h->next )
	{
		if( h->title != NULL && g_strcasecmp( h->title, title ) == 0 )
			break;
	}
	if( h && h->length > 0 )
	{
		char *s = g_new( char, h->length + 1 );
		
		s[h->length] = 0;
		if( h->fd >= 0 )
		{
			if( fstat( h->fd, stat ) != 0 )
			{
				g_free( s );
				return NULL;
			}
			mtime = stat->st_mtime;
		
			if( mtime > h->mtime )
			{
				g_free( s );
				return NULL;
			}
			
			lseek( h->fd, h->offset.file_offset, SEEK_SET );
			read( h->fd, s, h->length );
		}
		else
		{
			strncpy( s, h->offset.mem_offset, h->length );
		}
		return s;
	}
	
	return NULL;
}
