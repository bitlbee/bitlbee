  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2004 Wilmer van der Gaast and others                *
  \********************************************************************/

/*
 * nogaim, soon to be known as im_api. Not a separate product (unless
 * someone would be interested in such a thing), just a new name.
 *
 * Gaim without gaim - for BitlBee
 *
 * This file contains functions called by the Gaim IM-modules. It contains
 * some struct and type definitions from Gaim.
 *
 * Copyright (C) 1998-1999, Mark Spencer <markster@marko.net>
 *                          (and possibly other members of the Gaim team)
 * Copyright 2002-2007 Wilmer van der Gaast <wilmer@gaast.net>
 */

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

#ifndef _NOGAIM_H
#define _NOGAIM_H

#include "bitlbee.h"
#include "account.h"
#include "proxy.h"
#include "md5.h"
#include "sha.h"

#define BUF_LEN MSG_LEN
#define BUF_LONG ( BUF_LEN * 2 )
#define MSG_LEN 2048
#define BUF_LEN MSG_LEN

#define BUDDY_ALIAS_MAXLEN 388   /* because MSN names can be 387 characters */

#define WEBSITE "http://www.bitlbee.org/"
#define GAIM_AWAY_CUSTOM "Custom"

/* Sharing flags between all kinds of things. I just hope I won't hit any
   limits before 32-bit machines become extinct. ;-) */
#define OPT_LOGGED_IN   0x00000001
#define OPT_LOGGING_OUT 0x00000002
#define OPT_AWAY        0x00000004
#define OPT_DOES_HTML   0x00000010
#define OPT_TYPING      0x00000100 /* Some pieces of code make assumptions */
#define OPT_THINKING    0x00000200 /* about these values... Stupid me! */

/* ok. now the fun begins. first we create a connection structure */
struct im_connection
{
	account_t *acc;
	u_int32_t flags;
	
	/* each connection then can have its own protocol-specific data */
	void *proto_data;
	
	/* all connections need an input watcher */
	int inpa;
	guint keepalive;
	
	/* buddy list stuff. there is still a global groups for the buddy list, but
	 * we need to maintain our own set of buddies, and our own permit/deny lists */
	GSList *permit;
	GSList *deny;
	int permdeny;
	
	char displayname[128];
	char *away;
	
	int evil;
	
	/* BitlBee */
	irc_t *irc;
	
	struct groupchat *groupchats;
};

struct groupchat {
	struct im_connection *ic;

	GList *in_room;
	GList *ignored;
	
	struct groupchat *next;
	char *channel;
	char *title;
	char joined;
	void *data;
};

struct buddy {
	char name[80];
	char show[BUDDY_ALIAS_MAXLEN];
	int present;
	int evil;
	time_t signon;
	time_t idle;
	int uc;
	guint caps; /* woohoo! */
	void *proto_data; /* what a hack */
	struct im_connection *ic; /* the connection it belongs to */
};

struct prpl {
	int options;
	const char *name;

	/* Added this one to be able to add per-account settings, don't think
	   it should be used for anything else. */
	void (* init)		(account_t *);
	/* These should all be pretty obvious. */
	void (* login)		(account_t *);
	void (* keepalive)	(struct im_connection *);
	void (* logout)		(struct im_connection *);
	
	int  (* buddy_msg)	(struct im_connection *, char *to, char *message, int flags);
	void (* set_away)	(struct im_connection *, char *state, char *message);
	void (* get_away)       (struct im_connection *, char *who);
	int  (* send_typing)	(struct im_connection *, char *who, int flags);
	
	/* For now BitlBee doesn't really handle groups, just set it to NULL. */
	void (* add_buddy)	(struct im_connection *, char *name, char *group);
	void (* remove_buddy)	(struct im_connection *, char *name, char *group);
	
	/* Block list stuff. */
	void (* add_permit)	(struct im_connection *, char *who);
	void (* add_deny)	(struct im_connection *, char *who);
	void (* rem_permit)	(struct im_connection *, char *who);
	void (* rem_deny)	(struct im_connection *, char *who);
	/* Doesn't actually have UI hooks. */
	void (* set_permit_deny)(struct im_connection *);
	
	/* Request profile info. Free-formatted stuff, the IM module gives back
	   this info via imcb_log(). */
	void (* get_info)	(struct im_connection *, char *who);
	void (* set_my_name)	(struct im_connection *, char *name);
	void (* set_name)	(struct im_connection *, char *who, char *name);
	
	/* Group chat stuff. */
	void (* chat_invite)	(struct groupchat *, char *who, char *message);
	void (* chat_leave)	(struct groupchat *);
	void (* chat_msg)	(struct groupchat *, char *message, int flags);
	struct groupchat *
	     (* chat_with)	(struct im_connection *, char *who);
	struct groupchat *
	     (* chat_join)	(struct im_connection *, char *room, char *nick, char *password);
	
	GList *(* away_states)(struct im_connection *ic);
	
	/* Mainly for AOL, since they think "Bung hole" == "Bu ngho le". *sigh* */
	int (* handle_cmp) (const char *who1, const char *who2);
};

/* im_api core stuff. */
void nogaim_init();
G_MODULE_EXPORT GSList *get_connections();
G_MODULE_EXPORT struct prpl *find_protocol( const char *name );
G_MODULE_EXPORT void register_protocol( struct prpl * );

/* Connection management. */
G_MODULE_EXPORT struct im_connection *imcb_new( account_t *acc );
G_MODULE_EXPORT void imcb_free( struct im_connection *ic );
G_MODULE_EXPORT void imcb_connected( struct im_connection *ic );
G_MODULE_EXPORT void imc_logout( struct im_connection *ic, int allow_reconnect );

/* Communicating with the user. */
G_MODULE_EXPORT void imcb_log( struct im_connection *ic, char *format, ... ) G_GNUC_PRINTF( 2, 3 );
G_MODULE_EXPORT void imcb_error( struct im_connection *ic, char *format, ... ) G_GNUC_PRINTF( 2, 3 );
G_MODULE_EXPORT void imcb_ask( struct im_connection *ic, char *msg, void *data, void *doit, void *dont );
G_MODULE_EXPORT void imcb_ask_add( struct im_connection *ic, char *handle, const char *realname );

/* Buddy management */
G_MODULE_EXPORT void imcb_add_buddy( struct im_connection *ic, char *handle, char *group );
G_MODULE_EXPORT void imcb_remove_buddy( struct im_connection *ic, char *handle, char *group );
G_MODULE_EXPORT struct buddy *imcb_find_buddy( struct im_connection *ic, char *handle );
G_MODULE_EXPORT void imcb_rename_buddy( struct im_connection *ic, char *handle, char *realname );

/* Buddy activity */
G_MODULE_EXPORT void imcb_buddy_status( struct im_connection *ic, const char *handle, int flags, const char *state, const char *message );
/* Not implemented yet! */ G_MODULE_EXPORT void imcb_buddy_times( struct im_connection *ic, const char *handle, time_t login, time_t idle );
G_MODULE_EXPORT void imcb_buddy_msg( struct im_connection *ic, char *handle, char *msg, u_int32_t flags, time_t sent_at );
G_MODULE_EXPORT void imcb_buddy_typing( struct im_connection *ic, char *handle, u_int32_t flags );

/* Groupchats */
G_MODULE_EXPORT void imcb_chat_invited( struct im_connection *ic, char *handle, char *who, char *msg, GList *data );
G_MODULE_EXPORT struct groupchat *imcb_chat_new( struct im_connection *ic, char *handle );
G_MODULE_EXPORT void imcb_chat_add_buddy( struct groupchat *b, char *handle );
G_MODULE_EXPORT void imcb_chat_remove_buddy( struct groupchat *b, char *handle, char *reason );
G_MODULE_EXPORT void imcb_chat_msg( struct groupchat *c, char *who, char *msg, u_int32_t flags, time_t sent_at );
G_MODULE_EXPORT void imcb_chat_free( struct groupchat *c );

/* Actions, or whatever. */
int imc_set_away( struct im_connection *ic, char *away );
int imc_buddy_msg( struct im_connection *ic, char *handle, char *msg, int flags );
int imc_chat_msg( struct groupchat *c, char *msg, int flags );

void imc_add_allow( struct im_connection *ic, char *handle );
void imc_rem_allow( struct im_connection *ic, char *handle );
void imc_add_block( struct im_connection *ic, char *handle );
void imc_rem_block( struct im_connection *ic, char *handle );

/* Misc. stuff */
char *set_eval_away_devoice( set_t *set, char *value );
gboolean auto_reconnect( gpointer data, gint fd, b_input_condition cond );
void cancel_auto_reconnect( struct account *a );

#endif
