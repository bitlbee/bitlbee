  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2010 Wilmer van der Gaast and others                *
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

#ifndef __BEE_H__
#define __BEE_H__

struct bee_ui_funcs;
struct groupchat;

typedef struct bee
{
	struct set *set;
	
	GSList *users;
	struct account *accounts; /* TODO(wilmer): Use GSList here too? */
	
	/* Symbolic, to refer to the local user (who has no real bee_user
	   object). Not to be used by anything except so far imcb_chat_add/
	   remove_buddy(). This seems slightly cleaner than abusing NULL. */
	struct bee_user *user;
	
	const struct bee_ui_funcs *ui;
	void *ui_data;
} bee_t;

bee_t *bee_new();
void bee_free( bee_t *b );

typedef enum
{
	BEE_USER_ONLINE = 1,    /* Compatibility with old OPT_LOGGED_IN flag */
	BEE_USER_AWAY = 4,      /* Compatibility with old OPT_AWAY flag */
} bee_user_flags_t;

typedef struct bee_user
{
	struct im_connection *ic;
	char *handle;
	char *fullname;
	char *group;

	bee_user_flags_t flags;
	char *status;
	char *status_msg;
	
	bee_t *bee;
	void *ui_data;
} bee_user_t;

typedef struct bee_ui_funcs
{
	gboolean (*user_new)( bee_t *bee, struct bee_user *bu );
	gboolean (*user_free)( bee_t *bee, struct bee_user *bu );
	gboolean (*user_fullname)( bee_t *bee, bee_user_t *bu );
	gboolean (*user_status)( bee_t *bee, struct bee_user *bu, struct bee_user *old );
	gboolean (*user_msg)( bee_t *bee, bee_user_t *bu, const char *msg, time_t sent_at );
	gboolean (*user_typing)( bee_t *bee, bee_user_t *bu, guint32 flags );
	
	gboolean (*chat_new)( bee_t *bee, struct groupchat *c );
	gboolean (*chat_free)( bee_t *bee, struct groupchat *c );
	gboolean (*chat_log)( bee_t *bee, struct groupchat *c, const char *text );
	gboolean (*chat_msg)( bee_t *bee, struct groupchat *c, bee_user_t *bu, const char *msg, time_t sent_at );
	gboolean (*chat_add_user)( bee_t *bee, struct groupchat *c, bee_user_t *bu );
	gboolean (*chat_remove_user)( bee_t *bee, struct groupchat *c, bee_user_t *bu );
	gboolean (*chat_topic)( bee_t *bee, struct groupchat *c, const char *new, bee_user_t *bu );
	gboolean (*chat_name_hint)( bee_t *bee, struct groupchat *c, const char *name );
	
	struct file_transfer* (*ft_in_start)( bee_t *bee, bee_user_t *bu, const char *file_name, size_t file_size );
	gboolean (*ft_out_start)( struct im_connection *ic, struct file_transfer *ft );
	void (*ft_close)( struct im_connection *ic, struct file_transfer *ft );
	void (*ft_finished)( struct im_connection *ic, struct file_transfer *ft );
} bee_ui_funcs_t;


/* bee.c */
bee_t *bee_new();
void bee_free( bee_t *b );

/* bee_user.c */
bee_user_t *bee_user_new( bee_t *bee, struct im_connection *ic, const char *handle );
int bee_user_free( bee_t *bee, bee_user_t *bu );
bee_user_t *bee_user_by_handle( bee_t *bee, struct im_connection *ic, const char *handle );
int bee_user_msg( bee_t *bee, bee_user_t *bu, const char *msg, int flags );

/* Callbacks from IM modules to core: */
/* Buddy activity */
/* To manipulate the status of a handle.
 * - flags can be |='d with OPT_* constants. You will need at least:
 *   OPT_LOGGED_IN and OPT_AWAY.
 * - 'state' and 'message' can be NULL */
G_MODULE_EXPORT void imcb_buddy_status( struct im_connection *ic, const char *handle, int flags, const char *state, const char *message );
/* Not implemented yet! */ G_MODULE_EXPORT void imcb_buddy_times( struct im_connection *ic, const char *handle, time_t login, time_t idle );
/* Call when a handle says something. 'flags' and 'sent_at may be just 0. */
G_MODULE_EXPORT void imcb_buddy_msg( struct im_connection *ic, const char *handle, char *msg, guint32 flags, time_t sent_at );

/* bee_chat.c */
#if 0
struct groupchat *imcb_chat_new( struct im_connection *ic, const char *handle );
void imcb_chat_name_hint( struct groupchat *c, const char *name );
void imcb_chat_free( struct groupchat *c );
void imcb_chat_msg( struct groupchat *c, const char *who, char *msg, uint32_t flags, time_t sent_at );
void imcb_chat_log( struct groupchat *c, char *format, ... );
void imcb_chat_topic( struct groupchat *c, char *who, char *topic, time_t set_at );
void imcb_chat_add_buddy( struct groupchat *b, const char *handle );
void imcb_chat_remove_buddy( struct groupchat *b, const char *handle, const char *reason );
static int remove_chat_buddy_silent( struct groupchat *b, const char *handle );
#endif
int bee_chat_msg( bee_t *bee, struct groupchat *c, const char *msg, int flags );

#endif /* __BEE_H__ */
