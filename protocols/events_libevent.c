  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2006 Wilmer van der Gaast and others                *
  \********************************************************************/

/*
 * Event handling (using libevent)
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
  if not, write to the Free Software Foundation, Inc., 59 Temple Place,
  Suite 330, Boston, MA  02111-1307  USA
*/

#define BITLBEE_CORE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include "proxy.h"

#include <sys/time.h>
#include <event.h>

static guint id_next;
static GHashTable *id_hash;

/* Since libevent doesn't handle two event handlers for one fd-condition
   very well (which happens sometimes when BitlBee changes event handlers
   for a combination), let's buid some indexes so we can delete them here
   already, just in time. */
static GHashTable *read_hash;
static GHashTable *write_hash;

struct b_event_data
{
	guint id;
	struct event evinfo;
	gint timeout;
	b_event_handler function;
	void *data;
};

void b_main_init()
{
	event_init();
	
	id_next = 1;
	id_hash = g_hash_table_new( g_int_hash, g_int_equal );
	read_hash = g_hash_table_new( g_int_hash, g_int_equal );
	write_hash = g_hash_table_new( g_int_hash, g_int_equal );
}

void b_main_run()
{
	event_dispatch();
}

void b_main_quit()
{
	struct timeval tv;
	
	memset( &tv, 0, sizeof( struct timeval ) );
	event_loopexit( &tv );
}

static void b_event_passthrough( int fd, short event, void *data )
{
	struct b_event_data *b_ev = data;
	b_input_condition cond = 0;
	int id;
	
	if( fd >= 0 )
	{
		if( event & EV_READ )
			cond |= GAIM_INPUT_READ;
		if( event & EV_WRITE )
			cond |= GAIM_INPUT_WRITE;
	}
	
	event_debug( "b_event_passthrough( %d, %d, 0x%x ) (%d)\n", fd, event, (int) data, b_ev->id );
	
	/* Since the called function might cancel this handler already
	   (which free()s b_ev, we have to remember the ID here. */
	id = b_ev->id;
	
	if( !b_ev->function( b_ev->data, fd, cond ) )
	{
		event_debug( "Handler returned FALSE: " );
		b_event_remove( id );
	}
	else if( fd == -1 )
	{
		struct timeval tv;
		
		tv.tv_sec = b_ev->timeout / 1000;
		tv.tv_usec = ( b_ev->timeout % 1000 ) * 1000;
		
		evtimer_add( &b_ev->evinfo, &tv );
	}
}

gint b_input_add( gint fd, b_input_condition condition, b_event_handler function, gpointer data )
{
	struct b_event_data *b_ev;
	
	event_debug( "b_input_add( %d, %d, 0x%x, 0x%x ) ", fd, condition, function, data );
	
	if( ( condition & GAIM_INPUT_READ  && ( b_ev = g_hash_table_lookup( read_hash,  &fd ) ) ) ||
	    ( condition & GAIM_INPUT_WRITE && ( b_ev = g_hash_table_lookup( write_hash, &fd ) ) ) )
	{
		/* We'll stick with this libevent entry, but give it a new BitlBee id. */
		g_hash_table_remove( id_hash, &b_ev->id );
		
		event_debug( "(replacing old handler (id = %d)) = %d\n", b_ev->id, id_next );
		
		b_ev->id = id_next++;
		b_ev->function = function;
		b_ev->data = data;
	}
	else
	{
		GIOCondition out_cond;
		
		event_debug( "(new) = %d\n", id_next );
		
		b_ev = g_new0( struct b_event_data, 1 );
		b_ev->id = id_next++;
		b_ev->function = function;
		b_ev->data = data;
		
		out_cond = EV_PERSIST;
		if( condition & GAIM_INPUT_READ )
			out_cond |= EV_READ;
		if( condition & GAIM_INPUT_WRITE )
			out_cond |= EV_WRITE;
		
		event_set( &b_ev->evinfo, fd, out_cond, b_event_passthrough, b_ev );
		event_add( &b_ev->evinfo, NULL );
		
		if( out_cond & EV_READ )
			g_hash_table_insert( read_hash, &b_ev->evinfo.ev_fd, b_ev );
		if( out_cond & EV_WRITE )
			g_hash_table_insert( write_hash, &b_ev->evinfo.ev_fd, b_ev );
	}
	
	g_hash_table_insert( id_hash, &b_ev->id, b_ev );
	return b_ev->id;
}

/* TODO: Persistence for timers! */
gint b_timeout_add( gint timeout, b_event_handler function, gpointer data )
{
	struct b_event_data *b_ev = g_new0( struct b_event_data, 1 );
	struct timeval tv;
	
	b_ev->id = id_next++;
	b_ev->timeout = timeout;
	b_ev->function = function;
	b_ev->data = data;
	
	tv.tv_sec = timeout / 1000;
	tv.tv_usec = ( timeout % 1000 ) * 1000;
	
	evtimer_set( &b_ev->evinfo, b_event_passthrough, b_ev );
	evtimer_add( &b_ev->evinfo, &tv );
	
	event_debug( "b_timeout_add( %d, 0x%x, 0x%x ) = %d\n", timeout, function, data, b_ev->id );
	
	g_hash_table_insert( id_hash, &b_ev->id, b_ev );
	
	return b_ev->id;
}

void b_event_remove( gint id )
{
	struct b_event_data *b_ev = g_hash_table_lookup( id_hash, &id );
	
	event_debug( "b_event_remove( %d )\n", id );
	if( b_ev )
	{
		g_hash_table_remove( id_hash, &b_ev->id );
		if( b_ev->evinfo.ev_fd >= 0 )
		{
			if( b_ev->evinfo.ev_events & EV_READ )
				g_hash_table_remove( read_hash, &b_ev->evinfo.ev_fd );
			if( b_ev->evinfo.ev_events & EV_WRITE )
				g_hash_table_remove( write_hash, &b_ev->evinfo.ev_fd );
		}
		
		event_del( &b_ev->evinfo );
		g_free( b_ev );
	}
	else
	{
		event_debug( "Already removed?\n" );
	}
}

gboolean b_event_remove_by_data( gpointer data )
{
	/* FIXME! */
	event_debug( "FALSE!\n" );
	return FALSE;
}

void closesocket( int fd )
{
	struct b_event_data *b_ev;
	
	/* Since epoll() (the main reason we use libevent) automatically removes sockets from
	   the epoll() list when a socket gets closed and some modules have a habit of
	   closing sockets before removing event handlers, our and libevent's administration
	   get a little bit messed up. So this little function will remove the handlers
	   properly before closing a socket. */
	
	if( b_ev = g_hash_table_lookup( read_hash, &fd ) )
	{
		event_debug( "Warning: fd %d still had a read event handler when shutting down.\n", fd );
		b_event_remove( b_ev->id );
	}
	if( b_ev = g_hash_table_lookup( write_hash, &fd ) )
	{
		event_debug( "Warning: fd %d still had a write event handler when shutting down.\n", fd );
		b_event_remove( b_ev->id );
	}
	
	close( fd );
}
