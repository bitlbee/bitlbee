/********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2010 Wilmer van der Gaast and others                *
  \********************************************************************/

/* Stuff to handle rooms                                                */

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
#include "bitlbee.h"

struct groupchat *imcb_chat_new(struct im_connection *ic, const char *handle)
{
	struct groupchat *c = g_new0(struct groupchat, 1);
	bee_t *bee = ic->bee;

	/* This one just creates the conversation structure, user won't see
	   anything yet until s/he is joined to the conversation. (This
	   allows you to add other already present participants first.) */

	ic->groupchats = g_slist_prepend(ic->groupchats, c);
	c->ic = ic;
	c->title = g_strdup(handle);
	c->topic = g_strdup_printf(
	        "BitlBee groupchat: \"%s\". Please keep in mind that root-commands won't work here. Have fun!",
	        c->title);

	if (set_getbool(&ic->bee->set, "debug")) {
		imcb_log(ic, "Creating new conversation: (id=%p,handle=%s)", c, handle);
	}

	if (bee->ui->chat_new) {
		bee->ui->chat_new(bee, c);
	}

	return c;
}

void imcb_chat_name_hint(struct groupchat *c, const char *name)
{
	bee_t *bee = c->ic->bee;

	if (bee->ui->chat_name_hint) {
		bee->ui->chat_name_hint(bee, c, name);
	}
}

void imcb_chat_free(struct groupchat *c)
{
	struct im_connection *ic = c->ic;
	bee_t *bee = ic->bee;
	GList *ir;

	if (bee->ui->chat_free) {
		bee->ui->chat_free(bee, c);
	}

	if (set_getbool(&ic->bee->set, "debug")) {
		imcb_log(ic, "You were removed from conversation %p", c);
	}

	ic->groupchats = g_slist_remove(ic->groupchats, c);

	for (ir = c->in_room; ir; ir = ir->next) {
		g_free(ir->data);
	}
	g_list_free(c->in_room);
	g_free(c->title);
	g_free(c->topic);
	g_free(c);
}

static gboolean handle_is_self(struct im_connection *ic, const char *handle)
{
	return (ic->acc->prpl->handle_is_self) ?
	       ic->acc->prpl->handle_is_self(ic, handle) :
	       (ic->acc->prpl->handle_cmp(ic->acc->user, handle) == 0);
}

void imcb_chat_msg(struct groupchat *c, const char *who, char *msg, guint32 flags, time_t sent_at)
{
	struct im_connection *ic = c->ic;
	bee_t *bee = ic->bee;
	bee_user_t *bu;
	gboolean temp = FALSE;
	char *s;

	if (handle_is_self(ic, who) && !(flags & OPT_SELFMESSAGE)) {
		return;
	}

	bu = bee_user_by_handle(bee, ic, who);
	temp = (bu == NULL);

	if (temp) {
		bu = bee_user_new(bee, ic, who, BEE_USER_ONLINE);
	}

	s = set_getstr(&ic->bee->set, "strip_html");
	if ((g_strcasecmp(s, "always") == 0) ||
	    ((ic->flags & OPT_DOES_HTML) && s)) {
		strip_html(msg);
	}

	if (bee->ui->chat_msg) {
		bee->ui->chat_msg(bee, c, bu, msg, flags, sent_at);
	}

	if (temp) {
		bee_user_free(bee, bu);
	}
}

void imcb_chat_log(struct groupchat *c, char *format, ...)
{
	struct im_connection *ic = c->ic;
	bee_t *bee = ic->bee;
	va_list params;
	char *text;

	if (!bee->ui->chat_log) {
		return;
	}

	va_start(params, format);
	text = g_strdup_vprintf(format, params);
	va_end(params);

	bee->ui->chat_log(bee, c, text);
	g_free(text);
}

void imcb_chat_topic(struct groupchat *c, char *who, char *topic, time_t set_at)
{
	struct im_connection *ic = c->ic;
	bee_t *bee = ic->bee;
	bee_user_t *bu;

	if (!bee->ui->chat_topic) {
		return;
	}

	if (who == NULL) {
		bu = NULL;
	} else if (handle_is_self(ic, who)) {
		bu = bee->user;
	} else {
		bu = bee_user_by_handle(bee, ic, who);
	}

	if ((g_strcasecmp(set_getstr(&ic->bee->set, "strip_html"), "always") == 0) ||
	    ((ic->flags & OPT_DOES_HTML) && set_getbool(&ic->bee->set, "strip_html"))) {
		strip_html(topic);
	}

	bee->ui->chat_topic(bee, c, topic, bu);
}

void imcb_chat_add_buddy(struct groupchat *c, const char *handle)
{
	struct im_connection *ic = c->ic;
	bee_t *bee = ic->bee;
	bee_user_t *bu = bee_user_by_handle(bee, ic, handle);
	gboolean me;

	if (set_getbool(&c->ic->bee->set, "debug")) {
		imcb_log(c->ic, "User %s added to conversation %p", handle, c);
	}

	me = handle_is_self(ic, handle);

	/* Most protocols allow people to join, even when they're not in
	   your contact list. Try to handle that here */
	if (!me && !bu) {
		bu = bee_user_new(bee, ic, handle, BEE_USER_LOCAL);
	}

	/* Add the handle to the room userlist */
	/* TODO: Use bu instead of a string */
	c->in_room = g_list_append(c->in_room, g_strdup(handle));

	if (bee->ui->chat_add_user) {
		bee->ui->chat_add_user(bee, c, me ? bee->user : bu);
	}

	if (me) {
		c->joined = 1;
	}
}

void imcb_chat_remove_buddy(struct groupchat *c, const char *handle, const char *reason)
{
	struct im_connection *ic = c->ic;
	bee_t *bee = ic->bee;
	bee_user_t *bu = NULL;

	if (set_getbool(&bee->set, "debug")) {
		imcb_log(ic, "User %s removed from conversation %p (%s)", handle, c, reason ? reason : "");
	}

	/* It might be yourself! */
	if (handle_is_self(ic, handle)) {
		if (c->joined == 0) {
			return;
		}

		bu = bee->user;
		c->joined = 0;
	} else {
		bu = bee_user_by_handle(bee, ic, handle);
	}

	if (bee->ui->chat_remove_user && bu) {
		bee->ui->chat_remove_user(bee, c, bu, reason);
	}
}

int bee_chat_msg(bee_t *bee, struct groupchat *c, const char *msg, int flags)
{
	struct im_connection *ic = c->ic;
	char *buf = NULL;

	if ((ic->flags & OPT_DOES_HTML) && (g_strncasecmp(msg, "<html>", 6) != 0)) {
		buf = escape_html(msg);
		msg = buf;
	} else {
		buf = g_strdup(msg);
	}

	ic->acc->prpl->chat_msg(c, buf, flags);
	g_free(buf);

	return 1;
}

struct groupchat *bee_chat_by_title(bee_t *bee, struct im_connection *ic, const char *title)
{
	struct groupchat *c;
	GSList *l;

	for (l = ic->groupchats; l; l = l->next) {
		c = l->data;
		if (strcmp(c->title, title) == 0) {
			return c;
		}
	}

	return NULL;
}

void imcb_chat_invite(struct im_connection *ic, const char *name, const char *who, const char *msg)
{
	bee_user_t *bu = bee_user_by_handle(ic->bee, ic, who);

	if (bu && ic->bee->ui->chat_invite) {
		ic->bee->ui->chat_invite(ic->bee, bu, name, msg);
	}
}

void bee_chat_list_finish(struct im_connection *ic)
{
	cmd_chat_list_finish(ic);
}
