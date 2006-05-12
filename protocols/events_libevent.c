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
	
	if( !b_ev->function( b_ev->data, fd, cond ) )
		b_event_remove( b_ev->id );
}

gint b_input_add( gint source, b_input_condition condition, b_event_handler function, gpointer data )
{
	struct b_event_data *b_ev = g_new0( struct b_event_data, 1 );
	GIOCondition cond;
	
	b_ev->id = id_next++;
	b_ev->function = function;
	b_ev->data = data;
	
	cond = EV_PERSIST;
	if( condition & GAIM_INPUT_READ )
		cond |= EV_READ;
	if( condition & GAIM_INPUT_WRITE )
		cond |= EV_WRITE;
	
	event_set( &b_ev->evinfo, source, cond, b_event_passthrough, b_ev );
	event_add( &b_ev->evinfo, NULL );
	
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
	
	g_hash_table_insert( id_hash, &b_ev->id, b_ev );
	
	return b_ev->id;
}

void b_event_remove( gint tag )
{
	struct b_event_data *b_ev = g_hash_table_lookup( id_hash, &tag );
	
	if( b_ev )
	{
		event_del( &b_ev->evinfo );
		g_hash_table_remove( id_hash, &b_ev->id );
		g_free( b_ev );
	}
}

gboolean b_event_remove_by_data( gpointer data )
{
	/* FIXME! */
	return FALSE;
}
