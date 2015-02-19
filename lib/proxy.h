/*
 * nogaim
 *
 * Copyright (C) 1998-1999, Mark Spencer <markster@marko.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

/* this is the export part of the proxy.c file. it does a little
   prototype-ing stuff and redefine some net function to mask them
   with some kind of transparent layer */

#ifndef _PROXY_H_
#define _PROXY_H_

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <glib.h>
#include <gmodule.h>

#include "events.h"

#define PROXY_NONE 0
#define PROXY_HTTP 1
#define PROXY_SOCKS4 2
#define PROXY_SOCKS5 3

extern char proxyhost[128];
extern int proxyport;
extern int proxytype;
extern char proxyuser[128];
extern char proxypass[128];

G_MODULE_EXPORT int proxy_connect(const char *host, int port, b_event_handler func, gpointer data);

#endif /* _PROXY_H_ */
