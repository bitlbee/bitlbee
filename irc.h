  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2004 Wilmer van der Gaast and others                *
  \********************************************************************/

/* The big hairy IRCd part of the project                               */

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

#ifndef _IRC_H
#define _IRC_H

#define IRC_MAX_LINE 512
#define IRC_MAX_ARGS 8

#define IRC_LOGIN_TIMEOUT 60
#define IRC_PING_STRING "PinglBee"

#define UMODES "iasw"
#define UMODES_PRIV "Ro"
#define CMODES "nt"
#define CMODE "t"
#define UMODE "s"

typedef enum
{
	USTATUS_OFFLINE,
	USTATUS_AUTHORIZED,
	USTATUS_LOGGED_IN,
	USTATUS_IDENTIFIED,
	USTATUS_SHUTDOWN
} irc_status_t;

typedef struct channel
{
	char *name;
} channel_t;

typedef struct irc
{
	int fd;
	irc_status_t status;
	double last_pong;
	int pinging;
	char *sendbuffer;
	char *readbuffer;
	int quit;

	int sentbytes;
	time_t oldtime;

	char *nick;
	char *user;
	char *host;
	char *realname;
	char *password;

	char umode[8];
	
	char *myhost;
	char *mynick;

	char *channel;
	int c_id;

	char is_private;		/* Not too nice... */
	char *last_target;
	
	struct query *queries;
	struct account *accounts;
	
	struct __USER *users;
	GHashTable *userhash;
	GHashTable *watches;
	struct __NICK *nicks;
	struct help *help;
	struct set *set;

	GIOChannel *io_channel;
	gint r_watch_source_id;
	gint w_watch_source_id;
	gint ping_source_id;
} irc_t;

#include "user.h"
#include "nick.h"

extern GSList *irc_connection_list;

irc_t *irc_new( int fd );
void irc_abort( irc_t *irc );
void irc_free( irc_t *irc );

int irc_exec( irc_t *irc, char **cmd );
int irc_process( irc_t *irc );
char **irc_parse_line( char *line );
char *irc_build_line( char **cmd );

void irc_vawrite( irc_t *irc, char *format, va_list params );
void irc_write( irc_t *irc, char *format, ... );
void irc_write_all( int now, char *format, ... );
void irc_reply( irc_t *irc, int code, char *format, ... );
G_MODULE_EXPORT int irc_usermsg( irc_t *irc, char *format, ... );
char **irc_tokenize( char *buffer );

void irc_login( irc_t *irc );
int irc_check_login( irc_t *irc );
void irc_motd( irc_t *irc );
void irc_names( irc_t *irc, char *channel );
void irc_topic( irc_t *irc, char *channel );
void irc_umode_set( irc_t *irc, char *s, int allow_priv );
void irc_who( irc_t *irc, char *channel );
void irc_spawn( irc_t *irc, user_t *u );
void irc_join( irc_t *irc, user_t *u, char *channel );
void irc_part( irc_t *irc, user_t *u, char *channel );
void irc_kick( irc_t *irc, user_t *u, char *channel, user_t *kicker );
void irc_kill( irc_t *irc, user_t *u );
void irc_invite( irc_t *irc, char *nick, char *channel );
void irc_whois( irc_t *irc, char *nick );
int irc_away( irc_t *irc, char *away );
void irc_setpass( irc_t *irc, const char *pass ); /* USE WITH CAUTION! */

int irc_send( irc_t *irc, char *nick, char *s, int flags );
int irc_privmsg( irc_t *irc, user_t *u, char *type, char *to, char *prefix, char *msg );
int irc_msgfrom( irc_t *irc, char *nick, char *msg );
int irc_noticefrom( irc_t *irc, char *nick, char *msg );

int buddy_send_handler( irc_t *irc, user_t *u, char *msg, int flags );

#endif
