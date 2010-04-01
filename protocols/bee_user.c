  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2010 Wilmer van der Gaast and others                *
  \********************************************************************/

/* Stuff to handle, save and search buddies                             */

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
#include "bitlbee.h"

bee_user_t *bee_user_new( bee_t *bee, struct im_connection *ic, const char *handle )
{
	bee_user_t *bu;
	
	if( bee_user_by_handle( bee, ic, handle ) != NULL )
		return NULL;
	
	bu = g_new0( bee_user_t, 1 );
	bu->bee = bee;
	bu->ic = ic;
	bu->handle = g_strdup( handle );
	bee->users = g_slist_prepend( bee->users, bu );
	
	if( bee->ui->user_new )
		bee->ui->user_new( bee, bu );
	
	return bu;
}

int bee_user_free( bee_t *bee, struct im_connection *ic, const char *handle )
{
	bee_user_t *bu;
	
	if( ( bu = bee_user_by_handle( bee, ic, handle ) ) == NULL )
		return 0;
	
	if( bee->ui->user_free )
		bee->ui->user_free( bee, bu );
	
	g_free( bu->handle );
	g_free( bu->fullname );
	g_free( bu->group );
	g_free( bu->status );
	g_free( bu->status_msg );
	
	bee->users = g_slist_remove( bee->users, bu );
	
	return 1;
}

bee_user_t *bee_user_by_handle( bee_t *bee, struct im_connection *ic, const char *handle )
{
	GSList *l;
	
	for( l = bee->users; l; l = l->next )
	{
		bee_user_t *bu = l->data;
		
		if( bu->ic == ic && ic->acc->prpl->handle_cmp( bu->handle, handle ) )
			return bu;
	}
	
	return NULL;
}

int bee_user_msg( bee_t *bee, bee_user_t *bu, const char *msg, int flags )
{
	char *buf = NULL;
	int st;
	
	if( ( bu->ic->flags & OPT_DOES_HTML ) && ( g_strncasecmp( msg, "<html>", 6 ) != 0 ) )
	{
		buf = escape_html( msg );
		msg = buf;
	}
	
	st = bu->ic->acc->prpl->buddy_msg( bu->ic, bu->handle, msg, flags );
	g_free( buf );
	
	return st;
}


/* IM->UI callbacks */
void imcb_buddy_status( struct im_connection *ic, const char *handle, int flags, const char *state, const char *message )
{
	bee_t *bee = ic->bee;
	bee_user_t *bu, *old;
	
	if( !( bu = bee_user_by_handle( bee, ic, handle ) ) )
	{
		if( g_strcasecmp( set_getstr( &ic->bee->set, "handle_unknown" ), "add" ) == 0 )
		{
			bu = bee_user_new( bee, ic, handle );
		}
		else
		{
			if( set_getbool( &ic->bee->set, "debug" ) || g_strcasecmp( set_getstr( &ic->bee->set, "handle_unknown" ), "ignore" ) != 0 )
			{
				imcb_log( ic, "imcb_buddy_status() for unknown handle %s:", handle );
				imcb_log( ic, "flags = %d, state = %s, message = %s", flags,
				          state ? state : "NULL", message ? message : "NULL" );
			}
			
			return;
		}
	}
	
	/* May be nice to give the UI something to compare against. */
	old = g_memdup( bu, sizeof( bee_user_t ) );
	
	/* TODO(wilmer): OPT_AWAY, or just state == NULL ? */
	bu->flags = ( flags & OPT_LOGGED_IN ? BEE_USER_ONLINE : 0 ) |
	            ( flags & OPT_AWAY ? BEE_USER_AWAY : 0 );
	bu->status = g_strdup( ( flags & OPT_AWAY ) && state == NULL ? "Away" : state );
	bu->status_msg = g_strdup( message );
	
	if( bee->ui->user_status )
		bee->ui->user_status( bee, bu, old );
	
	g_free( old->status_msg );
	g_free( old->status );
	g_free( old );
#if 0	
	oa = u->away != NULL;
	oo = u->online;
	
	g_free( u->away );
	g_free( u->status_msg );
	u->away = u->status_msg = NULL;
	
	if( ( flags & OPT_LOGGED_IN ) && !u->online )
	{
		irc_spawn( ic->irc, u );
		u->online = 1;
	}
	else if( !( flags & OPT_LOGGED_IN ) && u->online )
	{
		struct groupchat *c;
		
		irc_kill( ic->irc, u );
		u->online = 0;
		
		/* Remove him/her from the groupchats to prevent PART messages after he/she QUIT already */
		for( c = ic->groupchats; c; c = c->next )
			remove_chat_buddy_silent( c, handle );
	}
	
	if( flags & OPT_AWAY )
	{
		if( state && message )
		{
			u->away = g_strdup_printf( "%s (%s)", state, message );
		}
		else if( state )
		{
			u->away = g_strdup( state );
		}
		else if( message )
		{
			u->away = g_strdup( message );
		}
		else
		{
			u->away = g_strdup( "Away" );
		}
	}
	else
	{
		u->status_msg = g_strdup( message );
	}
	
	/* LISPy... */
	if( ( set_getbool( &ic->bee->set, "away_devoice" ) ) &&		/* Don't do a thing when user doesn't want it */
	    ( u->online ) &&						/* Don't touch offline people */
	    ( ( ( u->online != oo ) && !u->away ) ||			/* Voice joining people */
	      ( ( u->online == oo ) && ( oa == !u->away ) ) ) )		/* (De)voice people changing state */
	{
		char *from;
		
		if( set_getbool( &ic->bee->set, "simulate_netsplit" ) )
		{
			from = g_strdup( ic->irc->myhost );
		}
		else
		{
			from = g_strdup_printf( "%s!%s@%s", ic->irc->mynick, ic->irc->mynick,
			                                    ic->irc->myhost );
		}
		irc_write( ic->irc, ":%s MODE %s %cv %s", from, ic->irc->channel,
		                                          u->away?'-':'+', u->nick );
		g_free( from );
	}
#endif
}

void imcb_buddy_msg( struct im_connection *ic, const char *handle, char *msg, uint32_t flags, time_t sent_at )
{
#if 0
	bee_t *bee = ic->bee;
	char *wrapped;
	user_t *u;
	
	u = user_findhandle( ic, handle );
	
	if( !u )
	{
		char *h = set_getstr( &bee->set, "handle_unknown" );
		
		if( g_strcasecmp( h, "ignore" ) == 0 )
		{
			if( set_getbool( &bee->set, "debug" ) )
				imcb_log( ic, "Ignoring message from unknown handle %s", handle );
			
			return;
		}
		else if( g_strncasecmp( h, "add", 3 ) == 0 )
		{
			int private = set_getbool( &bee->set, "private" );
			
			if( h[3] )
			{
				if( g_strcasecmp( h + 3, "_private" ) == 0 )
					private = 1;
				else if( g_strcasecmp( h + 3, "_channel" ) == 0 )
					private = 0;
			}
			
			imcb_add_buddy( ic, handle, NULL );
			u = user_findhandle( ic, handle );
			u->is_private = private;
		}
		else
		{
			imcb_log( ic, "Message from unknown handle %s:", handle );
			u = user_find( irc, irc->mynick );
		}
	}
	
	if( ( g_strcasecmp( set_getstr( &ic->bee->set, "strip_html" ), "always" ) == 0 ) ||
	    ( ( ic->flags & OPT_DOES_HTML ) && set_getbool( &ic->bee->set, "strip_html" ) ) )
		strip_html( msg );

	wrapped = word_wrap( msg, 425 );
	irc_msgfrom( irc, u->nick, wrapped );
	g_free( wrapped );
#endif
}
