  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2004 Wilmer van der Gaast and others                *
  \********************************************************************/

/* SSL module - dummy version                                           */

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

#include "ssl_client.h"

int ssl_errno;

void ssl_init( void )
{
}

void *ssl_connect( char *host, int port, ssl_input_function func, gpointer data )
{
	return( NULL );
}

int ssl_read( void *conn, char *buf, int len )
{
	return( -1 );
}

int ssl_write( void *conn, const char *buf, int len )
{
	return( -1 );
}

void ssl_disconnect( void *conn_ )
{
}

int ssl_getfd( void *conn )
{
	return( -1 );
}

void *ssl_starttls( int fd, ssl_input_function func, gpointer data ) 
{
	return NULL;
}

b_input_condition ssl_getdirection( void *conn )
{
	return B_EV_IO_READ;
}

int ssl_pending( void *conn )
{
	return 0;
}

int ssl_pending( void *conn )
{
	return 0;
}
