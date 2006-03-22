  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2004 Wilmer van der Gaast and others                *
  \********************************************************************/

/* Stuff to handle, save and search buddies                             */

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

typedef struct __USER
{
	char *nick;
	char *user;
	char *host;
	char *realname;
	
	char *away;
	
	char is_private;
	char online;
	
	char *handle;
	char *group;
	struct gaim_connection *gc;

 	char *sendbuf;
 	time_t last_typing_notice;
 	int sendbuf_len;
 	guint sendbuf_timer;
    	int sendbuf_flags;
	
	void (*send_handler) ( irc_t *irc, struct __USER *u, char *msg, int flags );
	
	struct __USER *next;
} user_t;

user_t *user_add( struct irc *irc, char *nick );
int user_del( irc_t *irc, char *nick );
G_MODULE_EXPORT user_t *user_find( irc_t *irc, char *nick );
G_MODULE_EXPORT user_t *user_findhandle( struct gaim_connection *gc, char *handle );
void user_rename( irc_t *irc, char *oldnick, char *newnick );
