/********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2013 Wilmer van der Gaast and others                *
  \********************************************************************/

/* Account management functions                                         */

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

#ifndef _ACCOUNT_H
#define _ACCOUNT_H

typedef struct account {
	struct prpl *prpl;
	char *user;
	char *pass;
	char *server;
	char *tag;

	int auto_connect;
	int auto_reconnect_delay;
	int reconnect;
	int flags;

	set_t *set;
	GHashTable *nicks;

	struct bee *bee;
	struct im_connection *ic;
	struct account *next;
} account_t;

account_t *account_add(bee_t *bee, struct prpl *prpl, char *user, char *pass);
account_t *account_get(bee_t *bee, const char *id);
account_t *account_by_tag(bee_t *bee, const char *tag);
void account_del(bee_t *bee, account_t *acc);
void account_on(bee_t *bee, account_t *a);
void account_off(bee_t *bee, account_t *a);

char *set_eval_account(set_t *set, char *value);
char *set_eval_account_reconnect_delay(set_t *set, char *value);
int account_reconnect_delay(account_t *a);

int protocol_account_islocal(const char* protocol);

typedef enum {
	ACC_SET_OFFLINE_ONLY = 0x02,    /* Allow changes only if the acct is offline. */
	ACC_SET_ONLINE_ONLY = 0x04,     /* Allow changes only if the acct is online. */
	ACC_SET_LOCKABLE = 0x08         /* Setting cannot be changed if the account is locked down */
} account_set_flag_t;

typedef enum {
	ACC_FLAG_AWAY_MESSAGE = 0x01,   /* Supports away messages instead of just states. */
	ACC_FLAG_STATUS_MESSAGE = 0x02, /* Supports status messages (without being away). */
	ACC_FLAG_HANDLE_DOMAINS = 0x04, /* Contact handles need a domain portion. */
	ACC_FLAG_LOCAL = 0x08,          /* Contact list is local. */
	ACC_FLAG_LOCKED = 0x10,         /* Account is locked (cannot be deleted, certain settings can't changed) */
} account_flag_t;

#endif
