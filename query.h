/********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2004 Wilmer van der Gaast and others                *
  \********************************************************************/

/* Questions to the user (mainly authorization requests from IM)        */

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
  if not, write to the Free Software Foundation, Inc., 51 Franklin St.,
  Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef _QUERY_H
#define _QUERY_H

typedef void (*query_callback) (void *data);

typedef struct query {
	struct im_connection *ic;
	char *question;
	query_callback yes, no, free;
	void *data;
	struct query *next;
} query_t;

query_t *query_add(irc_t *irc, struct im_connection *ic, char *question,
                   query_callback yes, query_callback no, query_callback free,
                   void *data);
void query_del(irc_t *irc, query_t *q);
void query_del_by_conn(irc_t *irc, struct im_connection *ic);
void query_answer(irc_t *irc, query_t *q, int ans);

#endif
