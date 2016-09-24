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
  if not, write to the Free Software Foundation, Inc., 51 Franklin St.,
  Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "bitlbee.h"

#define PROTO_HTTP      2
#define PROTO_HTTPS     5
#define PROTO_SOCKS4    3
#define PROTO_SOCKS5    4
#define PROTO_SOCKS4A   5
#define PROTO_DEFAULT   PROTO_HTTP

typedef struct url {
	int proto;
	int port;
	char host[MAX_STRING + 1];
	char file[MAX_STRING + 1];
	char user[MAX_STRING + 1];
	char pass[MAX_STRING + 1];
} url_t;

int url_set(url_t *url, const char *set_url);
