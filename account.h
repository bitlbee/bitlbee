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
	
	set_t *set;
	GHashTable *nicks;
	
	struct irc *irc;
	struct im_connection *ic;
	struct account *next;
} account_t;

account_t *account_add( irc_t *irc, struct prpl *prpl, char *user, char *pass );
account_t *account_get( irc_t *irc, char *id );
void account_del( irc_t *irc, account_t *acc );
void account_on( irc_t *irc, account_t *a );
void account_off( irc_t *irc, account_t *a );

char *set_eval_account( set_t *set, char *value );
char *set_eval_account_reconnect_delay( set_t *set, char *value );
int account_reconnect_delay( account_t *a );

#define ACC_SET_NOSAVE		0x01
#define ACC_SET_OFFLINE_ONLY	0x02
#define ACC_SET_ONLINE_ONLY	0x04

#endif
