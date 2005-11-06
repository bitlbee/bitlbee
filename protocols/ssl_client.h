  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2004 Wilmer van der Gaast and others                *
  \********************************************************************/

/* SSL module                                                           */

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

#include <glib.h>
#include "proxy.h"

typedef void (*SslInputFunction)(gpointer, void*, GaimInputCondition);

G_MODULE_EXPORT void *ssl_connect( char *host, int port, SslInputFunction func, gpointer data );
G_MODULE_EXPORT int ssl_read( void *conn, char *buf, int len );
G_MODULE_EXPORT int ssl_write( void *conn, const char *buf, int len );
G_MODULE_EXPORT void ssl_disconnect( void *conn_ );
G_MODULE_EXPORT int ssl_getfd( void *conn );
