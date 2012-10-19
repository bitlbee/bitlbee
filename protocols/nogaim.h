  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2010 Wilmer van der Gaast and others                *
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

#if(__sun)
#include <inttypes.h>
#else
#include <stdint.h>
#endif

#include "bitlbee.h"
#include "account.h"
#include "proxy.h"
#include "query.h"
#include "md5.h"
#include "ft.h"

#define BUDDY_ALIAS_MAXLEN 388   /* because MSN names can be 387 characters */

#define WEBSITE "http://www.bitlbee.org/"

/* Sharing flags between all kinds of things. I just hope I won't hit any
   limits before 32-bit machines become extinct. ;-) */
#define OPT_LOGGED_IN   0x00000001
#define OPT_LOGGING_OUT 0x00000002
#define OPT_AWAY        0x00000004
#define OPT_MOBILE      0x00000008
#define OPT_DOES_HTML   0x00000010
#define OPT_LOCALBUDDY  0x00000020 /* For nicks local to one groupchat */
#define OPT_SLOW_LOGIN  0x00000040 /* I.e. Twitter Oauth @ login time */
#define OPT_TYPING      0x00000100 /* Some pieces of code make assumptions */
#define OPT_THINKING    0x00000200 /* about these values... Stupid me! */
#define OPT_NOOTR       0x00001000 /* protocol not suitable for OTR */

/* ok. now the fun begins. first we create a connection structure */
struct im_connection
{
	account_t *acc;
	uint32_t flags;
	
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
	bee_t *bee;
	
	GSList *groupchats;
};

struct groupchat {
	struct im_connection *ic;

	/* stuff used just for chat */
	/* The in_room variable is a list of handles (not nicks!), kind of
	 * "nick list". This is how you can check who is in the group chat
	 * already, for example to avoid adding somebody two times. */
	GList *in_room;
	//GList *ignored;
	
	//struct groupchat *next;
	/* The title variable contains the ID you gave when you created the
	 * chat using imcb_chat_new(). */
	char *title;
	/* Use imcb_chat_topic() to change this variable otherwise the user
	 * won't notice the topic change. */
	char *topic;
	char joined;
	/* This is for you, you can add your own structure here to extend this
	 * structure for your protocol's needs. */
	void *data;
	void *ui_data;
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

struct buddy_action {
	char *name;
	char *description;
};

struct prpl {
	int options;
	/* You should set this to the name of your protocol.
	 * - The user sees this name ie. when imcb_log() is used. */
	const char *name;
	void *data;
	/* Maximum Message Size of this protocol.
	 * - Introduced for OTR, in order to fragment large protocol messages.
	 * - 0 means "unlimited". */
	unsigned int mms;

	/* Added this one to be able to add per-account settings, don't think
	 * it should be used for anything else. You are supposed to use the
	 * set_add() function to add new settings. */
	void (* init)		(account_t *);
	/* The typical usage of the login() function:
	 * - Create an im_connection using imcb_new() from the account_t parameter.
	 * - Initialize your myproto_data struct - you should store all your protocol-specific data there.
	 * - Save your custom structure to im_connection->proto_data.
	 * - Use proxy_connect() to connect to the server.
	 */
	void (* login)		(account_t *);
	/* Implementing this function is optional. */
	void (* keepalive)	(struct im_connection *);
	/* In this function you should:
	 * - Tell the server about you are logging out.
	 * - g_free() your myproto_data struct as BitlBee does not know how to
	 *   properly do so.
	 */
	void (* logout)		(struct im_connection *);
	
	/* This function is called when the user wants to send a message to a handle.
	 * - 'to' is a handle, not a nick
	 * - 'flags' may be ignored
	 */
	int  (* buddy_msg)	(struct im_connection *, char *to, char *message, int flags);
	/* This function is called then the user uses the /away IRC command.
	 * - 'state' contains the away reason.
	 * - 'message' may be ignored if your protocol does not support it.
	 */
	void (* set_away)	(struct im_connection *, char *state, char *message);
	/* Implementing this function is optional. */
	void (* get_away)       (struct im_connection *, char *who);
	/* Implementing this function is optional. */
	int  (* send_typing)	(struct im_connection *, char *who, int flags);
	
	/* 'name' is a handle to add/remove. For now BitlBee doesn't really
	 * handle groups, just set it to NULL, so you can ignore that
	 * parameter. */
	void (* add_buddy)	(struct im_connection *, char *name, char *group);
	void (* remove_buddy)	(struct im_connection *, char *name, char *group);
	
	/* Block list stuff. Implementing these are optional. */
	void (* add_permit)	(struct im_connection *, char *who);
	void (* add_deny)	(struct im_connection *, char *who);
	void (* rem_permit)	(struct im_connection *, char *who);
	void (* rem_deny)	(struct im_connection *, char *who);
	/* Doesn't actually have UI hooks. Not used at all, can be removed. */
	void (* set_permit_deny)(struct im_connection *);
	
	/* Request profile info. Free-formatted stuff, the IM module gives back
	   this info via imcb_log(). Implementing these are optional. */
	void (* get_info)	(struct im_connection *, char *who);
	/* set_my_name is *DEPRECATED*, not used by the UI anymore. Use the
	   display_name setting instead. */
	void (* set_my_name)	(struct im_connection *, char *name);
	void (* set_name)	(struct im_connection *, char *who, char *name);
	
	/* Group chat stuff. */
	/* This is called when the user uses the /invite IRC command.
	 * - 'who' may be ignored
	 * - 'message' is a handle to invite
	 */
	void (* chat_invite)	(struct groupchat *, char *who, char *message);
	/* This is called when the user uses the /part IRC command in a group
	 * chat. You just should tell the user about it, nothing more. */
	void (* chat_leave)	(struct groupchat *);
	/* This is called when the user sends a message to the groupchat.
	 * 'flags' may be ignored. */
	void (* chat_msg)	(struct groupchat *, char *message, int flags);
	/* This is called when the user uses the /join #nick IRC command.
	 * - 'who' is the handle of the nick
	 */
	struct groupchat *
	     (* chat_with)	(struct im_connection *, char *who);
	/* This is used when the user uses the /join #channel IRC command.  If
	 * your protocol does not support publicly named group chats, then do
	 * not implement this. */
	struct groupchat *
	     (* chat_join)	(struct im_connection *, const char *room,
	                         const char *nick, const char *password, set_t **sets);
	/* Change the topic, if supported. Note that BitlBee expects the IM
	   server to confirm the topic change with a regular topic change
	   event. If it doesn't do that, you have to fake it to make it
	   visible to the user. */
	void (* chat_topic)	(struct groupchat *, char *topic);
	
	/* If your protocol module needs any special info for joining chatrooms
	   other than a roomname + nickname, add them here. */
	void (* chat_add_settings)	(account_t *acc, set_t **head);
	void (* chat_free_settings)	(account_t *acc, set_t **head);
	
	/* You can tell what away states your protocol supports, so that
	 * BitlBee will try to map the IRC away reasons to them. If your
	 * protocol doesn't have any, just return one generic "Away". */
	GList *(* away_states)(struct im_connection *ic);
	
	/* Mainly for AOL, since they think "Bung hole" == "Bu ngho le". *sigh*
	 * - Most protocols will just want to set this to g_strcasecmp().*/
	int (* handle_cmp) (const char *who1, const char *who2);

	/* Implement these callbacks if you want to use imcb_ask_auth() */
	void (* auth_allow)	(struct im_connection *, const char *who);
	void (* auth_deny)	(struct im_connection *, const char *who);

	/* Incoming transfer request */
	void (* transfer_request) (struct im_connection *, file_transfer_t *ft, char *handle );
	
	void (* buddy_data_add) (struct bee_user *bu);
	void (* buddy_data_free) (struct bee_user *bu);
	
	GList *(* buddy_action_list) (struct bee_user *bu);
	void *(* buddy_action) (struct bee_user *bu, const char *action, char * const args[], void *data);
	
	/* Some placeholders so eventually older plugins may cooperate with newer BitlBees. */
	void *resv1;
	void *resv2;
	void *resv3;
	void *resv4;
	void *resv5;
};

/* im_api core stuff. */
void nogaim_init();
G_MODULE_EXPORT GSList *get_connections();
G_MODULE_EXPORT struct prpl *find_protocol( const char *name );
/* When registering a new protocol, you should allocate space for a new prpl
 * struct, initialize it (set the function pointers to point to your
 * functions), finally call this function. */
G_MODULE_EXPORT void register_protocol( struct prpl * );

/* Connection management. */
/* You will need this function in prpl->login() to get an im_connection from
 * the account_t parameter. */
G_MODULE_EXPORT struct im_connection *imcb_new( account_t *acc );
G_MODULE_EXPORT void imc_free( struct im_connection *ic );
/* Once you're connected, you should call this function, so that the user will
 * see the success. */
G_MODULE_EXPORT void imcb_connected( struct im_connection *ic );
/* This can be used to disconnect when something went wrong (ie. read error
 * from the server). You probably want to set the second parameter to TRUE. */
G_MODULE_EXPORT void imc_logout( struct im_connection *ic, int allow_reconnect );

/* Communicating with the user. */
/* A printf()-like function to tell the user anything you want. */
G_MODULE_EXPORT void imcb_log( struct im_connection *ic, char *format, ... ) G_GNUC_PRINTF( 2, 3 );
/* To tell the user an error, ie. before logging out when an error occurs. */
G_MODULE_EXPORT void imcb_error( struct im_connection *ic, char *format, ... ) G_GNUC_PRINTF( 2, 3 );

/* To ask a your about something.
 * - 'msg' is the question.
 * - 'data' can be your custom struct - it will be passed to the callbacks.
 * - 'doit' or 'dont' will be called depending of the answer of the user.
 */
G_MODULE_EXPORT void imcb_ask( struct im_connection *ic, char *msg, void *data, query_callback doit, query_callback dont );
G_MODULE_EXPORT void imcb_ask_with_free( struct im_connection *ic, char *msg, void *data, query_callback doit, query_callback dont, query_callback myfree );

/* Two common questions you may want to ask:
 * - X added you to his contact list, allow?
 * - X is not in your contact list, want to add?
 */
G_MODULE_EXPORT void imcb_ask_auth( struct im_connection *ic, const char *handle, const char *realname );
G_MODULE_EXPORT void imcb_ask_add( struct im_connection *ic, const char *handle, const char *realname );

/* Buddy management */
/* This function should be called for each handle which are visible to the
 * user, usually after a login, or if the user added a buddy and the IM
 * server confirms that the add was successful. Don't forget to do this! */
G_MODULE_EXPORT void imcb_add_buddy( struct im_connection *ic, const char *handle, const char *group );
G_MODULE_EXPORT void imcb_remove_buddy( struct im_connection *ic, const char *handle, char *group );
G_MODULE_EXPORT struct buddy *imcb_find_buddy( struct im_connection *ic, char *handle );
G_MODULE_EXPORT void imcb_rename_buddy( struct im_connection *ic, const char *handle, const char *realname );
G_MODULE_EXPORT void imcb_buddy_nick_hint( struct im_connection *ic, const char *handle, const char *nick );
G_MODULE_EXPORT void imcb_buddy_action_response( bee_user_t *bu, const char *action, char * const args[], void *data );

G_MODULE_EXPORT void imcb_buddy_typing( struct im_connection *ic, const char *handle, uint32_t flags );
G_MODULE_EXPORT struct bee_user *imcb_buddy_by_handle( struct im_connection *ic, const char *handle );
G_MODULE_EXPORT void imcb_clean_handle( struct im_connection *ic, char *handle );

/* Actions, or whatever. */
int imc_away_send_update( struct im_connection *ic );
int imc_chat_msg( struct groupchat *c, char *msg, int flags );

void imc_add_allow( struct im_connection *ic, char *handle );
void imc_rem_allow( struct im_connection *ic, char *handle );
void imc_add_block( struct im_connection *ic, char *handle );
void imc_rem_block( struct im_connection *ic, char *handle );

/* Misc. stuff */
char *set_eval_timezone( set_t *set, char *value );
gboolean auto_reconnect( gpointer data, gint fd, b_input_condition cond );
void cancel_auto_reconnect( struct account *a );

#endif
