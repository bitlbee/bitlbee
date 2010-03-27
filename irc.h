  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2004 Wilmer van der Gaast and others                *
  \********************************************************************/

/* The IRC-based UI (for now the only one)                              */

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

#define UMODES "abisw"
#define UMODES_PRIV "Ro"
#define CMODES "nt"
#define CMODE "t"
#define UMODE "s"
#define CTYPES "&#"

typedef enum
{
	USTATUS_OFFLINE = 0,
	USTATUS_AUTHORIZED = 1,
	USTATUS_LOGGED_IN = 2,
	USTATUS_IDENTIFIED = 4,
	USTATUS_SHUTDOWN = 8
} irc_status_t;

struct irc_user;

typedef struct irc
{
	int fd;
	irc_status_t status;
	double last_pong;
	int pinging;
	char *sendbuffer;
	char *readbuffer;
	GIConv iconv, oconv;

	struct irc_user *root;
	struct irc_user *user;

	char *password; /* HACK: Used to save the user's password, but before
	                   logging in, this may contain a password we should
	                   send to identify after USER/NICK are received. */

	char umode[8];
	
	struct query *queries;
	struct account *accounts;
	GSList *file_transfers;
	
	GSList *users, *channels;
	GHashTable *nick_user_hash;
	GHashTable *watches;

	gint r_watch_source_id;
	gint w_watch_source_id;
	gint ping_source_id;
	
	struct bee *b;
} irc_t;

typedef struct irc_user
{
	char *nick;
	char *user;
	char *host;
	char *fullname;
	
	/* Nickname in lowercase for case sensitive searches */
	char *key;
	
	char is_private;
	
	char *sendbuf;
	int sendbuf_len;
	guint sendbuf_timer;
	int sendbuf_flags;
	
	//struct user *b;
} irc_user_t;

typedef enum
{
	IRC_CHANNEL_JOINED = 1,
} irc_channel_flags_t;

typedef struct irc_channel
{
	irc_t *irc;
	int flags;
	char *name;
	char *topic;
	char mode[8];
	GSList *users;
	struct set *set;
} irc_channel_t;

#include "user.h"

/* irc.c */
extern GSList *irc_connection_list;

irc_t *irc_new( int fd );
void irc_abort( irc_t *irc, int immed, char *format, ... ) G_GNUC_PRINTF( 3, 4 );
void irc_free( irc_t *irc );

void irc_process( irc_t *irc );
char **irc_parse_line( char *line );
char *irc_build_line( char **cmd );

void irc_write( irc_t *irc, char *format, ... ) G_GNUC_PRINTF( 2, 3 );
void irc_write_all( int now, char *format, ... ) G_GNUC_PRINTF( 2, 3 );
void irc_vawrite( irc_t *irc, char *format, va_list params );

int irc_check_login( irc_t *irc );

/* irc_channel.c */
irc_channel_t *irc_channel_new( irc_t *irc, const char *name );
int irc_channel_add_user( irc_channel_t *ic, irc_user_t *iu );
int irc_channel_del_user( irc_channel_t *ic, irc_user_t *iu );
int irc_channel_set_topic( irc_channel_t *ic, const char *topic );

/* irc_commands.c */
void irc_exec( irc_t *irc, char **cmd );

/* irc_send.c */
void irc_send_num( irc_t *irc, int code, char *format, ... ) G_GNUC_PRINTF( 3, 4 );
void irc_send_login( irc_t *irc );
void irc_send_motd( irc_t *irc );
void irc_usermsg( irc_t *irc, char *format, ... );
void irc_send_join( irc_channel_t *ic, irc_user_t *iu );
void irc_send_part( irc_channel_t *ic, irc_user_t *iu, const char *reason );
void irc_send_names( irc_channel_t *ic );
void irc_send_topic( irc_channel_t *ic );

/* irc_user.c */
irc_user_t *irc_user_new( irc_t *irc, const char *nick );
int irc_user_free( irc_t *irc, const char *nick );
irc_user_t *irc_user_find( irc_t *irc, const char *nick );
int irc_user_rename( irc_t *irc, const char *old, const char *new );
gint irc_user_cmp( gconstpointer a_, gconstpointer b_ );

#endif
