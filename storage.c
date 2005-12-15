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

static storage_t *storage_init_single(const char *name)
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

GList *storage_init(const char *primary, char **migrate)
{
	GList *ret = NULL;
	int i;
	storage_t *storage;

	storage = storage_init_single(primary);
	if (storage == NULL)
		return NULL;

	ret = g_list_append(ret, storage);

	for (i = 0; migrate && migrate[i]; i++) {
		storage = storage_init_single(migrate[i]);
	
		if (storage)
			ret = g_list_append(ret, storage);
	}

	return ret;
}

storage_status_t storage_check_pass (const char *nick, const char *password)
{
	GList *gl;
	
	/* Loop until we don't get NO_SUCH_USER */

	for (gl = global.storage; gl; gl = gl->next) {
		storage_t *st = gl->data;
		storage_status_t status;

		status = st->check_pass(nick, password);
		if (status != STORAGE_NO_SUCH_USER)
			return status;
	}
	
	return STORAGE_NO_SUCH_USER;
}

storage_status_t storage_load (const char *nick, const char *password, irc_t * irc)
{
	GList *gl;
	
	/* Loop until we don't get NO_SUCH_USER */
	for (gl = global.storage; gl; gl = gl->next) {
		storage_t *st = gl->data;
		storage_status_t status;

		status = st->load(nick, password, irc);
		if (status == STORAGE_OK) {
			irc_setpass(irc, password);
			return status;
		}
		
		if (status != STORAGE_NO_SUCH_USER) 
			return status;
	}
	
	return STORAGE_NO_SUCH_USER;
}

storage_status_t storage_save (irc_t *irc, int overwrite)
{
	return ((storage_t *)global.storage->data)->save(irc, overwrite);
}

storage_status_t storage_remove (const char *nick, const char *password)
{
	GList *gl;
	storage_status_t ret = STORAGE_OK;
	
	/* Remove this account from all storage backends. If this isn't 
	 * done, the account will still be usable, it'd just be 
	 * loaded from a different backend. */
	for (gl = global.storage; gl; gl = gl->next) {
		storage_t *st = gl->data;
		storage_status_t status;

		status = st->remove(nick, password);
		if (status != STORAGE_NO_SUCH_USER && 
			status != STORAGE_OK)
			ret = status;
	}
	
	return ret;
}

storage_status_t storage_rename (const char *onick, const char *nnick, const char *password)
{
	storage_status_t status;
	GList *gl = global.storage;
	storage_t *primary_storage = gl->data;
	irc_t *irc;

	/* First, try to rename in the current write backend, assuming onick 
	 * is stored there */
	status = primary_storage->rename(onick, nnick, password);
	if (status != STORAGE_NO_SUCH_USER)
		return status;

	/* Try to load from a migration backend and save to the current backend. 
	 * Explicitly remove the account from the migration backend as otherwise 
	 * it'd still be usable under the old name */
	
	irc = g_new0(irc_t, 1);
	status = storage_load(onick, password, irc);
	if (status != STORAGE_OK) {
		irc_free(irc);
		return status;
	}

	g_free(irc->nick);
	irc->nick = g_strdup(nnick);

	status = storage_save(irc, FALSE);
	if (status != STORAGE_OK) {
		irc_free(irc);
		return status;
	}
	irc_free(irc);

	storage_remove(onick, password);

	return STORAGE_OK;
}
