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
  if not, write to the Free Software Foundation, Inc., 51 Franklin St.,
  Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef __BEE_H__
#define __BEE_H__

struct bee_ui_funcs;
struct groupchat;

typedef struct bee {
	/* Settings. See set.h for how these work. The UI can add its
	   own settings here. */
	struct set *set;

	GSList *users;  /* struct bee_user */
	GSList *groups; /* struct bee_group */
	struct account *accounts; /* TODO(wilmer): Use GSList here too? */

	/* Symbolic, to refer to the local user (who has no real bee_user
	   object). Not to be used by anything except so far imcb_chat_add/
	   remove_buddy(). */
	struct bee_user *user;

	/* Fill in the callbacks for events you care about. */
	const struct bee_ui_funcs *ui;

	/* And this one will be passed to every callback for any state the
	   UI may want to keep. */
	void *ui_data;
} bee_t;

bee_t *bee_new();
void bee_free(bee_t *b);

/* TODO(wilmer): Kill at least the OPT_ flags that have an equivalent here. */
typedef enum {
	BEE_USER_ONLINE = 1,    /* Compatibility with old OPT_LOGGED_IN flag */
	BEE_USER_AWAY = 4,      /* Compatibility with old OPT_AWAY flag */
	BEE_USER_MOBILE = 8,    /* Compatibility with old OPT_MOBILE flag */
	BEE_USER_LOCAL = 256,   /* Locally-added contacts (not in real contact list) */
	BEE_USER_SPECIAL = 512, /* Denotes a user as being special */
} bee_user_flags_t;

typedef struct bee_user {
	struct im_connection *ic;
	char *handle;
	char *fullname;
	char *nick;
	struct bee_group *group;

	bee_user_flags_t flags;
	char *status;     /* NULL means available, anything else is an away state. */
	char *status_msg; /* Status and/or away message. */

	/* Set using imcb_buddy_times(). */
	time_t login_time, idle_time;

	bee_t *bee;
	void *ui_data;
	void *data; /* Can be used by the IM module. */
} bee_user_t;

/* This one's mostly used so save space and make it easier (cheaper) to
   compare groups of contacts, etc. */
typedef struct bee_group {
	char *key;  /* Lower case version of the name. */
	char *name;
} bee_group_t;

typedef struct bee_ui_funcs {
	void (*imc_connected)(struct im_connection *ic);
	void (*imc_disconnected)(struct im_connection *ic);

	gboolean (*user_new)(bee_t *bee, struct bee_user *bu);
	gboolean (*user_free)(bee_t *bee, struct bee_user *bu);
	/* Set the fullname first, then call this one to notify the UI. */
	gboolean (*user_fullname)(bee_t *bee, bee_user_t *bu);
	gboolean (*user_nick_hint)(bee_t *bee, bee_user_t *bu, const char *hint);
	/* Notify the UI when an existing user is moved between groups. */
	gboolean (*user_group)(bee_t *bee, bee_user_t *bu);
	/* State info is already updated, old is provided in case the UI needs a diff. */
	gboolean (*user_status)(bee_t *bee, struct bee_user *bu, struct bee_user *old);
	/* On every incoming message. sent_at = 0 means unknown. */
	gboolean (*user_msg)(bee_t *bee, bee_user_t *bu, const char *msg, time_t sent_at);
	/* Flags currently defined (OPT_TYPING/THINKING) in nogaim.h. */
	gboolean (*user_typing)(bee_t *bee, bee_user_t *bu, guint32 flags);
	/* CTCP-like stuff (buddy action) response */
	gboolean (*user_action_response)(bee_t *bee, bee_user_t *bu, const char *action, char * const args[],
	                                 void *data);

	/* Called at creation time. Don't show to the user until s/he is
	   added using chat_add_user().  UI state can be stored via c->data. */
	gboolean (*chat_new)(bee_t *bee, struct groupchat *c);
	gboolean (*chat_free)(bee_t *bee, struct groupchat *c);
	/* System messages of any kind. */
	gboolean (*chat_log)(bee_t *bee, struct groupchat *c, const char *text);
	gboolean (*chat_msg)(bee_t *bee, struct groupchat *c, bee_user_t *bu, const char *msg, time_t sent_at);
	gboolean (*chat_add_user)(bee_t *bee, struct groupchat *c, bee_user_t *bu);
	gboolean (*chat_remove_user)(bee_t *bee, struct groupchat *c, bee_user_t *bu);
	gboolean (*chat_topic)(bee_t *bee, struct groupchat *c, const char *new_topic, bee_user_t *bu);
	gboolean (*chat_name_hint)(bee_t *bee, struct groupchat *c, const char *name);
	gboolean (*chat_invite)(bee_t *bee, bee_user_t *bu, const char *name, const char *msg);

	struct file_transfer* (*ft_in_start)(bee_t * bee, bee_user_t * bu, const char *file_name, size_t file_size);
	gboolean (*ft_out_start)(struct im_connection *ic, struct file_transfer *ft);
	void (*ft_close)(struct im_connection *ic, struct file_transfer *ft);
	void (*ft_finished)(struct im_connection *ic, struct file_transfer *ft);
} bee_ui_funcs_t;


/* bee.c */
bee_t *bee_new();
void bee_free(bee_t *b);

/* bee_user.c */
bee_user_t *bee_user_new(bee_t *bee, struct im_connection *ic, const char *handle, bee_user_flags_t flags);
int bee_user_free(bee_t *bee, bee_user_t *bu);
bee_user_t *bee_user_by_handle(bee_t *bee, struct im_connection *ic, const char *handle);
int bee_user_msg(bee_t *bee, bee_user_t *bu, const char *msg, int flags);
bee_group_t *bee_group_by_name(bee_t *bee, const char *name, gboolean creat);
void bee_group_free(bee_t *bee);

/* Callbacks from IM modules to core: */
/* Buddy activity */
/* To manipulate the status of a handle.
 * - flags can be |='d with OPT_* constants. You will need at least:
 *   OPT_LOGGED_IN and OPT_AWAY.
 * - 'state' and 'message' can be NULL */
G_MODULE_EXPORT void imcb_buddy_status(struct im_connection *ic, const char *handle, int flags, const char *state,
                                       const char *message);
G_MODULE_EXPORT void imcb_buddy_status_msg(struct im_connection *ic, const char *handle, const char *message);
G_MODULE_EXPORT void imcb_buddy_times(struct im_connection *ic, const char *handle, time_t login, time_t idle);
/* Call when a handle says something. 'flags' and 'sent_at may be just 0. */
G_MODULE_EXPORT void imcb_buddy_msg(struct im_connection *ic, const char *handle, const char *msg, guint32 flags,
                                    time_t sent_at);
G_MODULE_EXPORT void imcb_notify_email(struct im_connection *ic, const char *handle, char *msg, guint32 flags,
                                       time_t sent_at);

/* bee_chat.c */
/* These two functions are to create a group chat.
 * - imcb_chat_new(): the 'handle' parameter identifies the chat, like the
 *   channel name on IRC.
 * - After you have a groupchat pointer, you should add the handles, finally
 *   the user her/himself. At that point the group chat will be visible to the
 *   user, too. */
G_MODULE_EXPORT struct groupchat *imcb_chat_new(struct im_connection *ic, const char *handle);
G_MODULE_EXPORT void imcb_chat_name_hint(struct groupchat *c, const char *name);
G_MODULE_EXPORT void imcb_chat_free(struct groupchat *c);
/* To tell BitlBee 'who' said 'msg' in 'c'. 'flags' and 'sent_at' can be 0. */
G_MODULE_EXPORT void imcb_chat_msg(struct groupchat *c, const char *who, char *msg, guint32 flags, time_t sent_at);
/* System messages specific to a groupchat, so they can be displayed in the right context. */
G_MODULE_EXPORT void imcb_chat_log(struct groupchat *c, char *format, ...);
/* To tell BitlBee 'who' changed the topic of 'c' to 'topic'. */
G_MODULE_EXPORT void imcb_chat_topic(struct groupchat *c, char *who, char *topic, time_t set_at);
G_MODULE_EXPORT void imcb_chat_add_buddy(struct groupchat *c, const char *handle);
/* To remove a handle from a group chat. Reason can be NULL. */
G_MODULE_EXPORT void imcb_chat_remove_buddy(struct groupchat *c, const char *handle, const char *reason);
G_MODULE_EXPORT int bee_chat_msg(bee_t *bee, struct groupchat *c, const char *msg, int flags);
G_MODULE_EXPORT struct groupchat *bee_chat_by_title(bee_t *bee, struct im_connection *ic, const char *title);
G_MODULE_EXPORT void imcb_chat_invite(struct im_connection *ic, const char *name, const char *who, const char *msg);

#endif /* __BEE_H__ */
