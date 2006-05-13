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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

/*
 * Split off the event handling things from proxy.[ch] (and adding timer
 * stuff. This to allow BitlBee to use other libs than GLib for event
 * handling.
 */


#ifndef _EVENTS_H_
#define _EVENTS_H_

#include <sys/types.h>
#ifndef _WIN32
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#endif
#include <glib.h>
#include <gmodule.h>

typedef enum {
	GAIM_INPUT_READ = 1 << 0,
	GAIM_INPUT_WRITE = 1 << 1
} b_input_condition;
typedef gboolean (*b_event_handler)(gpointer data, gint fd, b_input_condition cond);

#define GAIM_READ_COND  (G_IO_IN | G_IO_HUP | G_IO_ERR)
#define GAIM_WRITE_COND (G_IO_OUT | G_IO_HUP | G_IO_ERR | G_IO_NVAL)
#define GAIM_ERR_COND   (G_IO_HUP | G_IO_ERR | G_IO_NVAL)

#define event_debug( x... ) printf( x )

G_MODULE_EXPORT void b_main_init();
G_MODULE_EXPORT void b_main_run();
G_MODULE_EXPORT void b_main_quit();

G_MODULE_EXPORT gint b_input_add(int fd, b_input_condition cond, b_event_handler func, gpointer data);
G_MODULE_EXPORT gint b_timeout_add(gint timeout, b_event_handler func, gpointer data);
G_MODULE_EXPORT void b_event_remove(gint id);
G_MODULE_EXPORT gboolean b_event_remove_by_data(gpointer data);

#endif /* _EVENTS_H_ */
