/*
 * yahoo_list.c: linked list routines
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
 * Some of this code was borrowed from elist.c in the eb-lite sources
 *
 */

#include <stdlib.h>

#include "yahoo_list.h"

YList *y_list_append(YList * list, void *data)
{
	YList *n;
	YList *new_list = malloc(sizeof(YList));
	YList *attach_to = NULL;

	new_list->next = NULL;
	new_list->data = data;

	for (n = list; n != NULL; n = n->next) {
		attach_to = n;
	}

	if (attach_to == NULL) {
		new_list->prev = NULL;
		return new_list;
	} else {
		new_list->prev = attach_to;
		attach_to->next = new_list;
		return list;
	}
}

YList *y_list_prepend(YList * list, void *data)
{
	YList *n = malloc(sizeof(YList));

	n->next = list;
	n->prev = NULL;
	n->data = data;
	if (list)
		list->prev = n;

	return n;
}

YList *y_list_concat(YList * list, YList * add)
{
	YList *l;

	if(!list)
		return add;

	if(!add)
		return list;

	for (l = list; l->next; l = l->next)
		;

	l->next = add;
	add->prev = l;

	return list;
}

YList *y_list_remove(YList * list, void *data)
{
	YList *n;

	for (n = list; n != NULL; n = n->next) {
		if (n->data == data) {
			list=y_list_remove_link(list, n);
			y_list_free_1(n);
			break;
		}
	}

	return list;
}

/* Warning */
/* link MUST be part of list */
/* caller must free link using y_list_free_1 */
YList *y_list_remove_link(YList * list, const YList * link)
{
	if (!link)
		return list;

	if (link->next)
		link->next->prev = link->prev;
	if (link->prev)
		link->prev->next = link->next;

	if (link == list)
		list = link->next;
	
	return list;
}

int y_list_length(const YList * list)
{
	int retval = 0;
	const YList *n = list;

	for (n = list; n != NULL; n = n->next) {
		retval++;
	}

	return retval;
}

/* well, you could just check for list == NULL, but that would be
 * implementation dependent
 */
int y_list_empty(const YList * list)
{
	if(!list)
		return 1;
	else
		return 0;
}

int y_list_singleton(const YList * list)
{
	if(!list || list->next)
		return 0;
	return 1;
}

YList *y_list_copy(YList * list)
{
	YList *n;
	YList *copy = NULL;

	for (n = list; n != NULL; n = n->next) {
		copy = y_list_append(copy, n->data);
	}

	return copy;
}

void y_list_free_1(YList * list)
{
	free(list);
}

void y_list_free(YList * list)
{
	YList *n = list;

	while (n != NULL) {
		YList *next = n->next;
		free(n);
		n = next;
	}
}

YList *y_list_find(YList * list, const void *data)
{
	YList *l;
	for (l = list; l && l->data != data; l = l->next)
		;

	return l;
}

void y_list_foreach(YList * list, YListFunc fn, void * user_data)
{
	for (; list; list = list->next)
		fn(list->data, user_data);
}

YList *y_list_find_custom(YList * list, const void *data, YListCompFunc comp)
{
	YList *l;
	for (l = list; l; l = l->next)
		if (comp(l->data, data) == 0)
			return l;

	return NULL;
}

YList *y_list_nth(YList * list, int n)
{
	int i=n;
	for ( ; list && i; list = list->next, i--)
		;

	return list;
}

YList *y_list_insert_sorted(YList * list, void *data, YListCompFunc comp)
{
	YList *l, *n, *prev = NULL;
	if (!list)
		return y_list_append(list, data);

       	n = malloc(sizeof(YList));
	n->data = data;
	for (l = list; l && comp(l->data, n->data) <= 0; l = l->next)
		prev = l;

	if (l) {
		n->prev = l->prev;
		l->prev = n;
	} else
		n->prev = prev;

	n->next = l;

	if(n->prev) {
		n->prev->next = n;
		return list;
	} else {
		return n;
	}
		
}
