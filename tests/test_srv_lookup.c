/********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2012 Wilmer van der Gaast and others                *
  \********************************************************************/

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
  if not, write to the Free Software Foundation, Inc., 51 Franklin St.,
  Fifth Floor, Boston, MA  02110-1301  USA
*/

#define HAVE_RESOLV_A

#define BITLBEE_CORE
#include "nogaim.h"
#include "base64.h"
#include "md5.h"
#include "misc.h"
#include "ssl_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <glib.h>
#include <time.h>

#include <sys/types.h>
#include <glib/gprintf.h>
#include <glib/gutils.h>
#include <unistd.h>

#include <resolv.h>


/* Not every installation has gotten around to supporting SRVs yet...*/
#ifndef T_SRV
#define T_SRV 33
#endif

int main()
{
	struct ns_srv_reply **srv;
	int i;

	srv = srv_lookup( "xmpp-client", "tcp", "jabber.org" );
	for( i = 0; srv[i]; ++i )
	{
		printf( "priority=%hu\n", srv[i]->prio );
		printf( "weight=%hu\n", srv[i]->weight );
		printf( "port=%hu\n", srv[i]->port );
		printf( "target=%s\n", srv[i]->name );
		printf( "\n" );
	}
	srv_free( srv );

	return 0;
}
