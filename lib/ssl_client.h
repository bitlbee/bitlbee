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

/* ssl_client makes it easier to open SSL connections to servers. (It
   doesn't offer SSL server functionality yet, but it could be useful
   to add it later.) Different ssl_client modules are available, and
   ssl_client tries to make them all behave the same. It's very simple
   and basic, it just imitates the proxy_connect() function from the
   Gaim libs and passes the socket to the program once the handshake
   is completed. */

#include <glib.h>
#include "proxy.h"

/* Some generic error codes. Especially SSL_AGAIN is important if you
   want to do asynchronous I/O. */
#define SSL_OK            0
#define SSL_NOHANDSHAKE   1
#define SSL_AGAIN         2

extern int ssl_errno;

/* This is what your callback function should look like. */
typedef gboolean (*ssl_input_function)(gpointer, void*, b_input_condition);


/* Connect to host:port, call the given function when the connection is
   ready to be used for SSL traffic. This is all done asynchronously, no
   blocking I/O! (Except for the DNS lookups, for now...) */
G_MODULE_EXPORT void *ssl_connect( char *host, int port, ssl_input_function func, gpointer data );

/* Start an SSL session on an existing fd. Useful for STARTTLS functionality,
   for example in Jabber. */
G_MODULE_EXPORT void *ssl_starttls( int fd, ssl_input_function func, gpointer data );

/* Obviously you need special read/write functions to read data. */
G_MODULE_EXPORT int ssl_read( void *conn, char *buf, int len );
G_MODULE_EXPORT int ssl_write( void *conn, const char *buf, int len );

/* See ssl_openssl.c for an explanation. */
G_MODULE_EXPORT int ssl_pending( void *conn );

/* Abort the SSL connection and disconnect the socket. Do not use close()
   directly, both the SSL library and the peer will be unhappy! */
G_MODULE_EXPORT void ssl_disconnect( void *conn_ );

/* Get the fd for this connection, you will usually need it for event
   handling. */
G_MODULE_EXPORT int ssl_getfd( void *conn );

/* This function returns GAIM_INPUT_READ/WRITE. With SSL connections it's
   possible that something has to be read while actually were trying to
   write something (think about key exchange/refresh/etc). So when an
   SSL operation returned SSL_AGAIN, *always* use this function when
   adding an event handler to the queue. (And it should perform exactly
   the same action as the handler that just received the SSL_AGAIN.) */
G_MODULE_EXPORT b_input_condition ssl_getdirection( void *conn );
