  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2004 Wilmer van der Gaast and others                *
  \********************************************************************/

/* Account management functions                                         */

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
#include "account.h"

account_t *account_add( irc_t *irc, struct prpl *prpl, char *user, char *pass )
{
	account_t *a;
	
	if( irc->accounts )
	{
		for( a = irc->accounts; a->next; a = a->next );
		a = a->next = g_new0 ( account_t, 1 );
	}
	else
	{
		irc->accounts = a = g_new0 ( account_t, 1 );
	}
	
	a->prpl = prpl;
	a->user = g_strdup( user );
	a->pass = g_strdup( pass );
	a->irc = irc;
	
	return( a );
}

account_t *account_get( irc_t *irc, char *id )
{
	account_t *a, *ret = NULL;
	int nr;
	
	if( sscanf( id, "%d", &nr ) == 1 && nr < 1000 )
	{
		for( a = irc->accounts; a; a = a->next )
			if( ( nr-- ) == 0 )
				return( a );
		
		return( NULL );
	}
	
	for( a = irc->accounts; a; a = a->next )
	{
		if( g_strcasecmp( id, a->prpl->name ) == 0 )
		{
			if( !ret )
				ret = a;
			else
				return( NULL ); /* We don't want to match more than one... */
		}
		else if( strstr( a->user, id ) )
		{
			if( !ret )
				ret = a;
			else
				return( NULL );
		}
	}
	
	return( ret );
}

void account_del( irc_t *irc, account_t *acc )
{
	account_t *a, *l = NULL;
	
	for( a = irc->accounts; a; a = (l=a)->next )
		if( a == acc )
		{
			if( a->gc ) return; /* Caller should have checked, accounts still in use can't be deleted. */
			
			if( l )
			{
				l->next = a->next;
			}
			else
			{
				irc->accounts = a->next;
			}
			
			g_free( a->user );
			g_free( a->pass );
			if( a->server ) g_free( a->server );
			if( a->reconnect )	/* This prevents any reconnect still queued to happen */
				cancel_auto_reconnect( a );
			g_free( a );
			
			break;
		}
}

void account_on( irc_t *irc, account_t *a )
{
	struct aim_user *u;
	
	if( a->gc )
	{
		/* Trying to enable an already-enabled account */
		return;
	}
	
	cancel_auto_reconnect( a );
	
	u = g_new0 ( struct aim_user, 1 );
	u->irc = irc;
	u->prpl = a->prpl;
	strncpy( u->username, a->user, sizeof( u->username ) - 1 );
	strncpy( u->password, a->pass, sizeof( u->password ) - 1 );
	if( a->server) strncpy( u->proto_opt[0], a->server, sizeof( u->proto_opt[0] ) - 1 );
	
	a->gc = (struct gaim_connection *) u; /* Bit hackish :-/ */
	a->reconnect = 0;
	
	a->prpl->login( u );
}

void account_off( irc_t *irc, account_t *a )
{
	account_offline( a->gc );
	a->gc = NULL;
	if( a->reconnect )
	{
		/* Shouldn't happen */
		cancel_auto_reconnect( a );
	}
}
