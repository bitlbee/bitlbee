/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Helper functions for json.c                                              *
*                                                                           *
*  Copyright 2012 Wilmer van der Gaast <wilmer@gaast.net>                   *
*                                                                           *
*  This library is free software; you can redistribute it and/or            *
*  modify it under the terms of the GNU Lesser General Public               *
*  License as published by the Free Software Foundation, version            *
*  2.1.                                                                     *
*                                                                           *
*  This library is distributed in the hope that it will be useful,          *
*  but WITHOUT ANY WARRANTY; without even the implied warranty of           *
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU        *
*  Lesser General Public License for more details.                          *
*                                                                           *
*  You should have received a copy of the GNU Lesser General Public License *
*  along with this library; if not, write to the Free Software Foundation,  *
*  Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA           *
*                                                                           *
****************************************************************************/

#include "json.h"

#define JSON_O_FOREACH(o, k, v) \
	char *k; json_value *v; int __i; \
	for( __i = 0; ( __i < (o)->u.object.length ) && \
	              ( k = (o)->u.object.values[__i].name ) && \
	              ( v = (o)->u.object.values[__i].value ); \
	              __i ++ )

json_value *json_o_get( const json_value *obj, const json_char *name );
const char *json_o_str( const json_value *obj, const json_char *name );
char *json_o_strdup( const json_value *obj, const json_char *name );
