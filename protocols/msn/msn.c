/********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2013 Wilmer van der Gaast and others                *
  \********************************************************************/

/* MSN module - Main file; functions to be called from BitlBee          */

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

#include "nogaim.h"
#include "soap.h"
#include "msn.h"

int msn_chat_id;
GSList *msn_connections;

static char *set_eval_display_name(set_t *set, char *value);

static void msn_init(account_t *acc)
{
	set_t *s;

	s = set_add(&acc->set, "display_name", NULL, set_eval_display_name, acc);
	s->flags |= SET_NOSAVE | ACC_SET_ONLINE_ONLY;

	s = set_add(&acc->set, "server", NULL, set_eval_account, acc);
	s->flags |= SET_NOSAVE | ACC_SET_OFFLINE_ONLY;

	s = set_add(&acc->set, "port", MSN_NS_PORT, set_eval_int, acc);
	s->flags |= ACC_SET_OFFLINE_ONLY;

	set_add(&acc->set, "mail_notifications", "false", set_eval_bool, acc);

	acc->flags |= ACC_FLAG_AWAY_MESSAGE | ACC_FLAG_STATUS_MESSAGE |
	              ACC_FLAG_HANDLE_DOMAINS;
}

static void msn_login(account_t *acc)
{
	struct im_connection *ic = imcb_new(acc);
	struct msn_data *md = g_new0(struct msn_data, 1);
	char *server = set_getstr(&ic->acc->set, "server");

	ic->proto_data = md;
	ic->flags |= OPT_PONGS | OPT_PONGED;

	if (!server) {
		server = "geo.gateway.messenger.live.com";
	}

	if (strchr(acc->user, '@') == NULL) {
		imcb_error(ic, "Invalid account name");
		imc_logout(ic, FALSE);
		return;
	}

	md->ic = ic;
	md->away_state = msn_away_state_list;
	md->domaintree = g_tree_new(msn_domaintree_cmp);
	md->fd = -1;
	md->is_http = TRUE;

	msn_connections = g_slist_prepend(msn_connections, ic);

	imcb_log(ic, "Connecting");
	msn_ns_connect(ic, server,
	               set_getint(&ic->acc->set, "port"));
}

static void msn_logout(struct im_connection *ic)
{
	struct msn_data *md = ic->proto_data;
	GSList *l;
	int i;

	if (md) {
		msn_ns_close(md);

		msn_soapq_flush(ic, FALSE);

		for (i = 0; i < sizeof(md->tokens) / sizeof(md->tokens[0]); i++) {
			g_free(md->tokens[i]);
		}
		g_free(md->lock_key);
		g_free(md->pp_policy);
		g_free(md->uuid);

		while (md->groups) {
			struct msn_group *mg = md->groups->data;
			md->groups = g_slist_remove(md->groups, mg);
			g_free(mg->id);
			g_free(mg->name);
			g_free(mg);
		}

		g_free(md->profile_rid);

		if (md->domaintree) {
			g_tree_destroy(md->domaintree);
		}
		md->domaintree = NULL;

		while (md->grpq) {
			struct msn_groupadd *ga = md->grpq->data;
			md->grpq = g_slist_remove(md->grpq, ga);
			g_free(ga->group);
			g_free(ga->who);
			g_free(ga);
		}

		g_free(md);
	}

	for (l = ic->permit; l; l = l->next) {
		g_free(l->data);
	}
	g_slist_free(ic->permit);

	for (l = ic->deny; l; l = l->next) {
		g_free(l->data);
	}
	g_slist_free(ic->deny);

	msn_connections = g_slist_remove(msn_connections, ic);
}

static int msn_buddy_msg(struct im_connection *ic, char *who, char *message, int away)
{
	struct bee_user *bu = bee_user_by_handle(ic->bee, ic, who);
	msn_ns_send_message(ic, bu, message);
	return 0;
}

static GList *msn_away_states(struct im_connection *ic)
{
	static GList *l = NULL;
	int i;

	if (l == NULL) {
		for (i = 0; *msn_away_state_list[i].code; i++) {
			if (*msn_away_state_list[i].name) {
				l = g_list_append(l, (void *) msn_away_state_list[i].name);
			}
		}
	}

	return l;
}

static void msn_set_away(struct im_connection *ic, char *state, char *message)
{
	struct msn_data *md = ic->proto_data;
	char *nick, *psm, *idle, *statecode, *body, *buf;

	if (state == NULL) {
		md->away_state = msn_away_state_list;
	} else if ((md->away_state = msn_away_state_by_name(state)) == NULL) {
		md->away_state = msn_away_state_list + 1;
	}

	statecode = (char *) md->away_state->code;
	nick = set_getstr(&ic->acc->set, "display_name");
	psm = message ? message : "";
	idle = (strcmp(statecode, "IDL") == 0) ? "false" : "true";

	body = g_markup_printf_escaped(MSN_PUT_USER_BODY,
		nick, psm, psm, md->uuid, statecode, md->uuid, idle, statecode,
		MSN_CAP1, MSN_CAP2, MSN_CAP1, MSN_CAP2
	);

	buf = g_strdup_printf(MSN_PUT_HEADERS, ic->acc->user, ic->acc->user, md->uuid,
		"/user", "application/user+xml",
		strlen(body), body);
	msn_ns_write(ic, -1, "PUT %d %zd\r\n%s", ++md->trId, strlen(buf), buf);

	g_free(buf);
	g_free(body);
}

static void msn_get_info(struct im_connection *ic, char *who)
{
	/* Just make an URL and let the user fetch the info */
	imcb_log(ic, "%s\n%s: %s%s", _("User Info"), _("For now, fetch yourself"), PROFILE_URL, who);
}

static void msn_add_buddy(struct im_connection *ic, char *who, char *group)
{
	struct bee_user *bu = bee_user_by_handle(ic->bee, ic, who);

	msn_buddy_list_add(ic, MSN_BUDDY_FL, who, who, group);
	if (bu && bu->group) {
		msn_buddy_list_remove(ic, MSN_BUDDY_FL, who, bu->group->name);
	}
}

static void msn_remove_buddy(struct im_connection *ic, char *who, char *group)
{
	msn_buddy_list_remove(ic, MSN_BUDDY_FL, who, NULL);
}

static void msn_chat_msg(struct groupchat *c, char *message, int flags)
{
	/* TODO: groupchats*/
}

static void msn_chat_invite(struct groupchat *c, char *who, char *message)
{
	/* TODO: groupchats*/
}

static void msn_chat_leave(struct groupchat *c)
{
	/* TODO: groupchats*/
}

static struct groupchat *msn_chat_with(struct im_connection *ic, char *who)
{
	/* TODO: groupchats*/
	struct groupchat *c = imcb_chat_new(ic, who);
	return c;
}

static void msn_keepalive(struct im_connection *ic)
{
	msn_ns_write(ic, -1, "PNG\r\n");
}

static void msn_add_permit(struct im_connection *ic, char *who)
{
	msn_buddy_list_add(ic, MSN_BUDDY_AL, who, who, NULL);
}

static void msn_rem_permit(struct im_connection *ic, char *who)
{
	msn_buddy_list_remove(ic, MSN_BUDDY_AL, who, NULL);
}

static void msn_add_deny(struct im_connection *ic, char *who)
{
	msn_buddy_list_add(ic, MSN_BUDDY_BL, who, who, NULL);
}

static void msn_rem_deny(struct im_connection *ic, char *who)
{
	msn_buddy_list_remove(ic, MSN_BUDDY_BL, who, NULL);
}

static int msn_send_typing(struct im_connection *ic, char *who, int typing)
{
	struct bee_user *bu = bee_user_by_handle(ic->bee, ic, who);

	if (!(bu->flags & BEE_USER_ONLINE)) {
		return 0;
	} else if (typing & OPT_TYPING) {
		return msn_ns_send_typing(ic, bu);
	} else {
		return 1;
	}
}

static char *set_eval_display_name(set_t *set, char *value)
{
	account_t *acc = set->data;
	struct im_connection *ic = acc->ic;
	struct msn_data *md = ic->proto_data;

	if (md->flags & MSN_EMAIL_UNVERIFIED) {
		imcb_log(ic, "Warning: Your e-mail address is unverified. MSN doesn't allow "
		         "changing your display name until your e-mail address is verified.");
	}

	if (md->flags & MSN_GOT_PROFILE_DN) {
		msn_soap_profile_set_dn(ic, value);
	} else {
		msn_soap_addressbook_set_display_name(ic, value);
	}

	return msn_ns_set_display_name(ic, value) ? value : NULL;
}

static void msn_buddy_data_add(bee_user_t *bu)
{
	struct msn_data *md = bu->ic->proto_data;
	struct msn_buddy_data *bd;
	char *handle;

	bd = bu->data = g_new0(struct msn_buddy_data, 1);
	g_tree_insert(md->domaintree, bu->handle, bu);

	for (handle = bu->handle; g_ascii_isdigit(*handle); handle++) {
		;
	}
	if (*handle == ':') {
		/* Pass a nick hint so hopefully the stupid numeric prefix
		   won't show up to the user.  */
		char *s = strchr(++handle, '@');
		if (s) {
			handle = g_strndup(handle, s - handle);
			imcb_buddy_nick_hint(bu->ic, bu->handle, handle);
			g_free(handle);
		}

		bd->flags |= MSN_BUDDY_FED;
	}
}

static void msn_buddy_data_free(bee_user_t *bu)
{
	struct msn_data *md = bu->ic->proto_data;
	struct msn_buddy_data *bd = bu->data;

	g_free(bd->cid);
	g_free(bd);

	g_tree_remove(md->domaintree, bu->handle);
}

void msn_initmodule()
{
	struct prpl *ret = g_new0(struct prpl, 1);

	ret->name = "msn";
	ret->mms = 1409;         /* this guess taken from libotr UPGRADING file */
	ret->login = msn_login;
	ret->init = msn_init;
	ret->logout = msn_logout;
	ret->buddy_msg = msn_buddy_msg;
	ret->away_states = msn_away_states;
	ret->set_away = msn_set_away;
	ret->get_info = msn_get_info;
	ret->add_buddy = msn_add_buddy;
	ret->remove_buddy = msn_remove_buddy;
	ret->chat_msg = msn_chat_msg;
	ret->chat_invite = msn_chat_invite;
	ret->chat_leave = msn_chat_leave;
	ret->chat_with = msn_chat_with;
	ret->keepalive = msn_keepalive;
	ret->add_permit = msn_add_permit;
	ret->rem_permit = msn_rem_permit;
	ret->add_deny = msn_add_deny;
	ret->rem_deny = msn_rem_deny;
	ret->send_typing = msn_send_typing;
	ret->handle_cmp = g_strcasecmp;
	ret->buddy_data_add = msn_buddy_data_add;
	ret->buddy_data_free = msn_buddy_data_free;

	register_protocol(ret);
}
