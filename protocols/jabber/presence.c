/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Jabber module - Handling of presence (tags), etc                         *
*                                                                           *
*  Copyright 2006 Wilmer van der Gaast <wilmer@gaast.net>                   *
*                                                                           *
*  This program is free software; you can redistribute it and/or modify     *
*  it under the terms of the GNU General Public License as published by     *
*  the Free Software Foundation; either version 2 of the License, or        *
*  (at your option) any later version.                                      *
*                                                                           *
*  This program is distributed in the hope that it will be useful,          *
*  but WITHOUT ANY WARRANTY; without even the implied warranty of           *
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
*  GNU General Public License for more details.                             *
*                                                                           *
*  You should have received a copy of the GNU General Public License along  *
*  with this program; if not, write to the Free Software Foundation, Inc.,  *
*  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.              *
*                                                                           *
\***************************************************************************/

#include "jabber.h"

xt_status jabber_pkt_presence( struct xt_node *node, gpointer data )
{
	struct gaim_connection *gc = data;
	char *from = xt_find_attr( node, "from" );
	char *type = xt_find_attr( node, "type" );	/* NULL should mean the person is online. */
	char *s;
	
	if( !from )
		return XT_HANDLED;
	
	s = strchr( from, '/' );
	if( s )
		*s = 0;
	
	if( type == NULL )
		serv_got_update( gc, from, 1, 0, 0, 0, 0, 0 );
	else if( strcmp( type, "unavailable" ) == 0 )
		serv_got_update( gc, from, 0, 0, 0, 0, 0, 0 );
	else
	{
		printf( "Received PRES from %s:\n", from );
		xt_print( node );
	}
	
	if( s )
		*s = '/';
	
	return XT_HANDLED;
}

int presence_send( struct gaim_connection *gc, char *to, char *show, char *status )
{
	struct xt_node *node;
	int st;
	
	node = jabber_make_packet( "presence", NULL, to, NULL );
	if( show && *show )
		xt_add_child( node, xt_new_node( "show", show, NULL ) );
	if( status )
		xt_add_child( node, xt_new_node( "status", status, NULL ) );
	
	st = jabber_write_packet( gc, node );
	
	xt_free_node( node );
	return st;
}
