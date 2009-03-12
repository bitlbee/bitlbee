  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2008 Wilmer van der Gaast and others                *
  \********************************************************************/

/* Keep track of chatrooms the user is interested in                    */

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

#include "bitlbee.h"

struct chat *chat_add( irc_t *irc, account_t *acc, char *handle, char *channel )
{
	struct chat *c, *l;
	set_t *s;

	if( acc->prpl->chat_join == NULL || !chat_chanok( channel ) ||
	    chat_chancmp( channel, irc->channel ) == 0 )
	{
		return NULL;
	}
	
	for( c = irc->chatrooms; c; c = c->next )
	{
		if( chat_chancmp( channel, c->channel ) == 0 )
			return NULL;
		
		if( acc == c->acc && g_strcasecmp( handle, c->handle ) == 0 )
			return NULL;
		
		l = c;
	}
	
	if( irc->chatrooms == NULL )
		irc->chatrooms = c = g_new0( struct chat, 1 );
	else
		l->next = c = g_new0( struct chat, 1 );
	
	c->acc = acc;
	c->handle = g_strdup( handle );
	c->channel = g_strdup( channel );
	
	s = set_add( &c->set, "auto_join", "false", set_eval_bool, c );
	/* s = set_add( &c->set, "auto_rejoin", "false", set_eval_bool, c ); */
	s = set_add( &c->set, "nick", NULL, NULL, c );
	s->flags |= SET_NULL_OK;
	
	return c;
}

struct chat *chat_byhandle( irc_t *irc, account_t *acc, char *handle )
{
	struct chat *c;
	
	for( c = irc->chatrooms; c; c = c->next )
	{
		if( acc == c->acc && g_strcasecmp( handle, c->handle ) == 0 )
			break;
	}
	
	return c;
}

struct chat *chat_bychannel( irc_t *irc, char *channel )
{
	struct chat *c;
	
	for( c = irc->chatrooms; c; c = c->next )
	{
		if( chat_chancmp( channel, c->channel ) == 0 )
			break;
	}
	
	return c;
}

struct chat *chat_get( irc_t *irc, char *id )
{
	struct chat *c, *ret = NULL;
	int nr;
	
	if( sscanf( id, "%d", &nr ) == 1 && nr < 1000 )
	{
		for( c = irc->chatrooms; c; c = c->next )
			if( ( nr-- ) == 0 )
				return c;
		
		return NULL;
	}
	
	for( c = irc->chatrooms; c; c = c->next )
	{
		if( strstr( c->handle, id ) )
		{
			if( !ret )
				ret = c;
			else
				return NULL;
		}
		else if( strstr( c->channel, id ) )
		{
			if( !ret )
				ret = c;
			else
				return NULL;
		}
	}
	
	return ret;
}

int chat_del( irc_t *irc, struct chat *chat )
{
	struct chat *c, *l = NULL;
	
	for( c = irc->chatrooms; c; c = (l=c)->next )
		if( c == chat )
			break;
	
	if( c == NULL )
		return 0;
	else if( l == NULL )
		irc->chatrooms = c->next;
	else
		l->next = c->next;
	
	while( c->set )
		set_del( &c->set, c->set->key );
	
	g_free( c->handle );
	g_free( c->channel );
	g_free( c );
	
	return 1;
}

int chat_chancmp( char *a, char *b )
{
	if( !chat_chanok( a ) || !chat_chanok( b ) )
		return 0;
	
	if( a[0] == b[0] )
		return nick_cmp( a + 1, b + 1 );
	else
		return -1;
}

int chat_chanok( char *a )
{
	if( strchr( CTYPES, a[0] ) != NULL )
		return nick_ok( a + 1 );
	else
		return 0;
}

int chat_join( irc_t *irc, struct chat *c, const char *password )
{
	struct groupchat *gc;
	char *nick = set_getstr( &c->set, "nick" );

	if( c->acc->ic == NULL || c->acc->prpl->chat_join == NULL )
		return 0;
	
	if( nick == NULL )
		nick = irc->nick;
	
	if( ( gc = c->acc->prpl->chat_join( c->acc->ic, c->handle, nick, password ) ) )
	{
		g_free( gc->channel );
		gc->channel = g_strdup( c->channel );
		return 1;
	}
	
	return 0;
}
