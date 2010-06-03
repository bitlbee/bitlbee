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

bee_user_t *bee_user_new( bee_t *bee, struct im_connection *ic, const char *handle, bee_user_flags_t flags )
{
	bee_user_t *bu;
	
	if( bee_user_by_handle( bee, ic, handle ) != NULL )
		return NULL;
	
	bu = g_new0( bee_user_t, 1 );
	bu->bee = bee;
	bu->ic = ic;
	bu->flags = flags;
	bu->handle = g_strdup( handle );
	bee->users = g_slist_prepend( bee->users, bu );
	
	if( bee->ui->user_new )
		bee->ui->user_new( bee, bu );
	
	/* Offline by default. This will set the right flags. */
	imcb_buddy_status( ic, handle, 0, NULL, NULL );
	
	return bu;
}

int bee_user_free( bee_t *bee, bee_user_t *bu )
{
	if( !bu )
		return 0;
	
	if( bee->ui->user_free )
		bee->ui->user_free( bee, bu );
	
	g_free( bu->handle );
	g_free( bu->fullname );
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
		
		if( bu->ic == ic && ic->acc->prpl->handle_cmp( bu->handle, handle ) == 0 )
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
	else
		buf = g_strdup( msg );
	
	st = bu->ic->acc->prpl->buddy_msg( bu->ic, bu->handle, buf, flags );
	g_free( buf );
	
	return st;
}


/* Groups */
static bee_group_t *bee_group_new( bee_t *bee, const char *name )
{
	bee_group_t *bg = g_new0( bee_group_t, 1 );
	
	bg->name = g_strdup( name );
	bg->key = g_utf8_casefold( name, -1 );
	bee->groups = g_slist_prepend( bee->groups, bg );
	
	return bg;
}

bee_group_t *bee_group_by_name( bee_t *bee, const char *name, gboolean creat )
{
	GSList *l;
	char *key;
	
	if( name == NULL )
		return NULL;
	
	key = g_utf8_casefold( name, -1 );
	for( l = bee->groups; l; l = l->next )
	{
		bee_group_t *bg = l->data;
		if( strcmp( bg->key, key ) == 0 )
			break;
	}
	g_free( key );
	
	if( !l )
		return creat ? bee_group_new( bee, name ) : NULL;
	else
		return l->data;
}

void bee_group_free( bee_t *bee )
{
	while( bee->groups )
	{
		bee_group_t *bg = bee->groups->data;
		g_free( bg->name );
		g_free( bg->key );
		g_free( bg );
		bee->groups = g_slist_remove( bee->groups, bee->groups->data );
	}
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
			bu = bee_user_new( bee, ic, handle, BEE_USER_LOCAL );
		}
		else
		{
			if( g_strcasecmp( set_getstr( &ic->bee->set, "handle_unknown" ), "ignore" ) != 0 )
			{
				imcb_log( ic, "imcb_buddy_status() for unknown handle %s:\n"
				              "flags = %d, state = %s, message = %s", handle, flags,
				              state ? state : "NULL", message ? message : "NULL" );
			}
			
			return;
		}
	}
	
	/* May be nice to give the UI something to compare against. */
	old = g_memdup( bu, sizeof( bee_user_t ) );
	
	/* TODO(wilmer): OPT_AWAY, or just state == NULL ? */
	bu->flags = flags;
	bu->status = g_strdup( ( flags & OPT_AWAY ) && state == NULL ? "Away" : state );
	bu->status_msg = g_strdup( message );
	
	if( bee->ui->user_status )
		bee->ui->user_status( bee, bu, old );
	
	g_free( old->status_msg );
	g_free( old->status );
	g_free( old );
}

void imcb_buddy_msg( struct im_connection *ic, const char *handle, char *msg, uint32_t flags, time_t sent_at )
{
	bee_t *bee = ic->bee;
	bee_user_t *bu;
	
	bu = bee_user_by_handle( bee, ic, handle );
	
	if( !bu )
	{
		char *h = set_getstr( &bee->set, "handle_unknown" );
		
		if( g_strcasecmp( h, "ignore" ) == 0 )
		{
			return;
		}
		else if( g_strncasecmp( h, "add", 3 ) == 0 )
		{
			bu = bee_user_new( bee, ic, handle, BEE_USER_LOCAL );
		}
	}
	
	if( ( g_strcasecmp( set_getstr( &ic->bee->set, "strip_html" ), "always" ) == 0 ) ||
	    ( ( ic->flags & OPT_DOES_HTML ) && set_getbool( &ic->bee->set, "strip_html" ) ) )
		strip_html( msg );
	
	if( bee->ui->user_msg && bu )
		bee->ui->user_msg( bee, bu, msg, sent_at );
	else
		imcb_log( ic, "Message from unknown handle %s:\n%s", handle, msg );
}

void imcb_buddy_typing( struct im_connection *ic, char *handle, uint32_t flags )
{
	bee_user_t *bu;
	
	if( ic->bee->ui->user_typing &&
	    ( bu = bee_user_by_handle( ic->bee, ic, handle ) ) )
	{
		ic->bee->ui->user_typing( ic->bee, bu, flags );
	}
}
