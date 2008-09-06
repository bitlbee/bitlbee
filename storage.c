  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2004 Wilmer van der Gaast and others                *
  \********************************************************************/

/* Support for multiple storage backends */

/* Copyright (C) 2005 Jelmer Vernooij <jelmer@samba.org> */

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
extern storage_t storage_xml;

static GList *storage_backends = NULL;

void register_storage_backend(storage_t *backend)
{
	storage_backends = g_list_append(storage_backends, backend);
}

static storage_t *storage_init_single(const char *name)
{
	GList *gl;
	storage_t *st = NULL;

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
	
	register_storage_backend(&storage_text);
	register_storage_backend(&storage_xml);
	
	storage = storage_init_single(primary);
	if (storage == NULL && storage->save == NULL)
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

storage_status_t storage_load (irc_t * irc, const char *password)
{
	GList *gl;
	
	if (irc && irc->status & USTATUS_IDENTIFIED)
		return STORAGE_OTHER_ERROR;
	
	/* Loop until we don't get NO_SUCH_USER */
	for (gl = global.storage; gl; gl = gl->next) {
		storage_t *st = gl->data;
		storage_status_t status;

		status = st->load(irc, password);
		if (status == STORAGE_OK)
			return status;
		
		if (status != STORAGE_NO_SUCH_USER) 
			return status;
	}
	
	return STORAGE_NO_SUCH_USER;
}

storage_status_t storage_save (irc_t *irc, char *password, int overwrite)
{
	storage_status_t st;
	
	if (password != NULL) {
		/* Should only use this in the "register" command. */
		if (irc->password || overwrite)
			return STORAGE_OTHER_ERROR;
		
		irc_setpass(irc, password);
	} else if ((irc->status & USTATUS_IDENTIFIED) == 0) {
		return STORAGE_NO_SUCH_USER;
	}
	
	st = ((storage_t *)global.storage->data)->save(irc, overwrite);
	
	if (password != NULL) {
		irc_setpass(irc, NULL);
	}
	
	return st;
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
		if (status != STORAGE_NO_SUCH_USER && status != STORAGE_OK)
			ret = status;
	}
	
	return ret;
}

#if 0
Not using this yet. Test thoroughly before adding UI hooks to this function.

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
#endif
