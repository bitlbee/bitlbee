  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2004 Wilmer van der Gaast and others                *
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
  if not, write to the Free Software Foundation, Inc., 59 Temple Place,
  Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef _ACCOUNT_H
#define _ACCOUNT_H

typedef struct account
{
	struct prpl *prpl;
	char *user;
	char *pass;
	char *server;
	
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

account_t *account_add( bee_t *bee, struct prpl *prpl, char *user, char *pass );
account_t *account_get( bee_t *bee, char *id );
void account_del( bee_t *bee, account_t *acc );
void account_on( bee_t *bee, account_t *a );
void account_off( bee_t *bee, account_t *a );

char *set_eval_account( set_t *set, char *value );
char *set_eval_account_reconnect_delay( set_t *set, char *value );
int account_reconnect_delay( account_t *a );

typedef enum
{
	ACC_SET_NOSAVE = 0x01,          /* Don't save this setting (i.e. stored elsewhere). */
	ACC_SET_OFFLINE_ONLY = 0x02,    /* Allow changes only if the acct is offline. */
	ACC_SET_ONLINE_ONLY = 0x04,     /* Allow changes only if the acct is online. */
} account_set_flag_t;

typedef enum
{
	ACC_FLAG_AWAY_MESSAGE = 0x01,   /* Supports away messages instead of just states. */
	ACC_FLAG_STATUS_MESSAGE = 0x02, /* Supports status messages (without being away). */
} account_flag_t;

#endif
