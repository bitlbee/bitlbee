/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Simple module to facilitate mastodon functionality.                       *
*                                                                           *
*  Copyright 2009 Geert Mulders <g.c.w.m.mulders@gmail.com>                 *
*                                                                           *
*  This library is free software; you can redistribute it and/or            *
*  modify it under the terms of the GNU Lesser General Public               *
*  License as published by the Free Software Foundation, version            *
*  2.1.                                                                     *
*                                                                           *
*  This library is distributed in the hope that it will be useful,          *
*  but WITHOUT ANY WARRANTY; without even the implied warranty of           *
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU        *
*  Lesser General Public License for more details.                          *
*                                                                           *
*  You should have received a copy of the GNU Lesser General Public License *
*  along with this library; if not, write to the Free Software Foundation,  *
*  Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA           *
*                                                                           *
****************************************************************************/

#pragma once

#include "nogaim.h"
#include "http_client.h"

typedef enum {
	/* With this set, mastodon_http_post() will post a generic confirmation
	   message to the user. */
	MASTODON_HTTP_USER_ACK = 0x1000000,
} mastodon_http_flags_t;

struct oauth_info;

struct http_request *mastodon_http(struct im_connection *ic, char *url_string, http_input_function func,
                                  gpointer data, int is_post, char** arguments, int arguments_len);
struct http_request *mastodon_http_f(struct im_connection *ic, char *url_string, http_input_function func,
                                    gpointer data, int is_post, char** arguments, int arguments_len,
                                    mastodon_http_flags_t flags);
