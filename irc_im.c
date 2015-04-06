/********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2012 Wilmer van der Gaast and others                *
  \********************************************************************/

/* Some glue to put the IRC and the IM stuff together.                  */

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

#include "bitlbee.h"
#include "dcc.h"

/* IM->IRC callbacks: Simple IM/buddy-related stuff. */

static const struct irc_user_funcs irc_user_im_funcs;

static void bee_irc_imc_connected(struct im_connection *ic)
{
	irc_t *irc = (irc_t *) ic->bee->ui_data;

	irc_channel_auto_joins(irc, ic->acc);
}

static void bee_irc_imc_disconnected(struct im_connection *ic)
{
	/* Maybe try to send /QUITs here instead of later on. */
}

static gboolean bee_irc_user_new(bee_t *bee, bee_user_t *bu)
{
	irc_user_t *iu;
	irc_t *irc = (irc_t *) bee->ui_data;
	char nick[MAX_NICK_LENGTH + 1], *s;

	memset(nick, 0, MAX_NICK_LENGTH + 1);
	strcpy(nick, nick_get(bu));

	bu->ui_data = iu = irc_user_new(irc, nick);
	iu->bu = bu;

	if (set_getbool(&irc->b->set, "private")) {
		iu->last_channel = NULL;
	} else {
		iu->last_channel = irc_channel_with_user(irc, iu);
	}

	if ((s = strchr(bu->handle, '@'))) {
		iu->host = g_strdup(s + 1);
		iu->user = g_strndup(bu->handle, s - bu->handle);
	} else {
		iu->user = g_strdup(bu->handle);
		if (bu->ic->acc->server) {
			iu->host = g_strdup(bu->ic->acc->server);
		} else {
			char *s;
			for (s = bu->ic->acc->tag; g_ascii_isalnum(*s); s++) {
				;
			}
			/* Only use the tag if we got to the end of the string.
			   (So allow alphanumerics only. Hopefully not too
			   restrictive.) */
			if (*s) {
				iu->host = g_strdup(bu->ic->acc->prpl->name);
			} else {
				iu->host = g_strdup(bu->ic->acc->tag);
			}
		}
	}

	while ((s = strchr(iu->user, ' '))) {
		*s = '_';
	}

	if (bu->flags & BEE_USER_LOCAL) {
		char *s = set_getstr(&bee->set, "handle_unknown");

		if (strcmp(s, "add_private") == 0) {
			iu->last_channel = NULL;
		} else if (strcmp(s, "add_channel") == 0) {
			iu->last_channel = irc->default_channel;
		}
	}

	iu->f = &irc_user_im_funcs;

	return TRUE;
}

static gboolean bee_irc_user_free(bee_t *bee, bee_user_t *bu)
{
	return irc_user_free(bee->ui_data, (irc_user_t *) bu->ui_data);
}

static gboolean bee_irc_user_status(bee_t *bee, bee_user_t *bu, bee_user_t *old)
{
	irc_t *irc = bee->ui_data;
	irc_user_t *iu = bu->ui_data;

	/* Do this outside the if below since away state can change without
	   the online state changing. */
	iu->flags &= ~IRC_USER_AWAY;
	if (bu->flags & BEE_USER_AWAY || !(bu->flags & BEE_USER_ONLINE)) {
		iu->flags |= IRC_USER_AWAY;
	}

	if ((bu->flags & BEE_USER_ONLINE) != (old->flags & BEE_USER_ONLINE)) {
		if (bu->flags & BEE_USER_ONLINE) {
			if (g_hash_table_lookup(irc->watches, iu->key)) {
				irc_send_num(irc, 600, "%s %s %s %d :%s", iu->nick, iu->user,
				             iu->host, (int) time(NULL), "logged online");
			}
		} else {
			if (g_hash_table_lookup(irc->watches, iu->key)) {
				irc_send_num(irc, 601, "%s %s %s %d :%s", iu->nick, iu->user,
				             iu->host, (int) time(NULL), "logged offline");
			}

			/* Send a QUIT since those will also show up in any
			   query windows the user may have, plus it's only
			   one QUIT instead of possibly many (in case of
			   multiple control chans). If there's a channel that
			   shows offline people, a JOIN will follow. */
			if (set_getbool(&bee->set, "offline_user_quits")) {
				irc_user_quit(iu, "Leaving...");
			}
		}
	}

	/* Reset this one since the info may have changed. */
	iu->away_reply_timeout = 0;

	bee_irc_channel_update(irc, NULL, iu);

	return TRUE;
}

void bee_irc_channel_update(irc_t *irc, irc_channel_t *ic, irc_user_t *iu)
{
	GSList *l;

	if (ic == NULL) {
		for (l = irc->channels; l; l = l->next) {
			ic = l->data;
			/* TODO: Just add a type flag or so.. */
			if (ic->f == irc->default_channel->f &&
			    (ic->flags & IRC_CHANNEL_JOINED)) {
				bee_irc_channel_update(irc, ic, iu);
			}
		}
		return;
	}
	if (iu == NULL) {
		for (l = irc->users; l; l = l->next) {
			iu = l->data;
			if (iu->bu) {
				bee_irc_channel_update(irc, ic, l->data);
			}
		}
		return;
	}

	if (!irc_channel_wants_user(ic, iu)) {
		irc_channel_del_user(ic, iu, IRC_CDU_PART, NULL);
	} else {
		struct irc_control_channel *icc = ic->data;
		int mode = 0;

		if (!(iu->bu->flags & BEE_USER_ONLINE)) {
			mode = icc->modes[0];
		} else if (iu->bu->flags & BEE_USER_AWAY) {
			mode = icc->modes[1];
		} else if (iu->bu->flags & BEE_USER_SPECIAL) {
			mode = icc->modes[2];
		} else {
			mode = icc->modes[3];
		}

		if (!mode) {
			irc_channel_del_user(ic, iu, IRC_CDU_PART, NULL);
		} else {
			irc_channel_add_user(ic, iu);
			irc_channel_user_set_mode(ic, iu, mode);
		}
	}
}

static gboolean bee_irc_user_msg(bee_t *bee, bee_user_t *bu, const char *msg_, time_t sent_at)
{
	irc_t *irc = bee->ui_data;
	irc_user_t *iu = (irc_user_t *) bu->ui_data;
	const char *dst;
	char *prefix = NULL;
	char *wrapped, *ts = NULL;
	char *msg = g_strdup(msg_);
	GSList *l;

	if (sent_at > 0 && set_getbool(&irc->b->set, "display_timestamps")) {
		ts = irc_format_timestamp(irc, sent_at);
	}

	dst = irc_user_msgdest(iu);
	if (dst != irc->user->nick) {
		/* if not messaging directly, call user by name */
		prefix = g_strdup_printf("%s%s%s", irc->user->nick, set_getstr(&bee->set, "to_char"), ts ? : "");
	} else {
		prefix = ts;
		ts = NULL;      /* don't double-free */
	}

	for (l = irc_plugins; l; l = l->next) {
		irc_plugin_t *p = l->data;
		if (p->filter_msg_in) {
			char *s = p->filter_msg_in(iu, msg, 0);
			if (s) {
				if (s != msg) {
					g_free(msg);
				}
				msg = s;
			} else {
				/* Modules can swallow messages. */
				goto cleanup;
			}
		}
	}

	if ((g_strcasecmp(set_getstr(&bee->set, "strip_html"), "always") == 0) ||
	    ((bu->ic->flags & OPT_DOES_HTML) && set_getbool(&bee->set, "strip_html"))) {
		char *s = g_strdup(msg);
		strip_html(s);
		g_free(msg);
		msg = s;
	}

	wrapped = word_wrap(msg, 425);
	irc_send_msg(iu, "PRIVMSG", dst, wrapped, prefix);
	g_free(wrapped);

cleanup:
	g_free(prefix);
	g_free(msg);
	g_free(ts);

	return TRUE;
}

static gboolean bee_irc_user_typing(bee_t *bee, bee_user_t *bu, uint32_t flags)
{
	irc_t *irc = (irc_t *) bee->ui_data;

	if (set_getbool(&bee->set, "typing_notice")) {
		irc_send_msg_f((irc_user_t *) bu->ui_data, "PRIVMSG", irc->user->nick,
		               "\001TYPING %d\001", (flags >> 8) & 3);
	} else {
		return FALSE;
	}

	return TRUE;
}

static gboolean bee_irc_user_action_response(bee_t *bee, bee_user_t *bu, const char *action, char * const args[],
                                             void *data)
{
	irc_t *irc = (irc_t *) bee->ui_data;
	GString *msg = g_string_new("\001");

	g_string_append(msg, action);
	while (*args) {
		if (strchr(*args, ' ')) {
			g_string_append_printf(msg, " \"%s\"", *args);
		} else {
			g_string_append_printf(msg, " %s", *args);
		}
		args++;
	}
	g_string_append_c(msg, '\001');

	irc_send_msg((irc_user_t *) bu->ui_data, "NOTICE", irc->user->nick, msg->str, NULL);

	g_string_free(msg, TRUE);

	return TRUE;
}

static gboolean bee_irc_user_nick_update(irc_user_t *iu);

static gboolean bee_irc_user_fullname(bee_t *bee, bee_user_t *bu)
{
	irc_user_t *iu = (irc_user_t *) bu->ui_data;
	char *s;

	if (iu->fullname != iu->nick) {
		g_free(iu->fullname);
	}
	iu->fullname = g_strdup(bu->fullname);

	/* Strip newlines (unlikely, but IRC-unfriendly so they must go)
	   TODO(wilmer): Do the same with away msgs again! */
	for (s = iu->fullname; *s; s++) {
		if (g_ascii_isspace(*s)) {
			*s = ' ';
		}
	}

	if ((bu->ic->flags & OPT_LOGGED_IN) && set_getbool(&bee->set, "display_namechanges")) {
		/* People don't like this /NOTICE. Meh, let's go back to the old one.
		char *msg = g_strdup_printf( "<< \002BitlBee\002 - Changed name to `%s' >>", iu->fullname );
		irc_send_msg( iu, "NOTICE", irc->user->nick, msg, NULL );
		*/
		imcb_log(bu->ic, "User `%s' changed name to `%s'", iu->nick, iu->fullname);
	}

	bee_irc_user_nick_update(iu);

	return TRUE;
}

static gboolean bee_irc_user_nick_hint(bee_t *bee, bee_user_t *bu, const char *hint)
{
	bee_irc_user_nick_update((irc_user_t *) bu->ui_data);

	return TRUE;
}

static gboolean bee_irc_user_group(bee_t *bee, bee_user_t *bu)
{
	irc_user_t *iu = (irc_user_t *) bu->ui_data;
	irc_t *irc = (irc_t *) bee->ui_data;
	bee_user_flags_t online;

	/* Take the user offline temporarily so we can change the nick (if necessary). */
	if ((online = bu->flags & BEE_USER_ONLINE)) {
		bu->flags &= ~BEE_USER_ONLINE;
	}

	bee_irc_channel_update(irc, NULL, iu);
	bee_irc_user_nick_update(iu);

	if (online) {
		bu->flags |= online;
		bee_irc_channel_update(irc, NULL, iu);
	}

	return TRUE;
}

static gboolean bee_irc_user_nick_update(irc_user_t *iu)
{
	bee_user_t *bu = iu->bu;
	char *newnick;

	if (bu->flags & BEE_USER_ONLINE) {
		/* Ignore if the user is visible already. */
		return TRUE;
	}

	if (nick_saved(bu)) {
		/* The user already assigned a nickname to this person. */
		return TRUE;
	}

	newnick = nick_get(bu);

	if (strcmp(iu->nick, newnick) != 0) {
		nick_dedupe(bu, newnick);
		irc_user_set_nick(iu, newnick);
	}

	return TRUE;
}

void bee_irc_user_nick_reset(irc_user_t *iu)
{
	bee_user_t *bu = iu->bu;
	bee_user_flags_t online;

	if (bu == FALSE) {
		return;
	}

	/* In this case, pretend the user is offline. */
	if ((online = bu->flags & BEE_USER_ONLINE)) {
		bu->flags &= ~BEE_USER_ONLINE;
	}

	nick_del(bu);
	bee_irc_user_nick_update(iu);

	bu->flags |= online;
}

/* IRC->IM calls */

static gboolean bee_irc_user_privmsg_cb(gpointer data, gint fd, b_input_condition cond);

static gboolean bee_irc_user_privmsg(irc_user_t *iu, const char *msg)
{
	const char *away;

	if (iu->bu == NULL) {
		return FALSE;
	}

	if ((away = irc_user_get_away(iu)) &&
	    time(NULL) >= iu->away_reply_timeout) {
		irc_send_num(iu->irc, 301, "%s :%s", iu->nick, away);
		iu->away_reply_timeout = time(NULL) +
		                         set_getint(&iu->irc->b->set, "away_reply_timeout");
	}

	if (iu->pastebuf == NULL) {
		iu->pastebuf = g_string_new(msg);
	} else {
		b_event_remove(iu->pastebuf_timer);
		g_string_append_printf(iu->pastebuf, "\n%s", msg);
	}

	if (set_getbool(&iu->irc->b->set, "paste_buffer")) {
		int delay;

		if ((delay = set_getint(&iu->irc->b->set, "paste_buffer_delay")) <= 5) {
			delay *= 1000;
		}

		iu->pastebuf_timer = b_timeout_add(delay, bee_irc_user_privmsg_cb, iu);

		return TRUE;
	} else {
		bee_irc_user_privmsg_cb(iu, 0, 0);

		return TRUE;
	}
}

static gboolean bee_irc_user_privmsg_cb(gpointer data, gint fd, b_input_condition cond)
{
	irc_user_t *iu = data;
	char *msg;
	GSList *l;

	msg = g_string_free(iu->pastebuf, FALSE);
	iu->pastebuf = NULL;
	iu->pastebuf_timer = 0;

	for (l = irc_plugins; l; l = l->next) {
		irc_plugin_t *p = l->data;
		if (p->filter_msg_out) {
			char *s = p->filter_msg_out(iu, msg, 0);
			if (s) {
				if (s != msg) {
					g_free(msg);
				}
				msg = s;
			} else {
				/* Modules can swallow messages. */
				iu->pastebuf = NULL;
				g_free(msg);
				return FALSE;
			}
		}
	}

	bee_user_msg(iu->irc->b, iu->bu, msg, 0);

	g_free(msg);

	return FALSE;
}

static gboolean bee_irc_user_ctcp(irc_user_t *iu, char *const *ctcp)
{
	if (ctcp[1] && g_strcasecmp(ctcp[0], "DCC") == 0
	    && g_strcasecmp(ctcp[1], "SEND") == 0) {
		if (iu->bu && iu->bu->ic && iu->bu->ic->acc->prpl->transfer_request) {
			file_transfer_t *ft = dcc_request(iu->bu->ic, ctcp);
			if (ft) {
				iu->bu->ic->acc->prpl->transfer_request(iu->bu->ic, ft, iu->bu->handle);
			}

			return TRUE;
		}
	} else if (g_strcasecmp(ctcp[0], "TYPING") == 0) {
		if (iu->bu && iu->bu->ic && iu->bu->ic->acc->prpl->send_typing && ctcp[1]) {
			int st = ctcp[1][0];
			if (st >= '0' && st <= '2') {
				st <<= 8;
				iu->bu->ic->acc->prpl->send_typing(iu->bu->ic, iu->bu->handle, st);
			}

			return TRUE;
		}
	} else if (g_strcasecmp(ctcp[0], "HELP") == 0 && iu->bu) {
		GString *supp = g_string_new("Supported CTCPs:");
		GList *l;

		if (iu->bu->ic && iu->bu->ic->acc->prpl->transfer_request) {
			g_string_append(supp, " DCC SEND,");
		}
		if (iu->bu->ic && iu->bu->ic->acc->prpl->send_typing) {
			g_string_append(supp, " TYPING,");
		}
		if (iu->bu->ic->acc->prpl->buddy_action_list) {
			for (l = iu->bu->ic->acc->prpl->buddy_action_list(iu->bu); l; l = l->next) {
				struct buddy_action *ba = l->data;
				g_string_append_printf(supp, " %s (%s),",
				                       ba->name, ba->description);
			}
		}
		g_string_truncate(supp, supp->len - 1);
		irc_send_msg_f(iu, "NOTICE", iu->irc->user->nick, "\001HELP %s\001", supp->str);
		g_string_free(supp, TRUE);
	} else if (iu->bu && iu->bu->ic && iu->bu->ic->acc->prpl->buddy_action) {
		iu->bu->ic->acc->prpl->buddy_action(iu->bu, ctcp[0], ctcp + 1, NULL);
	}

	return FALSE;
}

static const struct irc_user_funcs irc_user_im_funcs = {
	bee_irc_user_privmsg,
	bee_irc_user_ctcp,
};


/* IM->IRC: Groupchats */
const struct irc_channel_funcs irc_channel_im_chat_funcs;

static gboolean bee_irc_chat_new(bee_t *bee, struct groupchat *c)
{
	irc_t *irc = bee->ui_data;
	irc_channel_t *ic;
	char *topic;
	GSList *l;
	int i;

	/* Try to find a channel that expects to receive a groupchat.
	   This flag is set earlier in our current call trace. */
	for (l = irc->channels; l; l = l->next) {
		ic = l->data;
		if (ic->flags & IRC_CHANNEL_CHAT_PICKME) {
			break;
		}
	}

	/* If we found none, just generate some stupid name. */
	if (l == NULL) {
		for (i = 0; i <= 999; i++) {
			char name[16];
			sprintf(name, "#chat_%03d", i);
			if ((ic = irc_channel_new(irc, name))) {
				break;
			}
		}
	}

	if (ic == NULL) {
		return FALSE;
	}

	c->ui_data = ic;
	ic->data = c;

	topic = g_strdup_printf(
	        "BitlBee groupchat: \"%s\". Please keep in mind that root-commands won't work here. Have fun!",
	        c->title);
	irc_channel_set_topic(ic, topic, irc->root);
	g_free(topic);

	return TRUE;
}

static gboolean bee_irc_chat_free(bee_t *bee, struct groupchat *c)
{
	irc_channel_t *ic = c->ui_data;

	if (ic == NULL) {
		return FALSE;
	}

	if (ic->flags & IRC_CHANNEL_JOINED) {
		irc_channel_printf(ic, "Cleaning up channel, bye!");
	}

	ic->data = NULL;
	c->ui_data = NULL;
	irc_channel_del_user(ic, ic->irc->user, IRC_CDU_KICK, "Chatroom closed by server");

	return TRUE;
}

static gboolean bee_irc_chat_log(bee_t *bee, struct groupchat *c, const char *text)
{
	irc_channel_t *ic = c->ui_data;

	if (ic == NULL) {
		return FALSE;
	}

	irc_channel_printf(ic, "%s", text);

	return TRUE;
}

static gboolean bee_irc_chat_msg(bee_t *bee, struct groupchat *c, bee_user_t *bu, const char *msg, time_t sent_at)
{
	irc_t *irc = bee->ui_data;
	irc_user_t *iu = bu->ui_data;
	irc_channel_t *ic = c->ui_data;
	char *wrapped, *ts = NULL;

	if (ic == NULL) {
		return FALSE;
	}

	if (sent_at > 0 && set_getbool(&bee->set, "display_timestamps")) {
		ts = irc_format_timestamp(irc, sent_at);
	}

	wrapped = word_wrap(msg, 425);
	irc_send_msg(iu, "PRIVMSG", ic->name, wrapped, ts);
	g_free(ts);
	g_free(wrapped);

	return TRUE;
}

static gboolean bee_irc_chat_add_user(bee_t *bee, struct groupchat *c, bee_user_t *bu)
{
	irc_t *irc = bee->ui_data;
	irc_channel_t *ic = c->ui_data;

	if (ic == NULL) {
		return FALSE;
	}

	irc_channel_add_user(ic, bu == bee->user ? irc->user : bu->ui_data);

	return TRUE;
}

static gboolean bee_irc_chat_remove_user(bee_t *bee, struct groupchat *c, bee_user_t *bu)
{
	irc_t *irc = bee->ui_data;
	irc_channel_t *ic = c->ui_data;

	if (ic == NULL || bu == NULL) {
		return FALSE;
	}

	/* TODO: Possible bug here: If a module removes $user here instead of just
	   using imcb_chat_free() and the channel was IRC_CHANNEL_TEMP, we get into
	   a broken state around here. */
	irc_channel_del_user(ic, bu == bee->user ? irc->user : bu->ui_data, IRC_CDU_PART, NULL);

	return TRUE;
}

static gboolean bee_irc_chat_topic(bee_t *bee, struct groupchat *c, const char *new, bee_user_t *bu)
{
	irc_channel_t *ic = c->ui_data;
	irc_t *irc = bee->ui_data;
	irc_user_t *iu;

	if (ic == NULL) {
		return FALSE;
	}

	if (bu == NULL) {
		iu = irc->root;
	} else if (bu == bee->user) {
		iu = irc->user;
	} else {
		iu = bu->ui_data;
	}

	irc_channel_set_topic(ic, new, iu);

	return TRUE;
}

static gboolean bee_irc_chat_name_hint(bee_t *bee, struct groupchat *c, const char *name)
{
	return irc_channel_name_hint(c->ui_data, name);
}

static gboolean bee_irc_chat_invite(bee_t *bee, bee_user_t *bu, const char *name, const char *msg)
{
	char *channel, *s;
	irc_t *irc = bee->ui_data;
	irc_user_t *iu = bu->ui_data;
	irc_channel_t *chan;

	if (strchr(CTYPES, name[0])) {
		channel = g_strdup(name);
	} else {
		channel = g_strdup_printf("#%s", name);
	}

	if ((s = strchr(channel, '@'))) {
		*s = '\0';
	}

	if (strlen(channel) > MAX_NICK_LENGTH) {
		/* If the channel name is very long (like those insane GTalk
		   UUID names), try if we can use the inviter's nick. */
		s = g_strdup_printf("#%s", iu->nick);
		if (irc_channel_by_name(irc, s) == NULL) {
			g_free(channel);
			channel = s;
		}
	}

	if ((chan = irc_channel_new(irc, channel)) &&
	    set_setstr(&chan->set, "type", "chat") &&
	    set_setstr(&chan->set, "chat_type", "room") &&
	    set_setstr(&chan->set, "account", bu->ic->acc->tag) &&
	    set_setstr(&chan->set, "room", (char *) name)) {
		/* I'm assuming that if the user didn't "chat add" the room
		   himself but got invited, it's temporary, so make this a
		   temporary mapping that is removed as soon as we /PART. */
		chan->flags |= IRC_CHANNEL_TEMP;
	} else {
		irc_channel_free(chan);
		chan = NULL;
	}
	g_free(channel);

	irc_send_msg_f(iu, "PRIVMSG", irc->user->nick, "<< \002BitlBee\002 - Invitation to chatroom %s >>", name);
	if (msg) {
		irc_send_msg(iu, "PRIVMSG", irc->user->nick, msg, NULL);
	}
	if (chan) {
		irc_send_msg_f(iu, "PRIVMSG", irc->user->nick, "To join the room, just /join %s", chan->name);
		irc_send_invite(iu, chan);
	}

	return TRUE;
}

/* IRC->IM */
static gboolean bee_irc_channel_chat_privmsg_cb(gpointer data, gint fd, b_input_condition cond);

static gboolean bee_irc_channel_chat_privmsg(irc_channel_t *ic, const char *msg)
{
	struct groupchat *c = ic->data;
	char *trans = NULL, *s;

	if (c == NULL) {
		return FALSE;
	}

	if (set_getbool(&ic->set, "translate_to_nicks")) {
		char nick[MAX_NICK_LENGTH + 1];
		irc_user_t *iu;

		strncpy(nick, msg, MAX_NICK_LENGTH);
		nick[MAX_NICK_LENGTH] = '\0';
		if ((s = strchr(nick, ':')) || (s = strchr(nick, ','))) {
			*s = '\0';
			if ((iu = irc_user_by_name(ic->irc, nick)) && iu->bu &&
			    iu->bu->nick && irc_channel_has_user(ic, iu)) {
				trans = g_strconcat(iu->bu->nick, msg + (s - nick), NULL);
				msg = trans;
			}
		}
	}

	if (set_getbool(&ic->irc->b->set, "paste_buffer")) {
		int delay;

		if (ic->pastebuf == NULL) {
			ic->pastebuf = g_string_new(msg);
		} else {
			b_event_remove(ic->pastebuf_timer);
			g_string_append_printf(ic->pastebuf, "\n%s", msg);
		}

		if ((delay = set_getint(&ic->irc->b->set, "paste_buffer_delay")) <= 5) {
			delay *= 1000;
		}

		ic->pastebuf_timer = b_timeout_add(delay, bee_irc_channel_chat_privmsg_cb, ic);

		g_free(trans);
		return TRUE;
	} else {
		bee_chat_msg(ic->irc->b, c, msg, 0);
	}

	g_free(trans);
	return TRUE;
}

static gboolean bee_irc_channel_chat_privmsg_cb(gpointer data, gint fd, b_input_condition cond)
{
	irc_channel_t *ic = data;

	if (ic->data) {
		bee_chat_msg(ic->irc->b, ic->data, ic->pastebuf->str, 0);
	}

	g_string_free(ic->pastebuf, TRUE);
	ic->pastebuf = 0;
	ic->pastebuf_timer = 0;

	return FALSE;
}

static gboolean bee_irc_channel_chat_join(irc_channel_t *ic)
{
	char *acc_s, *room;
	account_t *acc;

	if (strcmp(set_getstr(&ic->set, "chat_type"), "room") != 0) {
		return TRUE;
	}

	if ((acc_s = set_getstr(&ic->set, "account")) &&
	    (room = set_getstr(&ic->set, "room")) &&
	    (acc = account_get(ic->irc->b, acc_s)) &&
	    acc->ic && acc->prpl->chat_join) {
		char *nick;

		if (!(nick = set_getstr(&ic->set, "nick"))) {
			nick = ic->irc->user->nick;
		}

		ic->flags |= IRC_CHANNEL_CHAT_PICKME;
		acc->prpl->chat_join(acc->ic, room, nick, NULL, &ic->set);
		ic->flags &= ~IRC_CHANNEL_CHAT_PICKME;

		return FALSE;
	} else {
		irc_send_num(ic->irc, 403, "%s :Can't join channel, account offline?", ic->name);
		return FALSE;
	}
}

static gboolean bee_irc_channel_chat_part(irc_channel_t *ic, const char *msg)
{
	struct groupchat *c = ic->data;

	if (c && c->ic->acc->prpl->chat_leave) {
		c->ic->acc->prpl->chat_leave(c);
	}

	/* Remove the reference. We don't need it anymore. */
	ic->data = NULL;

	return TRUE;
}

static gboolean bee_irc_channel_chat_topic(irc_channel_t *ic, const char *new)
{
	struct groupchat *c = ic->data;

	if (c == NULL) {
		return FALSE;
	}

	if (c->ic->acc->prpl->chat_topic == NULL) {
		irc_send_num(ic->irc, 482, "%s :IM network does not support channel topics", ic->name);
	} else {
		/* TODO: Need more const goodness here, sigh */
		char *topic = g_strdup(new);
		c->ic->acc->prpl->chat_topic(c, topic);
		g_free(topic);
	}

	/* Whatever happened, the IM module should ack the topic change. */
	return FALSE;
}

static gboolean bee_irc_channel_chat_invite(irc_channel_t *ic, irc_user_t *iu)
{
	struct groupchat *c = ic->data;
	bee_user_t *bu = iu->bu;

	if (bu == NULL) {
		return FALSE;
	}

	if (c) {
		if (iu->bu->ic != c->ic) {
			irc_send_num(ic->irc, 482, "%s :Can't mix different IM networks in one groupchat", ic->name);
		} else if (c->ic->acc->prpl->chat_invite) {
			c->ic->acc->prpl->chat_invite(c, iu->bu->handle, NULL);
		} else {
			irc_send_num(ic->irc, 482, "%s :IM protocol does not support room invitations", ic->name);
		}
	} else if (bu->ic->acc->prpl->chat_with &&
	           strcmp(set_getstr(&ic->set, "chat_type"), "groupchat") == 0) {
		ic->flags |= IRC_CHANNEL_CHAT_PICKME;
		iu->bu->ic->acc->prpl->chat_with(bu->ic, bu->handle);
		ic->flags &= ~IRC_CHANNEL_CHAT_PICKME;
	} else {
		irc_send_num(ic->irc, 482, "%s :IM protocol does not support room invitations", ic->name);
	}

	return TRUE;
}

static void bee_irc_channel_chat_kick(irc_channel_t *ic, irc_user_t *iu, const char *msg)
{
	struct groupchat *c = ic->data;
	bee_user_t *bu = iu->bu;

	if ((c == NULL) || (bu == NULL)) {
		return;
	}

	if (!c->ic->acc->prpl->chat_kick) {
		irc_send_num(ic->irc, 482, "%s :IM protocol does not support room kicking", ic->name);
		return;
	}

	c->ic->acc->prpl->chat_kick(c, iu->bu->handle, msg);
}

static char *set_eval_room_account(set_t *set, char *value);
static char *set_eval_chat_type(set_t *set, char *value);

static gboolean bee_irc_channel_init(irc_channel_t *ic)
{
	set_t *s;

	set_add(&ic->set, "account", NULL, set_eval_room_account, ic);
	set_add(&ic->set, "chat_type", "groupchat", set_eval_chat_type, ic);

	s = set_add(&ic->set, "nick", NULL, NULL, ic);
	s->flags |= SET_NULL_OK;

	set_add(&ic->set, "room", NULL, NULL, ic);
	set_add(&ic->set, "translate_to_nicks", "true", set_eval_bool, ic);

	/* chat_type == groupchat */
	ic->flags |= IRC_CHANNEL_TEMP;

	return TRUE;
}

static char *set_eval_room_account(set_t *set, char *value)
{
	struct irc_channel *ic = set->data;
	account_t *acc, *oa;

	if (!(acc = account_get(ic->irc->b, value))) {
		return SET_INVALID;
	} else if (!acc->prpl->chat_join) {
		irc_rootmsg(ic->irc, "Named chatrooms not supported on that account.");
		return SET_INVALID;
	}

	if (set->value && (oa = account_get(ic->irc->b, set->value)) &&
	    oa->prpl->chat_free_settings) {
		oa->prpl->chat_free_settings(oa, &ic->set);
	}

	if (acc->prpl->chat_add_settings) {
		acc->prpl->chat_add_settings(acc, &ic->set);
	}

	return g_strdup(acc->tag);
}

static char *set_eval_chat_type(set_t *set, char *value)
{
	struct irc_channel *ic = set->data;

	if (strcmp(value, "groupchat") == 0) {
		ic->flags |= IRC_CHANNEL_TEMP;
	} else if (strcmp(value, "room") == 0) {
		ic->flags &= ~IRC_CHANNEL_TEMP;
	} else {
		return NULL;
	}

	return value;
}

static gboolean bee_irc_channel_free(irc_channel_t *ic)
{
	struct groupchat *c = ic->data;

	set_del(&ic->set, "account");
	set_del(&ic->set, "chat_type");
	set_del(&ic->set, "nick");
	set_del(&ic->set, "room");
	set_del(&ic->set, "translate_to_nicks");

	ic->flags &= ~IRC_CHANNEL_TEMP;

	/* That one still points at this channel. Don't. */
	if (c) {
		c->ui_data = NULL;
	}

	return TRUE;
}

const struct irc_channel_funcs irc_channel_im_chat_funcs = {
	bee_irc_channel_chat_privmsg,
	bee_irc_channel_chat_join,
	bee_irc_channel_chat_part,
	bee_irc_channel_chat_topic,
	bee_irc_channel_chat_invite,
	bee_irc_channel_chat_kick,

	bee_irc_channel_init,
	bee_irc_channel_free,
};


/* IM->IRC: File transfers */
static file_transfer_t *bee_irc_ft_in_start(bee_t *bee, bee_user_t *bu, const char *file_name, size_t file_size)
{
	return dccs_send_start(bu->ic, (irc_user_t *) bu->ui_data, file_name, file_size);
}

static gboolean bee_irc_ft_out_start(struct im_connection *ic, file_transfer_t *ft)
{
	return dccs_recv_start(ft);
}

static void bee_irc_ft_close(struct im_connection *ic, file_transfer_t *ft)
{
	return dcc_close(ft);
}

static void bee_irc_ft_finished(struct im_connection *ic, file_transfer_t *file)
{
	dcc_file_transfer_t *df = file->priv;

	if (file->bytes_transferred >= file->file_size) {
		dcc_finish(file);
	} else {
		df->proto_finished = TRUE;
	}
}

const struct bee_ui_funcs irc_ui_funcs = {
	bee_irc_imc_connected,
	bee_irc_imc_disconnected,

	bee_irc_user_new,
	bee_irc_user_free,
	bee_irc_user_fullname,
	bee_irc_user_nick_hint,
	bee_irc_user_group,
	bee_irc_user_status,
	bee_irc_user_msg,
	bee_irc_user_typing,
	bee_irc_user_action_response,

	bee_irc_chat_new,
	bee_irc_chat_free,
	bee_irc_chat_log,
	bee_irc_chat_msg,
	bee_irc_chat_add_user,
	bee_irc_chat_remove_user,
	bee_irc_chat_topic,
	bee_irc_chat_name_hint,
	bee_irc_chat_invite,

	bee_irc_ft_in_start,
	bee_irc_ft_out_start,
	bee_irc_ft_close,
	bee_irc_ft_finished,
};
