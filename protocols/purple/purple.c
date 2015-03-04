/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  libpurple module - Main file                                             *
*                                                                           *
*  Copyright 2009-2012 Wilmer van der Gaast <wilmer@gaast.net>              *
*                                                                           *
*  This program is free software; you can redistribute it and/or modify     *
*  it under the terms of the GNU General Public License as published by     *
*  the Free Software Foundation; either version 2 of the License, or        *
*  (at your option) any later version.                                      *
*                                                                           *
*  This program is distributed in the hope that it will be useful,          *
*  but WITHOUT ANY WARRANTY; without even the implied warranty of           *
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
*  GNU General Public License for more details.                             *
*                                                                           *
*  You should have received a copy of the GNU General Public License along  *
*  with this program; if not, write to the Free Software Foundation, Inc.,  *
*  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.              *
*                                                                           *
\***************************************************************************/

#include "bitlbee.h"
#include "bpurple.h"
#include "help.h"

#include <stdarg.h>

#include <glib.h>
#include <purple.h>

GSList *purple_connections;

/* This makes me VERY sad... :-( But some libpurple callbacks come in without
   any context so this is the only way to get that. Don't want to support
   libpurple in daemon mode anyway. */
static bee_t *local_bee;

static char *set_eval_display_name(set_t *set, char *value);

struct im_connection *purple_ic_by_pa(PurpleAccount *pa)
{
	GSList *i;
	struct purple_data *pd;

	for (i = purple_connections; i; i = i->next) {
		pd = ((struct im_connection *) i->data)->proto_data;
		if (pd->account == pa) {
			return i->data;
		}
	}

	return NULL;
}

static struct im_connection *purple_ic_by_gc(PurpleConnection *gc)
{
	return purple_ic_by_pa(purple_connection_get_account(gc));
}

static gboolean purple_menu_cmp(const char *a, const char *b)
{
	while (*a && *b) {
		while (*a == '_') {
			a++;
		}
		while (*b == '_') {
			b++;
		}
		if (g_ascii_tolower(*a) != g_ascii_tolower(*b)) {
			return FALSE;
		}

		a++;
		b++;
	}

	return (*a == '\0' && *b == '\0');
}

static void purple_init(account_t *acc)
{
	PurplePlugin *prpl = purple_plugins_find_with_id((char *) acc->prpl->data);
	PurplePluginProtocolInfo *pi = prpl->info->extra_info;
	PurpleAccount *pa;
	GList *i, *st;
	set_t *s;
	char help_title[64];
	GString *help;
	static gboolean dir_fixed = FALSE;

	/* Layer violation coming up: Making an exception for libpurple here.
	   Dig in the IRC state a bit to get a username. Ideally we should
	   check if s/he identified but this info doesn't seem *that* important.
	   It's just that fecking libpurple can't *not* store this shit.

	   Remember that libpurple is not really meant to be used on public
	   servers anyway! */
	if (!dir_fixed) {
		irc_t *irc = acc->bee->ui_data;
		char *dir;

		dir = g_strdup_printf("%s/purple/%s", global.conf->configdir, irc->user->nick);
		purple_util_set_user_dir(dir);
		g_free(dir);

		purple_blist_load();
		purple_prefs_load();
		dir_fixed = TRUE;
	}

	help = g_string_new("");
	g_string_printf(help, "BitlBee libpurple module %s (%s).\n\nSupported settings:",
	                (char *) acc->prpl->name, prpl->info->name);

	if (pi->user_splits) {
		GList *l;
		g_string_append_printf(help, "\n* username: Username");
		for (l = pi->user_splits; l; l = l->next) {
			g_string_append_printf(help, "%c%s",
			                       purple_account_user_split_get_separator(l->data),
			                       purple_account_user_split_get_text(l->data));
		}
	}

	/* Convert all protocol_options into per-account setting variables. */
	for (i = pi->protocol_options; i; i = i->next) {
		PurpleAccountOption *o = i->data;
		const char *name;
		char *def = NULL;
		set_eval eval = NULL;
		void *eval_data = NULL;
		GList *io = NULL;
		GSList *opts = NULL;

		name = purple_account_option_get_setting(o);

		switch (purple_account_option_get_type(o)) {
		case PURPLE_PREF_STRING:
			def = g_strdup(purple_account_option_get_default_string(o));

			g_string_append_printf(help, "\n* %s (%s), %s, default: %s",
			                       name, purple_account_option_get_text(o),
			                       "string", def);

			break;

		case PURPLE_PREF_INT:
			def = g_strdup_printf("%d", purple_account_option_get_default_int(o));
			eval = set_eval_int;

			g_string_append_printf(help, "\n* %s (%s), %s, default: %s",
			                       name, purple_account_option_get_text(o),
			                       "integer", def);

			break;

		case PURPLE_PREF_BOOLEAN:
			if (purple_account_option_get_default_bool(o)) {
				def = g_strdup("true");
			} else {
				def = g_strdup("false");
			}
			eval = set_eval_bool;

			g_string_append_printf(help, "\n* %s (%s), %s, default: %s",
			                       name, purple_account_option_get_text(o),
			                       "boolean", def);

			break;

		case PURPLE_PREF_STRING_LIST:
			def = g_strdup(purple_account_option_get_default_list_value(o));

			g_string_append_printf(help, "\n* %s (%s), %s, default: %s",
			                       name, purple_account_option_get_text(o),
			                       "list", def);
			g_string_append(help, "\n  Possible values: ");

			for (io = purple_account_option_get_list(o); io; io = io->next) {
				PurpleKeyValuePair *kv = io->data;
				opts = g_slist_append(opts, kv->value);
				/* TODO: kv->value is not a char*, WTF? */
				if (strcmp(kv->value, kv->key) != 0) {
					g_string_append_printf(help, "%s (%s), ", (char *) kv->value, kv->key);
				} else {
					g_string_append_printf(help, "%s, ", (char *) kv->value);
				}
			}
			g_string_truncate(help, help->len - 2);
			eval = set_eval_list;
			eval_data = opts;

			break;

		default:
			/** No way to talk to the user right now, invent one when
			this becomes important.
			irc_rootmsg( acc->irc, "Setting with unknown type: %s (%d) Expect stuff to break..\n",
			             name, purple_account_option_get_type( o ) );
			*/
			g_string_append_printf(help, "\n* [%s] UNSUPPORTED (type %d)",
			                       name, purple_account_option_get_type(o));
			name = NULL;
		}

		if (name != NULL) {
			s = set_add(&acc->set, name, def, eval, acc);
			s->flags |= ACC_SET_OFFLINE_ONLY;
			s->eval_data = eval_data;
			g_free(def);
		}
	}

	g_snprintf(help_title, sizeof(help_title), "purple %s", (char *) acc->prpl->name);
	help_add_mem(&global.help, help_title, help->str);
	g_string_free(help, TRUE);

	s = set_add(&acc->set, "display_name", NULL, set_eval_display_name, acc);
	s->flags |= ACC_SET_ONLINE_ONLY;

	if (pi->options & OPT_PROTO_MAIL_CHECK) {
		s = set_add(&acc->set, "mail_notifications", "false", set_eval_bool, acc);
		s->flags |= ACC_SET_OFFLINE_ONLY;
	}

	if (strcmp(prpl->info->name, "Gadu-Gadu") == 0) {
		s = set_add(&acc->set, "gg_sync_contacts", "true", set_eval_bool, acc);
	}

	/* Go through all away states to figure out if away/status messages
	   are possible. */
	pa = purple_account_new(acc->user, (char *) acc->prpl->data);
	for (st = purple_account_get_status_types(pa); st; st = st->next) {
		PurpleStatusPrimitive prim = purple_status_type_get_primitive(st->data);

		if (prim == PURPLE_STATUS_AVAILABLE) {
			if (purple_status_type_get_attr(st->data, "message")) {
				acc->flags |= ACC_FLAG_STATUS_MESSAGE;
			}
		} else if (prim != PURPLE_STATUS_OFFLINE) {
			if (purple_status_type_get_attr(st->data, "message")) {
				acc->flags |= ACC_FLAG_AWAY_MESSAGE;
			}
		}
	}
	purple_accounts_remove(pa);
}

static void purple_sync_settings(account_t *acc, PurpleAccount *pa)
{
	PurplePlugin *prpl = purple_plugins_find_with_id(pa->protocol_id);
	PurplePluginProtocolInfo *pi = prpl->info->extra_info;
	GList *i;

	for (i = pi->protocol_options; i; i = i->next) {
		PurpleAccountOption *o = i->data;
		const char *name;
		set_t *s;

		name = purple_account_option_get_setting(o);
		s = set_find(&acc->set, name);
		if (s->value == NULL) {
			continue;
		}

		switch (purple_account_option_get_type(o)) {
		case PURPLE_PREF_STRING:
		case PURPLE_PREF_STRING_LIST:
			purple_account_set_string(pa, name, set_getstr(&acc->set, name));
			break;

		case PURPLE_PREF_INT:
			purple_account_set_int(pa, name, set_getint(&acc->set, name));
			break;

		case PURPLE_PREF_BOOLEAN:
			purple_account_set_bool(pa, name, set_getbool(&acc->set, name));
			break;

		default:
			break;
		}
	}

	if (pi->options & OPT_PROTO_MAIL_CHECK) {
		purple_account_set_check_mail(pa, set_getbool(&acc->set, "mail_notifications"));
	}
}

static void purple_login(account_t *acc)
{
	struct im_connection *ic = imcb_new(acc);
	struct purple_data *pd;

	if ((local_bee != NULL && local_bee != acc->bee) ||
	    (global.conf->runmode == RUNMODE_DAEMON && !getenv("BITLBEE_DEBUG"))) {
		imcb_error(ic,  "Daemon mode detected. Do *not* try to use libpurple in daemon mode! "
		           "Please use inetd or ForkDaemon mode instead.");
		imc_logout(ic, FALSE);
		return;
	}
	local_bee = acc->bee;

	/* For now this is needed in the _connected() handlers if using
	   GLib event handling, to make sure we're not handling events
	   on dead connections. */
	purple_connections = g_slist_prepend(purple_connections, ic);

	ic->proto_data = pd = g_new0(struct purple_data, 1);
	pd->account = purple_account_new(acc->user, (char *) acc->prpl->data);
	purple_account_set_password(pd->account, acc->pass);
	purple_sync_settings(acc, pd->account);

	purple_account_set_enabled(pd->account, "BitlBee", TRUE);
}

static void purple_logout(struct im_connection *ic)
{
	struct purple_data *pd = ic->proto_data;

	purple_account_set_enabled(pd->account, "BitlBee", FALSE);
	purple_connections = g_slist_remove(purple_connections, ic);
	purple_accounts_remove(pd->account);
	g_free(pd);
}

static int purple_buddy_msg(struct im_connection *ic, char *who, char *message, int flags)
{
	PurpleConversation *conv;
	struct purple_data *pd = ic->proto_data;

	if ((conv = purple_find_conversation_with_account(PURPLE_CONV_TYPE_IM,
	                                                  who, pd->account)) == NULL) {
		conv = purple_conversation_new(PURPLE_CONV_TYPE_IM,
		                               pd->account, who);
	}

	purple_conv_im_send(purple_conversation_get_im_data(conv), message);

	return 1;
}

static GList *purple_away_states(struct im_connection *ic)
{
	struct purple_data *pd = ic->proto_data;
	GList *st, *ret = NULL;

	for (st = purple_account_get_status_types(pd->account); st; st = st->next) {
		PurpleStatusPrimitive prim = purple_status_type_get_primitive(st->data);
		if (prim != PURPLE_STATUS_AVAILABLE && prim != PURPLE_STATUS_OFFLINE) {
			ret = g_list_append(ret, (void *) purple_status_type_get_name(st->data));
		}
	}

	return ret;
}

static void purple_set_away(struct im_connection *ic, char *state_txt, char *message)
{
	struct purple_data *pd = ic->proto_data;
	GList *status_types = purple_account_get_status_types(pd->account), *st;
	PurpleStatusType *pst = NULL;
	GList *args = NULL;

	for (st = status_types; st; st = st->next) {
		pst = st->data;

		if (state_txt == NULL &&
		    purple_status_type_get_primitive(pst) == PURPLE_STATUS_AVAILABLE) {
			break;
		}

		if (state_txt != NULL &&
		    g_strcasecmp(state_txt, purple_status_type_get_name(pst)) == 0) {
			break;
		}
	}

	if (message && purple_status_type_get_attr(pst, "message")) {
		args = g_list_append(args, "message");
		args = g_list_append(args, message);
	}

	purple_account_set_status_list(pd->account,
	                               st ? purple_status_type_get_id(pst) : "away",
	                               TRUE, args);

	g_list_free(args);
}

static char *set_eval_display_name(set_t *set, char *value)
{
	account_t *acc = set->data;
	struct im_connection *ic = acc->ic;

	if (ic) {
		imcb_log(ic, "Changing display_name not currently supported with libpurple!");
	}

	return NULL;
}

/* Bad bad gadu-gadu, not saving buddy list by itself */
static void purple_gg_buddylist_export(PurpleConnection *gc)
{
	struct im_connection *ic = purple_ic_by_gc(gc);

	if (set_getstr(&ic->acc->set, "gg_sync_contacts")) {
		GList *actions = gc->prpl->info->actions(gc->prpl, gc);
		GList *p;
		for (p = g_list_first(actions); p; p = p->next) {
			if (((PurplePluginAction *) p->data) &&
			    purple_menu_cmp(((PurplePluginAction *) p->data)->label,
			                    "Upload buddylist to Server") == 0) {
				PurplePluginAction action;
				action.plugin = gc->prpl;
				action.context = gc;
				action.user_data = NULL;
				((PurplePluginAction *) p->data)->callback(&action);
				break;
			}
		}
		g_list_free(actions);
	}
}

static void purple_gg_buddylist_import(PurpleConnection *gc)
{
	struct im_connection *ic = purple_ic_by_gc(gc);

	if (set_getstr(&ic->acc->set, "gg_sync_contacts")) {
		GList *actions = gc->prpl->info->actions(gc->prpl, gc);
		GList *p;
		for (p = g_list_first(actions); p; p = p->next) {
			if (((PurplePluginAction *) p->data) &&
			    purple_menu_cmp(((PurplePluginAction *) p->data)->label,
			                    "Download buddylist from Server") == 0) {
				PurplePluginAction action;
				action.plugin = gc->prpl;
				action.context = gc;
				action.user_data = NULL;
				((PurplePluginAction *) p->data)->callback(&action);
				break;
			}
		}
		g_list_free(actions);
	}
}

static void purple_add_buddy(struct im_connection *ic, char *who, char *group)
{
	PurpleBuddy *pb;
	PurpleGroup *pg = NULL;
	struct purple_data *pd = ic->proto_data;

	if (group && !(pg = purple_find_group(group))) {
		pg = purple_group_new(group);
		purple_blist_add_group(pg, NULL);
	}

	pb = purple_buddy_new(pd->account, who, NULL);
	purple_blist_add_buddy(pb, NULL, pg, NULL);
	purple_account_add_buddy(pd->account, pb);

	purple_gg_buddylist_export(pd->account->gc);
}

static void purple_remove_buddy(struct im_connection *ic, char *who, char *group)
{
	PurpleBuddy *pb;
	struct purple_data *pd = ic->proto_data;

	pb = purple_find_buddy(pd->account, who);
	if (pb != NULL) {
		PurpleGroup *group;

		group = purple_buddy_get_group(pb);
		purple_account_remove_buddy(pd->account, pb, group);

		purple_blist_remove_buddy(pb);
	}

	purple_gg_buddylist_export(pd->account->gc);
}

static void purple_add_permit(struct im_connection *ic, char *who)
{
	struct purple_data *pd = ic->proto_data;

	purple_privacy_permit_add(pd->account, who, FALSE);
}

static void purple_add_deny(struct im_connection *ic, char *who)
{
	struct purple_data *pd = ic->proto_data;

	purple_privacy_deny_add(pd->account, who, FALSE);
}

static void purple_rem_permit(struct im_connection *ic, char *who)
{
	struct purple_data *pd = ic->proto_data;

	purple_privacy_permit_remove(pd->account, who, FALSE);
}

static void purple_rem_deny(struct im_connection *ic, char *who)
{
	struct purple_data *pd = ic->proto_data;

	purple_privacy_deny_remove(pd->account, who, FALSE);
}

static void purple_get_info(struct im_connection *ic, char *who)
{
	struct purple_data *pd = ic->proto_data;

	serv_get_info(purple_account_get_connection(pd->account), who);
}

static void purple_keepalive(struct im_connection *ic)
{
}

static int purple_send_typing(struct im_connection *ic, char *who, int flags)
{
	PurpleTypingState state = PURPLE_NOT_TYPING;
	struct purple_data *pd = ic->proto_data;

	if (flags & OPT_TYPING) {
		state = PURPLE_TYPING;
	} else if (flags & OPT_THINKING) {
		state = PURPLE_TYPED;
	}

	serv_send_typing(purple_account_get_connection(pd->account), who, state);

	return 1;
}

static void purple_chat_msg(struct groupchat *gc, char *message, int flags)
{
	PurpleConversation *pc = gc->data;

	purple_conv_chat_send(purple_conversation_get_chat_data(pc), message);
}

struct groupchat *purple_chat_with(struct im_connection *ic, char *who)
{
	/* No, "of course" this won't work this way. Or in fact, it almost
	   does, but it only lets you send msgs to it, you won't receive
	   any. Instead, we have to click the virtual menu item.
	PurpleAccount *pa = ic->proto_data;
	PurpleConversation *pc;
	PurpleConvChat *pcc;
	struct groupchat *gc;

	gc = imcb_chat_new( ic, "BitlBee-libpurple groupchat" );
	gc->data = pc = purple_conversation_new( PURPLE_CONV_TYPE_CHAT, pa, "BitlBee-libpurple groupchat" );
	pc->ui_data = gc;

	pcc = PURPLE_CONV_CHAT( pc );
	purple_conv_chat_add_user( pcc, ic->acc->user, "", 0, TRUE );
	purple_conv_chat_invite_user( pcc, who, "Please join my chat", FALSE );
	//purple_conv_chat_add_user( pcc, who, "", 0, TRUE );
	*/

	/* There went my nice afternoon. :-( */

	struct purple_data *pd = ic->proto_data;
	PurplePlugin *prpl = purple_plugins_find_with_id(pd->account->protocol_id);
	PurplePluginProtocolInfo *pi = prpl->info->extra_info;
	PurpleBuddy *pb = purple_find_buddy(pd->account, who);
	PurpleMenuAction *mi;
	GList *menu;

	void (*callback)(PurpleBlistNode *, gpointer); /* FFFFFFFFFFFFFUUUUUUUUUUUUUU */

	if (!pb || !pi || !pi->blist_node_menu) {
		return NULL;
	}

	menu = pi->blist_node_menu(&pb->node);
	while (menu) {
		mi = menu->data;
		if (purple_menu_cmp(mi->label, "initiate chat") ||
		    purple_menu_cmp(mi->label, "initiate conference")) {
			break;
		}
		menu = menu->next;
	}

	if (menu == NULL) {
		return NULL;
	}

	/* Call the fucker. */
	callback = (void *) mi->callback;
	callback(&pb->node, menu->data);

	return NULL;
}

void purple_chat_invite(struct groupchat *gc, char *who, char *message)
{
	PurpleConversation *pc = gc->data;
	PurpleConvChat *pcc = PURPLE_CONV_CHAT(pc);
	struct purple_data *pd = gc->ic->proto_data;

	serv_chat_invite(purple_account_get_connection(pd->account),
	                 purple_conv_chat_get_id(pcc),
	                 message && *message ? message : "Please join my chat",
	                 who);
}

void purple_chat_kick(struct groupchat *gc, char *who, const char *message)
{
	PurpleConversation *pc = gc->data;
	char *str = g_strdup_printf("kick %s %s", who, message);

	purple_conversation_do_command(pc, str, NULL, NULL);
	g_free(str);
}

void purple_chat_leave(struct groupchat *gc)
{
	PurpleConversation *pc = gc->data;

	purple_conversation_destroy(pc);
}

struct groupchat *purple_chat_join(struct im_connection *ic, const char *room, const char *nick, const char *password,
                                   set_t **sets)
{
	struct purple_data *pd = ic->proto_data;
	PurplePlugin *prpl = purple_plugins_find_with_id(pd->account->protocol_id);
	PurplePluginProtocolInfo *pi = prpl->info->extra_info;
	GHashTable *chat_hash;
	PurpleConversation *conv;
	GList *info, *l;

	if (!pi->chat_info || !pi->chat_info_defaults ||
	    !(info = pi->chat_info(purple_account_get_connection(pd->account)))) {
		imcb_error(ic, "Joining chatrooms not supported by this protocol");
		return NULL;
	}

	if ((conv = purple_find_conversation_with_account(PURPLE_CONV_TYPE_CHAT,
	                                                  room, pd->account))) {
		purple_conversation_destroy(conv);
	}

	chat_hash = pi->chat_info_defaults(
	        purple_account_get_connection(pd->account), room
	);

	for (l = info; l; l = l->next) {
		struct proto_chat_entry *pce = l->data;

		if (strcmp(pce->identifier, "handle") == 0) {
			g_hash_table_replace(chat_hash, "handle", g_strdup(nick));
		} else if (strcmp(pce->identifier, "password") == 0) {
			g_hash_table_replace(chat_hash, "password", g_strdup(password));
		} else if (strcmp(pce->identifier, "passwd") == 0) {
			g_hash_table_replace(chat_hash, "passwd", g_strdup(password));
		}
	}

	serv_join_chat(purple_account_get_connection(pd->account), chat_hash);

	return NULL;
}

void purple_transfer_request(struct im_connection *ic, file_transfer_t *ft, char *handle);

static void purple_ui_init();

GHashTable *prplcb_ui_info()
{
	static GHashTable *ret;

	if (ret == NULL) {
		ret = g_hash_table_new(g_str_hash, g_str_equal);
		g_hash_table_insert(ret, "name", "BitlBee");
		g_hash_table_insert(ret, "version", BITLBEE_VERSION);
	}

	return ret;
}

static PurpleCoreUiOps bee_core_uiops =
{
	NULL,
	NULL,
	purple_ui_init,
	NULL,
	prplcb_ui_info,
};

static void prplcb_conn_progress(PurpleConnection *gc, const char *text, size_t step, size_t step_count)
{
	struct im_connection *ic = purple_ic_by_gc(gc);

	imcb_log(ic, "%s", text);
}

static void prplcb_conn_connected(PurpleConnection *gc)
{
	struct im_connection *ic = purple_ic_by_gc(gc);
	const char *dn;
	set_t *s;

	imcb_connected(ic);

	if ((dn = purple_connection_get_display_name(gc)) &&
	    (s = set_find(&ic->acc->set, "display_name"))) {
		g_free(s->value);
		s->value = g_strdup(dn);
	}

	// user list needs to be requested for Gadu-Gadu
	purple_gg_buddylist_import(gc);

	if (gc->flags & PURPLE_CONNECTION_HTML) {
		ic->flags |= OPT_DOES_HTML;
	}
}

static void prplcb_conn_disconnected(PurpleConnection *gc)
{
	struct im_connection *ic = purple_ic_by_gc(gc);

	if (ic != NULL) {
		imc_logout(ic, !gc->wants_to_die);
	}
}

static void prplcb_conn_notice(PurpleConnection *gc, const char *text)
{
	struct im_connection *ic = purple_ic_by_gc(gc);

	if (ic != NULL) {
		imcb_log(ic, "%s", text);
	}
}

static void prplcb_conn_report_disconnect_reason(PurpleConnection *gc, PurpleConnectionError reason, const char *text)
{
	struct im_connection *ic = purple_ic_by_gc(gc);

	/* PURPLE_CONNECTION_ERROR_NAME_IN_USE means concurrent login,
	   should probably handle that. */
	if (ic != NULL) {
		imcb_error(ic, "%s", text);
	}
}

static PurpleConnectionUiOps bee_conn_uiops =
{
	prplcb_conn_progress,
	prplcb_conn_connected,
	prplcb_conn_disconnected,
	prplcb_conn_notice,
	NULL,
	NULL,
	NULL,
	prplcb_conn_report_disconnect_reason,
};

static void prplcb_blist_update(PurpleBuddyList *list, PurpleBlistNode *node)
{
	if (node->type == PURPLE_BLIST_BUDDY_NODE) {
		PurpleBuddy *bud = (PurpleBuddy *) node;
		PurpleGroup *group = purple_buddy_get_group(bud);
		struct im_connection *ic = purple_ic_by_pa(bud->account);
		PurpleStatus *as;
		int flags = 0;

		if (ic == NULL) {
			return;
		}

		if (bud->server_alias) {
			imcb_rename_buddy(ic, bud->name, bud->server_alias);
		} else if (bud->alias) {
			imcb_rename_buddy(ic, bud->name, bud->alias);
		}

		if (group) {
			imcb_add_buddy(ic, bud->name, purple_group_get_name(group));
		}

		flags |= purple_presence_is_online(bud->presence) ? OPT_LOGGED_IN : 0;
		flags |= purple_presence_is_available(bud->presence) ? 0 : OPT_AWAY;

		as = purple_presence_get_active_status(bud->presence);

		imcb_buddy_status(ic, bud->name, flags, purple_status_get_name(as),
		                  purple_status_get_attr_string(as, "message"));

		imcb_buddy_times(ic, bud->name,
		                 purple_presence_get_login_time(bud->presence),
		                 purple_presence_get_idle_time(bud->presence));
	}
}

static void prplcb_blist_new(PurpleBlistNode *node)
{
	if (node->type == PURPLE_BLIST_BUDDY_NODE) {
		PurpleBuddy *bud = (PurpleBuddy *) node;
		struct im_connection *ic = purple_ic_by_pa(bud->account);

		if (ic == NULL) {
			return;
		}

		imcb_add_buddy(ic, bud->name, NULL);

		prplcb_blist_update(NULL, node);
	}
}

static void prplcb_blist_remove(PurpleBuddyList *list, PurpleBlistNode *node)
{
/*
        PurpleBuddy *bud = (PurpleBuddy*) node;

        if( node->type == PURPLE_BLIST_BUDDY_NODE )
        {
                struct im_connection *ic = purple_ic_by_pa( bud->account );

                if( ic == NULL )
                        return;

                imcb_remove_buddy( ic, bud->name, NULL );
        }
*/
}

static PurpleBlistUiOps bee_blist_uiops =
{
	NULL,
	prplcb_blist_new,
	NULL,
	prplcb_blist_update,
	prplcb_blist_remove,
};

void prplcb_conv_new(PurpleConversation *conv)
{
	if (conv->type == PURPLE_CONV_TYPE_CHAT) {
		struct im_connection *ic = purple_ic_by_pa(conv->account);
		struct groupchat *gc;

		gc = imcb_chat_new(ic, conv->name);
		if (conv->title != NULL) {
			imcb_chat_name_hint(gc, conv->title);
			imcb_chat_topic(gc, NULL, conv->title, 0);
		}

		conv->ui_data = gc;
		gc->data = conv;

		/* libpurple brokenness: Whatever. Show that we join right away,
		   there's no clear "This is you!" signaling in _add_users so
		   don't even try. */
		imcb_chat_add_buddy(gc, gc->ic->acc->user);
	}
}

void prplcb_conv_free(PurpleConversation *conv)
{
	struct groupchat *gc = conv->ui_data;

	imcb_chat_free(gc);
}

void prplcb_conv_add_users(PurpleConversation *conv, GList *cbuddies, gboolean new_arrivals)
{
	struct groupchat *gc = conv->ui_data;
	GList *b;

	for (b = cbuddies; b; b = b->next) {
		PurpleConvChatBuddy *pcb = b->data;

		imcb_chat_add_buddy(gc, pcb->name);
	}
}

void prplcb_conv_del_users(PurpleConversation *conv, GList *cbuddies)
{
	struct groupchat *gc = conv->ui_data;
	GList *b;

	for (b = cbuddies; b; b = b->next) {
		imcb_chat_remove_buddy(gc, b->data, "");
	}
}

void prplcb_conv_chat_msg(PurpleConversation *conv, const char *who, const char *message, PurpleMessageFlags flags,
                          time_t mtime)
{
	struct groupchat *gc = conv->ui_data;
	PurpleBuddy *buddy;

	/* ..._SEND means it's an outgoing message, no need to echo those. */
	if (flags & PURPLE_MESSAGE_SEND) {
		return;
	}

	buddy = purple_find_buddy(conv->account, who);
	if (buddy != NULL) {
		who = purple_buddy_get_name(buddy);
	}

	imcb_chat_msg(gc, who, (char *) message, 0, mtime);
}

static void prplcb_conv_im(PurpleConversation *conv, const char *who, const char *message, PurpleMessageFlags flags,
                           time_t mtime)
{
	struct im_connection *ic = purple_ic_by_pa(conv->account);
	PurpleBuddy *buddy;

	/* ..._SEND means it's an outgoing message, no need to echo those. */
	if (flags & PURPLE_MESSAGE_SEND) {
		return;
	}

	buddy = purple_find_buddy(conv->account, who);
	if (buddy != NULL) {
		who = purple_buddy_get_name(buddy);
	}

	imcb_buddy_msg(ic, (char *) who, (char *) message, 0, mtime);
}

/* No, this is not a ui_op but a signal. */
static void prplcb_buddy_typing(PurpleAccount *account, const char *who, gpointer null)
{
	PurpleConversation *conv;
	PurpleConvIm *im;
	int state;

	if ((conv = purple_find_conversation_with_account(PURPLE_CONV_TYPE_IM, who, account)) == NULL) {
		return;
	}

	im = PURPLE_CONV_IM(conv);
	switch (purple_conv_im_get_typing_state(im)) {
	case PURPLE_TYPING:
		state = OPT_TYPING;
		break;
	case PURPLE_TYPED:
		state = OPT_THINKING;
		break;
	default:
		state = 0;
	}

	imcb_buddy_typing(purple_ic_by_pa(account), who, state);
}

static PurpleConversationUiOps bee_conv_uiops =
{
	prplcb_conv_new,           /* create_conversation  */
	prplcb_conv_free,          /* destroy_conversation */
	prplcb_conv_chat_msg,      /* write_chat           */
	prplcb_conv_im,            /* write_im             */
	NULL,                      /* write_conv           */
	prplcb_conv_add_users,     /* chat_add_users       */
	NULL,                      /* chat_rename_user     */
	prplcb_conv_del_users,     /* chat_remove_users    */
	NULL,                      /* chat_update_user     */
	NULL,                      /* present              */
	NULL,                      /* has_focus            */
	NULL,                      /* custom_smiley_add    */
	NULL,                      /* custom_smiley_write  */
	NULL,                      /* custom_smiley_close  */
	NULL,                      /* send_confirm         */
};

struct prplcb_request_action_data {
	void *user_data, *bee_data;
	PurpleRequestActionCb yes, no;
	int yes_i, no_i;
};

static void prplcb_request_action_yes(void *data)
{
	struct prplcb_request_action_data *pqad = data;

	if (pqad->yes) {
		pqad->yes(pqad->user_data, pqad->yes_i);
	}
	g_free(pqad);
}

static void prplcb_request_action_no(void *data)
{
	struct prplcb_request_action_data *pqad = data;

	if (pqad->no) {
		pqad->no(pqad->user_data, pqad->no_i);
	}
	g_free(pqad);
}

static void *prplcb_request_action(const char *title, const char *primary, const char *secondary,
                                   int default_action, PurpleAccount *account, const char *who,
                                   PurpleConversation *conv, void *user_data, size_t action_count,
                                   va_list actions)
{
	struct prplcb_request_action_data *pqad;
	int i;
	char *q;

	pqad = g_new0(struct prplcb_request_action_data, 1);

	for (i = 0; i < action_count; i++) {
		char *caption;
		void *fn;

		caption = va_arg(actions, char*);
		fn = va_arg(actions, void*);

		if (strstr(caption, "Accept") || strstr(caption, "OK")) {
			pqad->yes = fn;
			pqad->yes_i = i;
		} else if (strstr(caption, "Reject") || strstr(caption, "Cancel")) {
			pqad->no = fn;
			pqad->no_i = i;
		}
	}

	pqad->user_data = user_data;

	/* TODO: IRC stuff here :-( */
	q = g_strdup_printf("Request: %s\n\n%s\n\n%s", title, primary, secondary);
	pqad->bee_data = query_add(local_bee->ui_data, purple_ic_by_pa(account), q,
	                           prplcb_request_action_yes, prplcb_request_action_no, g_free, pqad);

	g_free(q);

	return pqad;
}

/* So it turns out some requests have no account context at all, because
 * libpurple hates us. This means that query_del_by_conn() won't remove those
 * on logout, and will segfault if the user replies. That's why this exists.
 */
static void prplcb_close_request(PurpleRequestType type, void *data)
{
	if (type == PURPLE_REQUEST_ACTION) {
		struct prplcb_request_action_data *pqad = data;
		query_del(local_bee->ui_data, pqad->bee_data);
	}
	/* Add the request input handler here when that becomes a thing */
}

/*
static void prplcb_request_test()
{
        fprintf( stderr, "bla\n" );
}
*/

static PurpleRequestUiOps bee_request_uiops =
{
	NULL,
	NULL,
	prplcb_request_action,
	NULL,
	NULL,
	prplcb_close_request,
	NULL,
};

static void prplcb_privacy_permit_added(PurpleAccount *account, const char *name)
{
	struct im_connection *ic = purple_ic_by_pa(account);

	if (!g_slist_find_custom(ic->permit, name, (GCompareFunc) ic->acc->prpl->handle_cmp)) {
		ic->permit = g_slist_prepend(ic->permit, g_strdup(name));
	}
}

static void prplcb_privacy_permit_removed(PurpleAccount *account, const char *name)
{
	struct im_connection *ic = purple_ic_by_pa(account);
	void *n;

	n = g_slist_find_custom(ic->permit, name, (GCompareFunc) ic->acc->prpl->handle_cmp);
	ic->permit = g_slist_remove(ic->permit, n);
}

static void prplcb_privacy_deny_added(PurpleAccount *account, const char *name)
{
	struct im_connection *ic = purple_ic_by_pa(account);

	if (!g_slist_find_custom(ic->deny, name, (GCompareFunc) ic->acc->prpl->handle_cmp)) {
		ic->deny = g_slist_prepend(ic->deny, g_strdup(name));
	}
}

static void prplcb_privacy_deny_removed(PurpleAccount *account, const char *name)
{
	struct im_connection *ic = purple_ic_by_pa(account);
	void *n;

	n = g_slist_find_custom(ic->deny, name, (GCompareFunc) ic->acc->prpl->handle_cmp);
	ic->deny = g_slist_remove(ic->deny, n);
}

static PurplePrivacyUiOps bee_privacy_uiops =
{
	prplcb_privacy_permit_added,
	prplcb_privacy_permit_removed,
	prplcb_privacy_deny_added,
	prplcb_privacy_deny_removed,
};

static void prplcb_debug_print(PurpleDebugLevel level, const char *category, const char *arg_s)
{
	fprintf(stderr, "DEBUG %s: %s", category, arg_s);
}

static PurpleDebugUiOps bee_debug_uiops =
{
	prplcb_debug_print,
};

static guint prplcb_ev_timeout_add(guint interval, GSourceFunc func, gpointer udata)
{
	return b_timeout_add(interval, (b_event_handler) func, udata);
}

static guint prplcb_ev_input_add(int fd, PurpleInputCondition cond, PurpleInputFunction func, gpointer udata)
{
	return b_input_add(fd, cond | B_EV_FLAG_FORCE_REPEAT, (b_event_handler) func, udata);
}

static gboolean prplcb_ev_remove(guint id)
{
	b_event_remove((gint) id);
	return TRUE;
}

static PurpleEventLoopUiOps glib_eventloops =
{
	prplcb_ev_timeout_add,
	prplcb_ev_remove,
	prplcb_ev_input_add,
	prplcb_ev_remove,
};

static void *prplcb_notify_email(PurpleConnection *gc, const char *subject, const char *from,
                                 const char *to, const char *url)
{
	struct im_connection *ic = purple_ic_by_gc(gc);

	imcb_log(ic, "Received e-mail from %s for %s: %s <%s>", from, to, subject, url);

	return NULL;
}

static void *prplcb_notify_userinfo(PurpleConnection *gc, const char *who, PurpleNotifyUserInfo *user_info)
{
	struct im_connection *ic = purple_ic_by_gc(gc);
	GString *info = g_string_new("");
	GList *l = purple_notify_user_info_get_entries(user_info);
	char *key;
	const char *value;
	int n;

	while (l) {
		PurpleNotifyUserInfoEntry *e = l->data;

		switch (purple_notify_user_info_entry_get_type(e)) {
		case PURPLE_NOTIFY_USER_INFO_ENTRY_PAIR:
		case PURPLE_NOTIFY_USER_INFO_ENTRY_SECTION_HEADER:
			key = g_strdup(purple_notify_user_info_entry_get_label(e));
			value = purple_notify_user_info_entry_get_value(e);

			if (key) {
				strip_html(key);
				g_string_append_printf(info, "%s: ", key);

				if (value) {
					n = strlen(value) - 1;
					while (g_ascii_isspace(value[n])) {
						n--;
					}
					g_string_append_len(info, value, n + 1);
				}
				g_string_append_c(info, '\n');
				g_free(key);
			}

			break;
		case PURPLE_NOTIFY_USER_INFO_ENTRY_SECTION_BREAK:
			g_string_append(info, "------------------------\n");
			break;
		}

		l = l->next;
	}

	imcb_log(ic, "User %s info:\n%s", who, info->str);
	g_string_free(info, TRUE);

	return NULL;
}

static PurpleNotifyUiOps bee_notify_uiops =
{
	NULL,
	prplcb_notify_email,
	NULL,
	NULL,
	NULL,
	NULL,
	prplcb_notify_userinfo,
};

static void *prplcb_account_request_authorize(PurpleAccount *account, const char *remote_user,
                                              const char *id, const char *alias, const char *message, gboolean on_list,
                                              PurpleAccountRequestAuthorizationCb authorize_cb,
                                              PurpleAccountRequestAuthorizationCb deny_cb, void *user_data)
{
	struct im_connection *ic = purple_ic_by_pa(account);
	char *q;

	if (alias) {
		q = g_strdup_printf("%s (%s) wants to add you to his/her contact "
		                    "list. (%s)", alias, remote_user, message);
	} else {
		q = g_strdup_printf("%s wants to add you to his/her contact "
		                    "list. (%s)", remote_user, message);
	}

	imcb_ask_with_free(ic, q, user_data, authorize_cb, deny_cb, NULL);
	g_free(q);

	return NULL;
}

static PurpleAccountUiOps bee_account_uiops =
{
	NULL,
	NULL,
	NULL,
	prplcb_account_request_authorize,
	NULL,
};

extern PurpleXferUiOps bee_xfer_uiops;

static void purple_ui_init()
{
	purple_connections_set_ui_ops(&bee_conn_uiops);
	purple_blist_set_ui_ops(&bee_blist_uiops);
	purple_conversations_set_ui_ops(&bee_conv_uiops);
	purple_request_set_ui_ops(&bee_request_uiops);
	purple_privacy_set_ui_ops(&bee_privacy_uiops);
	purple_notify_set_ui_ops(&bee_notify_uiops);
	purple_accounts_set_ui_ops(&bee_account_uiops);
	purple_xfers_set_ui_ops(&bee_xfer_uiops);

	if (getenv("BITLBEE_DEBUG")) {
		purple_debug_set_ui_ops(&bee_debug_uiops);
	}
}

void purple_initmodule()
{
	struct prpl funcs;
	GList *prots;
	GString *help;
	char *dir;

	if (B_EV_IO_READ != PURPLE_INPUT_READ ||
	    B_EV_IO_WRITE != PURPLE_INPUT_WRITE) {
		/* FIXME FIXME FIXME FIXME FIXME :-) */
		exit(1);
	}

	dir = g_strdup_printf("%s/purple", global.conf->configdir);
	purple_util_set_user_dir(dir);
	g_free(dir);

	purple_debug_set_enabled(FALSE);
	purple_core_set_ui_ops(&bee_core_uiops);
	purple_eventloop_set_ui_ops(&glib_eventloops);
	if (!purple_core_init("BitlBee")) {
		/* Initializing the core failed. Terminate. */
		fprintf(stderr, "libpurple initialization failed.\n");
		abort();
	}

	if (proxytype != PROXY_NONE) {
		PurpleProxyInfo *pi = purple_global_proxy_get_info();
		switch (proxytype) {
		case PROXY_SOCKS4:
			purple_proxy_info_set_type(pi, PURPLE_PROXY_SOCKS4);
			break;
		case PROXY_SOCKS5:
			purple_proxy_info_set_type(pi, PURPLE_PROXY_SOCKS5);
			break;
		case PROXY_HTTP:
			purple_proxy_info_set_type(pi, PURPLE_PROXY_HTTP);
			break;
		}
		purple_proxy_info_set_host(pi, proxyhost);
		purple_proxy_info_set_port(pi, proxyport);
		purple_proxy_info_set_username(pi, proxyuser);
		purple_proxy_info_set_password(pi, proxypass);
	}

	purple_set_blist(purple_blist_new());

	/* No, really. So far there were ui_ops for everything, but now suddenly
	   one needs to use signals for typing notification stuff. :-( */
	purple_signal_connect(purple_conversations_get_handle(), "buddy-typing",
	                      &funcs, PURPLE_CALLBACK(prplcb_buddy_typing), NULL);
	purple_signal_connect(purple_conversations_get_handle(), "buddy-typed",
	                      &funcs, PURPLE_CALLBACK(prplcb_buddy_typing), NULL);
	purple_signal_connect(purple_conversations_get_handle(), "buddy-typing-stopped",
	                      &funcs, PURPLE_CALLBACK(prplcb_buddy_typing), NULL);

	memset(&funcs, 0, sizeof(funcs));
	funcs.login = purple_login;
	funcs.init = purple_init;
	funcs.logout = purple_logout;
	funcs.buddy_msg = purple_buddy_msg;
	funcs.away_states = purple_away_states;
	funcs.set_away = purple_set_away;
	funcs.add_buddy = purple_add_buddy;
	funcs.remove_buddy = purple_remove_buddy;
	funcs.add_permit = purple_add_permit;
	funcs.add_deny = purple_add_deny;
	funcs.rem_permit = purple_rem_permit;
	funcs.rem_deny = purple_rem_deny;
	funcs.get_info = purple_get_info;
	funcs.keepalive = purple_keepalive;
	funcs.send_typing = purple_send_typing;
	funcs.handle_cmp = g_strcasecmp;
	/* TODO(wilmer): Set these only for protocols that support them? */
	funcs.chat_msg = purple_chat_msg;
	funcs.chat_with = purple_chat_with;
	funcs.chat_invite = purple_chat_invite;
	funcs.chat_kick = purple_chat_kick;
	funcs.chat_leave = purple_chat_leave;
	funcs.chat_join = purple_chat_join;
	funcs.transfer_request = purple_transfer_request;

	help = g_string_new("BitlBee libpurple module supports the following IM protocols:\n");

	/* Add a protocol entry to BitlBee's structures for every protocol
	   supported by this libpurple instance. */
	for (prots = purple_plugins_get_protocols(); prots; prots = prots->next) {
		PurplePlugin *prot = prots->data;
		struct prpl *ret;

		/* If we already have this one (as a native module), don't
		   add a libpurple duplicate. */
		if (find_protocol(prot->info->id)) {
			continue;
		}

		ret = g_memdup(&funcs, sizeof(funcs));
		ret->name = ret->data = prot->info->id;
		if (strncmp(ret->name, "prpl-", 5) == 0) {
			ret->name += 5;
		}
		register_protocol(ret);

		g_string_append_printf(help, "\n* %s (%s)", ret->name, prot->info->name);

		/* libpurple doesn't define a protocol called OSCAR, but we
		   need it to be compatible with normal BitlBee. */
		if (g_strcasecmp(prot->info->id, "prpl-aim") == 0) {
			ret = g_memdup(&funcs, sizeof(funcs));
			ret->name = "oscar";
			ret->data = prot->info->id;
			register_protocol(ret);
		}
	}

	g_string_append(help, "\n\nFor used protocols, more information about available "
	                "settings can be found using \x02help purple <protocol name>\x02 "
	                "(create an account using that protocol first!)");

	/* Add a simple dynamically-generated help item listing all
	   the supported protocols. */
	help_add_mem(&global.help, "purple", help->str);
	g_string_free(help, TRUE);
}
