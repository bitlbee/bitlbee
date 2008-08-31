  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2008 Wilmer van der Gaast and others                *
  \********************************************************************/

/* Keep track of chatrooms the user is interested in                    */

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

struct chat
{
	account_t *acc;
	
	char *handle;
	char *channel;
	set_t *set;
	
	struct chat *next;
};

struct chat *chat_add( irc_t *irc, account_t *acc, char *handle, char *channel );
struct chat *chat_byhandle( irc_t *irc, account_t *acc, char *handle );
struct chat *chat_bychannel( irc_t *irc, char *channel );
struct chat *chat_get( irc_t *irc, char *id );
int chat_del( irc_t *irc, struct chat *chat );

int chat_chancmp( char *a, char *b );
int chat_chanok( char *a );
