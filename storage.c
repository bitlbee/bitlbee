  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2004 Wilmer van der Gaast and others                *
  \********************************************************************/

/* Support for multiple storage backends */

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
#include "crypting.h"

extern storage_t storage_text;

static GList text_entry = { &storage_text, NULL, NULL };
static GList *storage_backends = &text_entry;

void register_storage_backend(storage_t *backend)
{
	storage_backends = g_list_append(storage_backends, backend);
}

storage_t *storage_init(const char *name)
{
	GList *gl;
	storage_t *st;

	for (gl = storage_backends; gl; gl = gl->next) {
		st = gl->data;
		if (strcmp(st->name, name) == 0)
			break;
	}

	if (gl == NULL) 
		return NULL;

	if (st->init)
		st->init();

	return st;
}

storage_status_t storage_check_pass (const char *nick, const char *password)
{
	return global.storage->check_pass(nick, password);
}

storage_status_t storage_load (const char *nick, const char *password, irc_t * irc)
{
	return global.storage->load(nick, password, irc);
}

storage_status_t storage_save (irc_t *irc, int overwrite)
{
	return global.storage->save(irc, overwrite);
}

storage_status_t storage_remove (const char *nick, const char *password)
{
	return global.storage->remove(nick, password);
}

storage_status_t storage_rename (const char *onick, const char *nnick, const char *password)
{
	return global.storage->rename(onick, nnick, password);
}
