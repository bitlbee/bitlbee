  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2010 Wilmer van der Gaast and others                *
  \********************************************************************/

/* Some IM-core stuff                                                   */

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

static char *set_eval_away_status( set_t *set, char *value );

bee_t *bee_new()
{
	bee_t *b = g_new0( bee_t, 1 );
	set_t *s;
	
	s = set_add( &b->set, "auto_connect", "true", set_eval_bool, b );
	s = set_add( &b->set, "auto_reconnect", "true", set_eval_bool, b );
	s = set_add( &b->set, "auto_reconnect_delay", "5*3<900", set_eval_account_reconnect_delay, b );
	s = set_add( &b->set, "away", NULL, set_eval_away_status, b );
	s->flags |= SET_NULL_OK | SET_HIDDEN;
	s = set_add( &b->set, "debug", "false", set_eval_bool, b );
	s = set_add( &b->set, "mobile_is_away", "false", set_eval_bool, b );
	s = set_add( &b->set, "save_on_quit", "true", set_eval_bool, b );
	s = set_add( &b->set, "status", NULL, set_eval_away_status, b );
	s->flags |= SET_NULL_OK;
	s = set_add( &b->set, "strip_html", "true", NULL, b );
	
	b->user = g_malloc( 1 );
	
	return b;
}

void bee_free( bee_t *b )
{
	while( b->accounts )
	{
		if( b->accounts->ic )
			imc_logout( b->accounts->ic, FALSE );
		else if( b->accounts->reconnect )
			cancel_auto_reconnect( b->accounts );
		
		if( b->accounts->ic == NULL )
			account_del( b, b->accounts );
		else
			/* Nasty hack, but account_del() doesn't work in this
			   case and we don't want infinite loops, do we? ;-) */
			b->accounts = b->accounts->next;
	}
	
	while( b->set )
		set_del( &b->set, b->set->key );
	
	bee_group_free( b );
	
	g_free( b->user );
	g_free( b );
}

static char *set_eval_away_status( set_t *set, char *value )
{
	bee_t *bee = set->data;
	account_t *a;
	
	g_free( set->value );
	set->value = g_strdup( value );
	
	for( a = bee->accounts; a; a = a->next )
	{
		struct im_connection *ic = a->ic;
		
		if( ic && ic->flags & OPT_LOGGED_IN )
			imc_away_send_update( ic );
	}
	
	return value;
}
