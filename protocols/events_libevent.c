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
#include <sys/types.h>
#include "proxy.h"

#include <sys/time.h>
#include <event.h>

static guint id_next;
static GHashTable *id_hash;

struct b_event_data
{
	guint id;
	struct event evinfo;
	b_event_handler function;
	void *data;
};

void b_main_init()
{
	event_init();
	
	id_next = 1;
	id_hash = g_hash_table_new( g_int_hash, g_int_equal );
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
	
	if( fd >= 0 )
	{
		if( event & EV_READ )
			cond |= GAIM_INPUT_READ;
		if( event & EV_WRITE )
			cond |= GAIM_INPUT_WRITE;
	}
	
	event_debug( "b_event_passthrough( %d, %d, 0x%x ) (%d)\n", fd, event, (int) data, b_ev->id );
	
	if( !b_ev->function( b_ev->data, fd, cond ) )
	{
		event_debug( "Handler returned FALSE: " );
		b_event_remove( b_ev->id );
	}
}

gint b_input_add( gint fd, b_input_condition condition, b_event_handler function, gpointer data )
{
	struct b_event_data *b_ev = g_new0( struct b_event_data, 1 );
	GIOCondition out_cond;
	
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
	
	event_debug( "b_input_add( %d, %d, 0x%x, 0x%x ) = %d\n", fd, condition, function, data, b_ev->id );
	
	g_hash_table_insert( id_hash, &b_ev->id, b_ev );
	
	return b_ev->id;
}

/* TODO: Persistence for timers! */
gint b_timeout_add( gint timeout, b_event_handler function, gpointer data )
{
	struct b_event_data *b_ev = g_new0( struct b_event_data, 1 );
	struct timeval tv;
	
	b_ev->id = id_next++;
	b_ev->function = function;
	b_ev->data = data;
	
	tv.tv_sec = timeout / 1000;
	tv.tv_usec = ( timeout % 1000 ) * 1000;
	
	evtimer_set( &b_ev->evinfo, b_event_passthrough, b_ev );
	evtimer_add( &b_ev->evinfo, &tv );
	
	event_debug( "b_timeout_add( %d, %d, 0x%x ) = %d\n", timeout, function, data, b_ev->id );
	
	g_hash_table_insert( id_hash, &b_ev->id, b_ev );
	
	return b_ev->id;
}

void b_event_remove( gint id )
{
	struct b_event_data *b_ev = g_hash_table_lookup( id_hash, &id );
	
	event_debug( "b_event_remove( %d )\n", id );
	if( b_ev )
	{
		event_del( &b_ev->evinfo );
		g_hash_table_remove( id_hash, &b_ev->id );
		g_free( b_ev );
	}
	else
	{
		event_debug( "Invalid?\n" );
	}
}

gboolean b_event_remove_by_data( gpointer data )
{
	/* FIXME! */
	event_debug( "FALSE!\n" );
	return FALSE;
}
