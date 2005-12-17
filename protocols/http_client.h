  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2005 Wilmer van der Gaast and others                *
  \********************************************************************/

/* HTTP(S) module (actually, it only does HTTPS right now)              */

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

#include "ssl_client.h"

struct http_request;

typedef void (*http_input_function)( struct http_request * );

struct http_request
{
	char *request;
	int request_length;
	int status_code;
	char *reply_headers;
	char *reply_body;
	int finished;
	
	void *ssl;
	int fd;
	
	int inpa;
	int bytes_written;
	int bytes_read;
	
	http_input_function func;
	gpointer data;
};

void *http_dorequest( char *host, int port, http_input_function func, int ssl, char *request, gpointer data );
