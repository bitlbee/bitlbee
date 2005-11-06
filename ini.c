  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2004 Wilmer van der Gaast and others                *
  \********************************************************************/

/* INI file reading code						*/

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

ini_t *ini_open( char *file )
{
	ini_t *ini = g_new0( ini_t, 1 );
	
	if( ( ini->fp = fopen( file, "r" ) ) == NULL )
	{
		g_free( ini );
		return( NULL );
	}
	
	return( ini );
}

int ini_read( ini_t *file )
{
	char key[MAX_STRING], s[MAX_STRING], *t;
	int i;
	
	while( !feof( file->fp ) )
	{
		*s = 0;
		fscanf( file->fp, "%127[^\n#]s", s );
		fscanf( file->fp, "%*[^\n]s" );
		fgetc( file->fp );		/* Skip newline		*/
		file->line ++;
		if( strchr( s, '=' ) )
		{
			sscanf( s, "%[^ =]s", key );
			if( ( t = strchr( key, '.' ) ) )
			{
				*t = 0;
				strcpy( file->section, key );
				t ++;
			}
			else
			{
				strcpy( file->section, file->c_section );
				t = key;
			}
			sscanf( t, "%s", file->key );
			t = strchr( s, '=' ) + 1;
			for( i = 0; t[i] == ' '; i ++ );
			strcpy( file->value, &t[i] );
			for( i = strlen( file->value ) - 1; file->value[i] == 32; i -- )
				file->value[i] = 0;
			
			return( 1 );
		}
		else if( ( t = strchr( s, '[' ) ) )
		{
			strcpy( file->c_section, t + 1 );
			t = strchr( file->c_section, ']' );
			*t = 0;
		}
	}
	return( 0 );
}

void ini_close( ini_t *file )
{
	fclose( file->fp );
	g_free( file );
}
