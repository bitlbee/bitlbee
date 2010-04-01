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

typedef struct bee
{
	struct set *set;
	
	GSList *users;
	struct account *accounts; /* TODO(wilmer): Use GSList here too? */
	
	const struct bee_ui_funcs *ui;
	void *ui_data;
} bee_t;

bee_t *bee_new();
void bee_free( bee_t *b );

typedef enum
{
	BEE_USER_ONLINE = 1,
	BEE_USER_AWAY = 2,
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
	gboolean (*user_status)( bee_t *bee, struct bee_user *bu, struct bee_user *old );
} bee_ui_funcs_t;


/* bee.c */
bee_t *bee_new();
void bee_free( bee_t *b );

/* bee_user.c */
bee_user_t *bee_user_new( bee_t *bee, struct im_connection *ic, const char *handle );
int bee_user_free( bee_t *bee, struct im_connection *ic, const char *handle );
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
G_MODULE_EXPORT void imcb_buddy_msg( struct im_connection *ic, const char *handle, char *msg, uint32_t flags, time_t sent_at );

#endif /* __BEE_H__ */
