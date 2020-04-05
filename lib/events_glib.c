/********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2006 Wilmer van der Gaast and others                *
  \********************************************************************/

/*
 * Event handling (using GLib)
 */

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

#define BITLBEE_CORE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "proxy.h"

typedef struct _GaimIOClosure {
	b_event_handler function;
	gpointer data;
	guint flags;
} GaimIOClosure;

static GMainLoop *loop = NULL;

void b_main_init()
{
	if (loop == NULL) {
		loop = g_main_loop_new(NULL, FALSE);
	}
}

void b_main_run()
{
	g_main_loop_run(loop);
}

void b_main_quit()
{
	g_main_loop_quit(loop);
}

void b_main_iteration()
{
	g_main_context_iteration(NULL, FALSE);
	event_debug("b_main_iteration()\n");
}

static gboolean gaim_io_invoke(GIOChannel *source, GIOCondition condition, gpointer data)
{
	GaimIOClosure *closure = data;
	b_input_condition gaim_cond = 0;
	gboolean st;

	if (condition & G_IO_NVAL) {
		return FALSE;
	}

	if (condition & GAIM_READ_COND) {
		gaim_cond |= B_EV_IO_READ;
	}
	if (condition & GAIM_WRITE_COND) {
		gaim_cond |= B_EV_IO_WRITE;
	}

	event_debug("gaim_io_invoke( %d, %d, %p )\n", g_io_channel_unix_get_fd(source), condition, data);

	st = closure->function(closure->data, g_io_channel_unix_get_fd(source), gaim_cond);

	if (!st) {
		event_debug("Returned FALSE, cancelling.\n");
	}

	if (closure->flags & B_EV_FLAG_FORCE_ONCE) {
		return FALSE;
	} else if (closure->flags & B_EV_FLAG_FORCE_REPEAT) {
		return TRUE;
	} else {
		return st;
	}
}

static void gaim_io_destroy(gpointer data)
{
	event_debug("gaim_io_destroy( 0%p )\n", data);
	g_free(data);
}

gint b_input_add(gint source, b_input_condition condition, b_event_handler function, gpointer data)
{
	GaimIOClosure *closure = g_new0(GaimIOClosure, 1);
	GIOChannel *channel;
	GIOCondition cond = 0;
	int st;

	closure->function = function;
	closure->data = data;
	closure->flags = condition;

	if (condition & B_EV_IO_READ) {
		cond |= GAIM_READ_COND;
	}
	if (condition & B_EV_IO_WRITE) {
		cond |= GAIM_WRITE_COND;
	}

	channel = g_io_channel_unix_new(source);
	st = g_io_add_watch_full(channel, G_PRIORITY_DEFAULT, cond,
	                         gaim_io_invoke, closure, gaim_io_destroy);

	event_debug("b_input_add( %d, %d, %p, %p ) = %d (%p)\n", source, condition, function, data, st, closure);

	g_io_channel_unref(channel);
	return st;
}

gint b_timeout_add(gint timeout, b_event_handler func, gpointer data)
{
	/* GSourceFunc and the BitlBee event handler function aren't
	   really the same, but they're "compatible". ;-) It will do
	   for now, BitlBee only looks at the "data" argument. */
	gint st = g_timeout_add(timeout, (GSourceFunc) func, data);

	event_debug("b_timeout_add( %d, %p, %p ) = %d\n", timeout, func, data, st);

	return st;
}

void b_event_remove(gint tag)
{
	event_debug("b_event_remove( %d )\n", tag);

	if (tag > 0) {
		g_source_remove(tag);
	}
}

void closesocket(int fd)
{
	close(fd);
}
