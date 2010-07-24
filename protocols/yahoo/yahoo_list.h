/*
 * yahoo_list.h: linked list routines
 *
 * Some code copyright (C) 2002-2004, Philip S Tellis <philip.tellis AT gmx.net>
 * Other code copyright Meredydd Luff <meredydd AT everybuddy.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef __YLIST_H__
#define __YLIST_H__

/* BitlBee already uses GLib so use it. */

typedef GList YList;

#define y_list_append g_list_append
#define y_list_concat g_list_concat
#define y_list_copy g_list_copy
#define y_list_empty g_list_empty
#define y_list_find g_list_find
#define y_list_find_custom g_list_find_custom
#define y_list_foreach g_list_foreach
#define y_list_free g_list_free
#define y_list_free_1 g_list_free_1
#define y_list_insert_sorted g_list_insert_sorted
#define y_list_length g_list_length
#define y_list_next g_list_next
#define y_list_nth g_list_nth
#define y_list_prepend g_list_prepend
#define y_list_remove g_list_remove
#define y_list_remove_link g_list_remove_link
#define y_list_singleton g_list_singleton

#endif
