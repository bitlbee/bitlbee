  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2004 Wilmer van der Gaast and others                *
  \********************************************************************/

/* Storage backend that uses the LDB embedded LDAP-like database */

/* Copyright (C) 2006 Jelmer Vernooij <jelmer@samba.org> */

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
#include <ldb.h>

static void sldb_init (void)
{
}

static storage_status_t sldb_load ( const char *my_nick, const char* password, irc_t *irc )
{
	return STORAGE_OK;
}

static storage_status_t sldb_save( irc_t *irc, int overwrite )
{
	return STORAGE_OK;
}

static storage_status_t sldb_check_pass( const char *nick, const char *password )
{
	return STORAGE_OK;
}

static storage_status_t sldb_remove( const char *nick, const char *password )
{
	return STORAGE_OK;
}

storage_t storage_ldb = {
	.name = "ldb",
	.init = sldb_init,
	.check_pass = sldb_check_pass,
	.remove = sldb_remove,
	.load = sldb_load,
	.save = sldb_save
};
