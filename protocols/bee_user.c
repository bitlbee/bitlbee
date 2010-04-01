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
