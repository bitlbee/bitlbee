/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Helper functions for json.c                                              *
*                                                                           *
*  Copyright 2012-2012 Wilmer van der Gaast <wilmer@gaast.net>              *
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
****************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <glib.h>

#include "json_util.h"

json_value *json_o_get(const json_value *obj, const json_char *name)
{
	int i;

	if (!obj || obj->type != json_object) {
		return NULL;
	}

	for (i = 0; i < obj->u.object.length; ++i) {
		if (strcmp(obj->u.object.values[i].name, name) == 0) {
			return obj->u.object.values[i].value;
		}
	}

	return NULL;
}

const char *json_o_str(const json_value *obj, const json_char *name)
{
	json_value *ret = json_o_get(obj, name);

	if (ret && ret->type == json_string) {
		return ret->u.string.ptr;
	} else {
		return NULL;
	}
}

char *json_o_strdup(const json_value *obj, const json_char *name)
{
	json_value *ret = json_o_get(obj, name);

	if (ret && ret->type == json_string && ret->u.string.ptr) {
		return g_memdup(ret->u.string.ptr, ret->u.string.length + 1);
	} else {
		return NULL;
	}
}
