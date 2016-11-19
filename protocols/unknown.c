/********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2016 Wilmer van der Gaast and others                *
  \********************************************************************/

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

#define BITLBEE_CORE
#include "nogaim.h"

/* Displays an error when trying to connect this account */
static void unknown_prpl_login(account_t *acc)
{
	struct im_connection *ic = imcb_new(acc);
	char *msg;

	imcb_error(ic, "Unknown protocol");

	msg = explain_unknown_protocol(acc->prpl->name);
	imcb_error(ic, msg);
	g_free(msg);

	imc_logout(ic, FALSE);
}

/* Required, no-op */
static void unknown_prpl_logout(struct im_connection *ic)
{
}

/* Needed to ensure the server setting is preserved */
static void unknown_prpl_init(account_t *acc)
{
	set_t *s;

	s = set_add(&acc->set, "server", NULL, set_eval_account, acc);
	s->flags |= SET_NOSAVE | ACC_SET_OFFLINE_ONLY | SET_NULL_OK;
}

/* Needed to silence warnings about named groupchats not supported */
struct groupchat *unknown_prpl_chat_join(struct im_connection *ic, const char *room, const char *nick, const char *password,
                                         set_t **sets)
{
	return NULL;
}

void unknown_prpl_initmodule(struct prpl **prpl)
{
	struct prpl *ret = g_new0(struct prpl, 1);

	ret->name = "unknown";
	ret->options = PRPL_OPT_UNKNOWN_PROTOCOL | PRPL_OPT_NOOTR;
	ret->login = unknown_prpl_login;
	ret->logout = unknown_prpl_logout;
	ret->init = unknown_prpl_init;
	ret->chat_join = unknown_prpl_chat_join;
	*prpl = ret;
}

