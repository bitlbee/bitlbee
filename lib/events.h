/*
 * nogaim
 *
 * Copyright (C) 2006 Wilmer van der Gaast and others
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

/* This stuff used to be in proxy.c too, but I split it off so BitlBee can
   use other libraries (like libevent) to handle events. proxy.c is one very
   nice piece of work from Gaim. It connects to a TCP server in the back-
   ground and calls a callback function once the connection is ready to use.
   This function (proxy_connect()) can be found in proxy.c. (It also
   transparently handles HTTP/SOCKS proxies, when necessary.)

   This file offers some extra event handling toys, which will be handled
   by GLib or libevent. The advantage of using libevent is that it can use
   more advanced I/O polling functions like epoll() in recent Linux
   kernels. This should improve BitlBee's scalability. */


#ifndef _EVENTS_H_
#define _EVENTS_H_

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <glib.h>
#include <gmodule.h>

/* The conditions you can pass to b_input_add()/that will be passed to
   the given callback function. */
typedef enum {
	B_EV_IO_READ = 1 << 0,
	        B_EV_IO_WRITE = 1 << 1,
	        B_EV_FLAG_FORCE_ONCE = 1 << 16,
	        B_EV_FLAG_FORCE_REPEAT = 1 << 17,
} b_input_condition;
typedef gboolean (*b_event_handler)(gpointer data, gint fd, b_input_condition cond);

/* For internal use. */
#define GAIM_READ_COND  (G_IO_IN | G_IO_HUP | G_IO_ERR)
#define GAIM_WRITE_COND (G_IO_OUT | G_IO_HUP | G_IO_ERR | G_IO_NVAL)
#define GAIM_ERR_COND   (G_IO_HUP | G_IO_ERR | G_IO_NVAL)

/* #define event_debug( ... ) printf( __VA_ARGS__ ) */
#define event_debug(...)

/* Call this once when the program starts. It'll initialize the event handler
   library (if necessary) and then return immediately. */
G_MODULE_EXPORT void b_main_init();

/* This one enters the event loop. It shouldn't return until one of the event
   handlers calls b_main_quit(). */
G_MODULE_EXPORT void b_main_run();
G_MODULE_EXPORT void b_main_quit();

G_MODULE_EXPORT void b_main_iteration();


/* Add event handlers (for I/O or a timeout). The event handler will be called
   every time the event "happens", until your event handler returns FALSE (or
   until you remove it using b_event_remove(). As usual, the data argument
   can be used to pass your own data to the event handler. */
G_MODULE_EXPORT gint b_input_add(int fd, b_input_condition cond, b_event_handler func, gpointer data);
G_MODULE_EXPORT gint b_timeout_add(gint timeout, b_event_handler func, gpointer data);
G_MODULE_EXPORT void b_event_remove(gint id);

/* With libevent, this one also cleans up event handlers if that wasn't already
   done (the caller is expected to do so but may miss it sometimes). */
G_MODULE_EXPORT void closesocket(int fd);

#endif /* _EVENTS_H_ */
