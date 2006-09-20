/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Jabber module - Misc. stuff                                              *
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

static int next_id = 1;

char *set_eval_resprio( set_t *set, char *value )
{
	account_t *acc = set->data;
	
	/* Only run this stuff if the account is online ATM. */
	if( acc->gc )
	{
		/* ... */
	}
	
	if( g_strcasecmp( set->key, "priority" ) == 0 )
		return set_eval_int( set, value );
	else
		return value;
}

char *set_eval_tls( set_t *set, char *value )
{
	if( g_strcasecmp( value, "try" ) == 0 )
		return value;
	else
		return set_eval_bool( set, value );
}

struct xt_node *jabber_make_packet( char *name, char *type, char *to, struct xt_node *children )
{
	char *id = g_strdup_printf( "BeeX%04x", next_id++ );
	struct xt_node *node;
	
	node = xt_new_node( name, NULL, children );
	
	xt_add_attr( node, "id", id );
	if( type )
		xt_add_attr( node, "type", type );
	if( to )
		xt_add_attr( node, "to", to );
	
	g_free( id );
	
	return node;
}
