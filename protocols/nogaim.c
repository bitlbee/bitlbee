/********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2012 Wilmer van der Gaast and others                *
  \********************************************************************/

/*
 * nogaim
 *
 * Gaim without gaim - for BitlBee
 *
 * This file contains functions called by the Gaim IM-modules. It's written
 * from scratch for BitlBee and doesn't contain any code from Gaim anymore
 * (except for the function names).
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
  if not, write to the Free Software Foundation, Inc., 51 Franklin St.,
  Fifth Floor, Boston, MA  02110-1301  USA
*/

#define BITLBEE_CORE
#include <ctype.h>

#include "nogaim.h"

GSList *connections;

#ifdef WITH_PLUGINS
GList *plugins = NULL;

static gint pluginscmp(gconstpointer a, gconstpointer b, gpointer data)
{
	const struct plugin_info *ia = a;
	const struct plugin_info *ib = b;

	return g_strcasecmp(ia->name, ib->name);
}

/* semi-private */
gboolean plugin_info_validate(struct plugin_info *info, const char *path)
{
	GList *l;
	gboolean loaded = FALSE;

	if (!path) {
		path = "(null)";
	}

	if (info->abiver != BITLBEE_ABI_VERSION_CODE) {
		log_message(LOGLVL_ERROR,
			    "`%s' uses ABI %u but %u is required\n",
			    path, info->abiver,
			    BITLBEE_ABI_VERSION_CODE);
		return FALSE;
	}

	if (!info->name || !info->version) {
		log_message(LOGLVL_ERROR,
			    "Name or version missing from the "
			    "plugin info in `%s'\n", path);
		return FALSE;
	}

	for (l = plugins; l; l = l->next) {
		struct plugin_info *i = l->data;

		if (g_strcasecmp(i->name, info->name) == 0) {
			loaded = TRUE;
			break;
		}
	}

	if (loaded) {
		log_message(LOGLVL_WARNING,
			    "%s plugin already loaded\n",
			    info->name);
		return FALSE;
	}

	return TRUE;
}

/* semi-private */
gboolean plugin_info_add(struct plugin_info *info)
{
	plugins = g_list_insert_sorted_with_data(plugins, info, pluginscmp, NULL);
	return TRUE;
}

gboolean load_plugin(char *path)
{
	struct plugin_info *info = NULL;
	struct plugin_info * (*info_function) (void) = NULL;
	void (*init_function) (void);

	GModule *mod = g_module_open(path, G_MODULE_BIND_LAZY);

	if (!mod) {
		log_message(LOGLVL_ERROR, "Error loading plugin `%s': %s\n", path, g_module_error());
		return FALSE;
	}

	if (g_module_symbol(mod, "init_plugin_info", (gpointer *) &info_function)) {
		info = info_function();

		if (!plugin_info_validate(info, path)) {
			g_module_close(mod);
			return FALSE;
		}
	} else {
		log_message(LOGLVL_WARNING, "Can't find function `init_plugin_info' in `%s'\n", path);
	}

	if (!g_module_symbol(mod, "init_plugin", (gpointer *) &init_function)) {
		log_message(LOGLVL_WARNING, "Can't find function `init_plugin' in `%s'\n", path);
		g_module_close(mod);
		return FALSE;
	}

	if (info_function) {
		plugin_info_add(info);
	}

	init_function();
	return TRUE;
}

void load_plugins(void)
{
	GDir *dir;
	GError *error = NULL;

	dir = g_dir_open(global.conf->plugindir, 0, &error);

	if (dir) {
		const gchar *entry;
		char *path;

		while ((entry = g_dir_read_name(dir))) {
			if (!g_str_has_suffix(entry, "." G_MODULE_SUFFIX)) {
				continue;
			}

			path = g_build_filename(global.conf->plugindir, entry, NULL);
			if (!path) {
				log_message(LOGLVL_WARNING, "Can't build path for %s\n", entry);
				continue;
			}

			load_plugin(path);

			g_free(path);
		}

		g_dir_close(dir);
	}
}

GList *get_plugins()
{
	return plugins;
}
#endif

GList *protocols = NULL;
GList *disabled_protocols = NULL;

void register_protocol(struct prpl *p)
{
	int i;
	gboolean refused = global.conf->protocols != NULL;

	for (i = 0; global.conf->protocols && global.conf->protocols[i]; i++) {
		if (g_strcasecmp(p->name, global.conf->protocols[i]) == 0) {
			refused = FALSE;
		}
	}

	if (refused) {
		disabled_protocols = g_list_append(disabled_protocols, p);
	} else {
		protocols = g_list_append(protocols, p);
	}
}

static int proto_name_cmp(const void *proto_, const void *name)
{
	const struct prpl *proto = proto_;
	return g_strcasecmp(proto->name, name);
}

struct prpl *find_protocol(const char *name)
{
	GList *gl = g_list_find_custom(protocols, name, proto_name_cmp);
	return gl ? gl->data: NULL;
}

gboolean is_protocol_disabled(const char *name)
{
	return g_list_find_custom(disabled_protocols, name, proto_name_cmp) != NULL;
}

/* Returns heap allocated string with text attempting to explain why a protocol is missing 
 * Free the return value with g_free() */
char *explain_unknown_protocol(const char *name)
{
	char *extramsg = NULL;

	if (is_protocol_disabled(name)) {
		return g_strdup("Protocol disabled in the global config (bitlbee.conf)");
	}

	if (strcmp(name, "yahoo") == 0) {
		return g_strdup("The old yahoo protocol is gone, try the funyahoo++ libpurple plugin instead.");
	}

#ifdef WITH_PURPLE
	if ((strcmp(name, "msn") == 0) ||
	    (strcmp(name, "loubserp-mxit") == 0) ||
	    (strcmp(name, "myspace") == 0)) {
		return g_strdup("This protocol has been removed from your libpurple version.");
	}

	if (strcmp(name, "hipchat") == 0) {
		return g_strdup("This account type isn't supported by libpurple's jabber.");
	}

#else
	extramsg = "If this is a libpurple plugin, you might need to install bitlbee-libpurple instead.";
#endif
	return g_strconcat("The protocol plugin is not installed or could not be loaded. "
	                   "Use the `plugins' command to list available protocols. ",
	                   extramsg, NULL);
}

void nogaim_init()
{
	extern void jabber_initmodule();
	extern void twitter_initmodule();
	extern void purple_initmodule();

#ifdef WITH_JABBER
	jabber_initmodule();
#endif

#ifdef WITH_TWITTER
	twitter_initmodule();
#endif

#ifdef WITH_PURPLE
	purple_initmodule();
#endif

#ifdef WITH_PLUGINS
	load_plugins();
#endif
}

GList *get_protocols()
{
	return protocols;
}

GList *get_protocols_disabled()
{
	return disabled_protocols;
}

GSList *get_connections()
{
	return connections;
}

struct im_connection *imcb_new(account_t *acc)
{
	struct im_connection *ic;
	GHashFunc fn_hash = NULL;
	GEqualFunc fn_equal = NULL;

	ic = g_new0(struct im_connection, 1);

	ic->bee = acc->bee;
	ic->acc = acc;
	acc->ic = ic;

	/* figure out if we have hashing functions compatible with handle_cmp */
	if (acc->prpl->handle_cmp == g_ascii_strcasecmp) {
		fn_hash = b_istr_hash;
		fn_equal = b_istr_equal;
	} else if (acc->prpl->handle_cmp == g_strcmp0 || acc->prpl->handle_cmp == strcmp) {
		fn_hash = g_str_hash;
		fn_equal = g_str_equal;
	}

	/* only create the hash table if we found them */
	if (fn_hash && fn_equal) {
		ic->bee_users = g_hash_table_new_full(fn_hash, fn_equal, NULL, NULL);
	}

	connections = g_slist_append(connections, ic);

	return(ic);
}

void imc_free(struct im_connection *ic)
{
	account_t *a;

	/* Destroy the pointer to this connection from the account list */
	for (a = ic->bee->accounts; a; a = a->next) {
		if (a->ic == ic) {
			a->ic = NULL;
			break;
		}
	}

	if (ic->bee_users) {
		g_hash_table_destroy(ic->bee_users);
	}

	connections = g_slist_remove(connections, ic);
	g_free(ic);
}

static void serv_got_crap(struct im_connection *ic, char *format, ...)
{
	va_list params;
	char *text;
	account_t *a;

	if (!ic->bee->ui->log) {
		return;
	}

	va_start(params, format);
	text = g_strdup_vprintf(format, params);
	va_end(params);

	if ((g_strcasecmp(set_getstr(&ic->bee->set, "strip_html"), "always") == 0) ||
	    ((ic->flags & OPT_DOES_HTML) && set_getbool(&ic->bee->set, "strip_html"))) {
		strip_html(text);
	}

	/* Try to find a different connection on the same protocol. */
	for (a = ic->bee->accounts; a; a = a->next) {
		if (a->prpl == ic->acc->prpl && a->ic != ic) {
			break;
		}
	}

	/* If we found one, include the screenname in the message. */
	if (a) {
		ic->bee->ui->log(ic->bee, ic->acc->tag, text);
	} else {
		ic->bee->ui->log(ic->bee, ic->acc->prpl->name, text);
	}

	g_free(text);
}

void imcb_log(struct im_connection *ic, char *format, ...)
{
	va_list params;
	char *text;

	va_start(params, format);
	text = g_strdup_vprintf(format, params);
	va_end(params);

	if (ic->flags & OPT_LOGGED_IN) {
		serv_got_crap(ic, "%s", text);
	} else {
		serv_got_crap(ic, "Logging in: %s", text);
	}

	g_free(text);
}

void imcb_error(struct im_connection *ic, char *format, ...)
{
	va_list params;
	char *text;

	va_start(params, format);
	text = g_strdup_vprintf(format, params);
	va_end(params);

	if (ic->flags & OPT_LOGGED_IN) {
		serv_got_crap(ic, "Error: %s", text);
	} else {
		serv_got_crap(ic, "Login error: %s", text);
	}

	g_free(text);
}

static gboolean send_keepalive(gpointer d, gint fd, b_input_condition cond)
{
	struct im_connection *ic = d;

	if ((ic->flags & OPT_PONGS) && !(ic->flags & OPT_PONGED)) {
		/* This protocol is expected to ack keepalives and hasn't
		   since the last time we were here. */
		imcb_error(ic, "Connection timeout");
		imc_logout(ic, TRUE);
		return FALSE;
	}
	ic->flags &= ~OPT_PONGED;

	if (ic->acc->prpl->keepalive) {
		ic->acc->prpl->keepalive(ic);
	}

	return TRUE;
}

void start_keepalives(struct im_connection *ic, int interval)
{
	b_event_remove(ic->keepalive);
	ic->keepalive = b_timeout_add(interval, send_keepalive, ic);

	/* Connecting successfully counts as a first successful pong. */
	if (ic->flags & OPT_PONGS) {
		ic->flags |= OPT_PONGED;
	}
}

void imcb_connected(struct im_connection *ic)
{
	/* MSN servers sometimes redirect you to a different server and do
	   the whole login sequence again, so these "late" calls to this
	   function should be handled correctly. (IOW, ignored) */
	if (ic->flags & OPT_LOGGED_IN) {
		return;
	}

	if (ic->acc->flags & ACC_FLAG_LOCAL) {
		GHashTableIter nicks;
		gpointer k, v;
		g_hash_table_iter_init(&nicks, ic->acc->nicks);
		while (g_hash_table_iter_next(&nicks, &k, &v)) {
			ic->acc->prpl->add_buddy(ic, (char *) k, NULL);
		}
	}

	imcb_log(ic, "Logged in");

	ic->flags |= OPT_LOGGED_IN;
	start_keepalives(ic, 60000);

	/* Necessary to send initial presence status, even if we're not away. */
	imc_away_send_update(ic);

	/* Apparently we're connected successfully, so reset the
	   exponential backoff timer. */
	ic->acc->auto_reconnect_delay = 0;

	if (ic->bee->ui->imc_connected) {
		ic->bee->ui->imc_connected(ic);
	}
}

gboolean auto_reconnect(gpointer data, gint fd, b_input_condition cond)
{
	account_t *a = data;

	a->reconnect = 0;
	account_on(a->bee, a);

	return(FALSE);          /* Only have to run the timeout once */
}

void cancel_auto_reconnect(account_t *a)
{
	b_event_remove(a->reconnect);
	a->reconnect = 0;
}

void imc_logout(struct im_connection *ic, int allow_reconnect)
{
	bee_t *bee = ic->bee;
	account_t *a;
	GSList *l;
	int delay;

	/* Nested calls might happen sometimes, this is probably the best
	   place to catch them. */
	if (ic->flags & OPT_LOGGING_OUT) {
		return;
	} else {
		ic->flags |= OPT_LOGGING_OUT;
	}

	if (ic->bee->ui->imc_disconnected) {
		ic->bee->ui->imc_disconnected(ic);
	}

	imcb_log(ic, "Signing off..");

	/* TBH I don't remember anymore why I didn't just use ic->acc... */
	for (a = bee->accounts; a; a = a->next) {
		if (a->ic == ic) {
			break;
		}
	}

	if (a && !allow_reconnect && !(ic->flags & OPT_LOGGED_IN) &&
	    set_getbool(&a->set, "oauth")) {
		/* If this account supports OAuth, we're not logged in yet and
		   not allowed to retry, assume there were auth issues. Give a
		   helpful message on what might be necessary to fix this. */
		imcb_log(ic, "If you're having problems logging in, try re-requesting "
		         "an OAuth token: account %s set password \"\"", a->tag);
	}

	for (l = bee->users; l; ) {
		bee_user_t *bu = l->data;
		GSList *next = l->next;

		if (bu->ic == ic) {
			bee_user_free(bee, bu);
		}

		l = next;
	}

	b_event_remove(ic->keepalive);
	ic->keepalive = 0;
	ic->acc->prpl->logout(ic);
	b_event_remove(ic->inpa);

	g_free(ic->away);
	ic->away = NULL;

	query_del_by_conn((irc_t *) ic->bee->ui_data, ic);

	if (!a) {
		/* Uhm... This is very sick. */
	} else if (allow_reconnect && set_getbool(&bee->set, "auto_reconnect") &&
	           set_getbool(&a->set, "auto_reconnect") &&
	           (delay = account_reconnect_delay(a)) > 0) {
		imcb_log(ic, "Reconnecting in %d seconds..", delay);
		a->reconnect = b_timeout_add(delay * 1000, auto_reconnect, a);
	}

	imc_free(ic);
}

void imcb_ask(struct im_connection *ic, char *msg, void *data,
              query_callback doit, query_callback dont)
{
	query_add((irc_t *) ic->bee->ui_data, ic, msg, doit, dont, g_free, data);
}

void imcb_ask_with_free(struct im_connection *ic, char *msg, void *data,
                        query_callback doit, query_callback dont, query_callback myfree)
{
	query_add((irc_t *) ic->bee->ui_data, ic, msg, doit, dont, myfree, data);
}

void imcb_add_buddy(struct im_connection *ic, const char *handle, const char *group)
{
	bee_user_t *bu;
	bee_t *bee = ic->bee;
	bee_group_t *oldg;

	if (!(bu = bee_user_by_handle(bee, ic, handle))) {
		bu = bee_user_new(bee, ic, handle, 0);
	}

	oldg = bu->group;
	bu->group = bee_group_by_name(bee, group, TRUE);

	if (bee->ui->user_group && bu->group != oldg) {
		bee->ui->user_group(bee, bu);
	}
}

void imcb_rename_buddy(struct im_connection *ic, const char *handle, const char *fullname)
{
	bee_t *bee = ic->bee;
	bee_user_t *bu = bee_user_by_handle(bee, ic, handle);

	if (!bu || !fullname) {
		return;
	}

	if (!bu->fullname || strcmp(bu->fullname, fullname) != 0) {
		g_free(bu->fullname);
		bu->fullname = g_strdup(fullname);

		if (bee->ui->user_fullname) {
			bee->ui->user_fullname(bee, bu);
		}
	}
}

void imcb_remove_buddy(struct im_connection *ic, const char *handle, char *group)
{
	bee_user_free(ic->bee, bee_user_by_handle(ic->bee, ic, handle));
}

/* Implements either imcb_buddy_nick_hint() or imcb_buddy_nick_change() depending on the value of 'change' */
static void buddy_nick_hint_or_change(struct im_connection *ic, const char *handle, const char *nick, gboolean change)
{
	bee_t *bee = ic->bee;
	bee_user_t *bu = bee_user_by_handle(bee, ic, handle);

	if (!bu || !nick) {
		return;
	}

	g_free(bu->nick);
	bu->nick = g_strdup(nick);

	if (change && bee->ui->user_nick_change) {
		bee->ui->user_nick_change(bee, bu, nick);
	} else if (!change && bee->ui->user_nick_hint) {
		bee->ui->user_nick_hint(bee, bu, nick);
	}
}

/* Soft variant, for newly created users. Does nothing if it's already online */
void imcb_buddy_nick_hint(struct im_connection *ic, const char *handle, const char *nick)
{
	buddy_nick_hint_or_change(ic, handle, nick, FALSE);
}

/* Hard variant, always changes the nick */
void imcb_buddy_nick_change(struct im_connection *ic, const char *handle, const char *nick)
{
	buddy_nick_hint_or_change(ic, handle, nick, TRUE);
}

struct imcb_ask_cb_data {
	struct im_connection *ic;
	char *handle;
};

static void imcb_ask_cb_free(void *data)
{
	struct imcb_ask_cb_data *cbd = data;

	g_free(cbd->handle);
	g_free(cbd);
}

static void imcb_ask_auth_cb_no(void *data)
{
	struct imcb_ask_cb_data *cbd = data;

	cbd->ic->acc->prpl->auth_deny(cbd->ic, cbd->handle);

	imcb_ask_cb_free(cbd);
}

static void imcb_ask_auth_cb_yes(void *data)
{
	struct imcb_ask_cb_data *cbd = data;

	cbd->ic->acc->prpl->auth_allow(cbd->ic, cbd->handle);

	imcb_ask_cb_free(cbd);
}

void imcb_ask_auth(struct im_connection *ic, const char *handle, const char *realname)
{
	struct imcb_ask_cb_data *data = g_new0(struct imcb_ask_cb_data, 1);
	char *s, *realname_ = NULL;

	if (realname != NULL) {
		realname_ = g_strdup_printf(" (%s)", realname);
	}

	s = g_strdup_printf("The user %s%s wants to add you to his/her buddy list.",
	                    handle, realname_ ? realname_ : "");

	g_free(realname_);

	data->ic = ic;
	data->handle = g_strdup(handle);
	query_add((irc_t *) ic->bee->ui_data, ic, s,
	          imcb_ask_auth_cb_yes, imcb_ask_auth_cb_no, imcb_ask_cb_free, data);

	g_free(s);
}

static void imcb_ask_add_cb_yes(void *data)
{
	struct imcb_ask_cb_data *cbd = data;

	cbd->ic->acc->prpl->add_buddy(cbd->ic, cbd->handle, NULL);

	imcb_ask_cb_free(data);
}

void imcb_ask_add(struct im_connection *ic, const char *handle, const char *realname)
{
	struct imcb_ask_cb_data *data;
	char *s;

	/* TODO: Make a setting for this! */
	if (bee_user_by_handle(ic->bee, ic, handle) != NULL) {
		return;
	}

	data = g_new0(struct imcb_ask_cb_data, 1);

	s = g_strdup_printf("The user %s is not in your buddy list yet. Do you want to add him/her now?", handle);

	data->ic = ic;
	data->handle = g_strdup(handle);
	query_add((irc_t *) ic->bee->ui_data, ic, s,
	          imcb_ask_add_cb_yes, imcb_ask_cb_free, imcb_ask_cb_free, data);

	g_free(s);
}

struct bee_user *imcb_buddy_by_handle(struct im_connection *ic, const char *handle)
{
	return bee_user_by_handle(ic->bee, ic, handle);
}

/* The plan is to not allow straight calls to prpl functions anymore, but do
   them all from some wrappers. We'll start to define some down here: */

int imc_chat_msg(struct groupchat *c, char *msg, int flags)
{
	char *buf = NULL;

	if ((c->ic->flags & OPT_DOES_HTML) && (g_strncasecmp(msg, "<html>", 6) != 0)) {
		buf = escape_html(msg);
		msg = buf;
	}

	c->ic->acc->prpl->chat_msg(c, msg, flags);
	g_free(buf);

	return 1;
}

static char *imc_away_state_find(GList *gcm, char *away, char **message);

int imc_away_send_update(struct im_connection *ic)
{
	char *away, *msg = NULL;

	if (ic->acc->prpl->away_states == NULL ||
	    ic->acc->prpl->set_away == NULL) {
		return 0;
	}

	away = set_getstr(&ic->acc->set, "away") ?
	       : set_getstr(&ic->bee->set, "away");
	if (away && *away) {
		GList *m = ic->acc->prpl->away_states(ic);
		if (m == NULL) {
			return 0;
		}
		msg = ic->acc->flags & ACC_FLAG_AWAY_MESSAGE ? away : NULL;
		away = imc_away_state_find(m, away, &msg) ? :
		       (imc_away_state_find(m, "away", NULL) ? : m->data);
	} else if (ic->acc->flags & ACC_FLAG_STATUS_MESSAGE) {
		away = NULL;
		msg = set_getstr(&ic->acc->set, "status") ?
		      : set_getstr(&ic->bee->set, "status");
	}

	ic->acc->prpl->set_away(ic, away, msg);

	return 1;
}

static char *imc_away_alias_list[8][5] =
{
	{ "Away from computer", "Away", "Extended away", NULL },
	{ "NA", "N/A", "Not available", NULL },
	{ "Busy", "Do not disturb", "DND", "Occupied", NULL },
	{ "Be right back", "BRB", NULL },
	{ "On the phone", "Phone", "On phone", NULL },
	{ "Out to lunch", "Lunch", "Food", NULL },
	{ "Invisible", "Hidden" },
	{ NULL }
};

static char *imc_away_state_find(GList *gcm, char *away, char **message)
{
	GList *m;
	int i, j;

	for (m = gcm; m; m = m->next) {
		if (g_strncasecmp(m->data, away, strlen(m->data)) == 0) {
			/* At least the Yahoo! module works better if message
			   contains no data unless it adds something to what
			   we have in state already. */
			if (message && strlen(m->data) == strlen(away)) {
				*message = NULL;
			}

			return m->data;
		}
	}

	for (i = 0; *imc_away_alias_list[i]; i++) {
		int keep_message;

		for (j = 0; imc_away_alias_list[i][j]; j++) {
			if (g_strncasecmp(away, imc_away_alias_list[i][j], strlen(imc_away_alias_list[i][j])) == 0) {
				keep_message = strlen(away) != strlen(imc_away_alias_list[i][j]);
				break;
			}
		}

		if (!imc_away_alias_list[i][j]) {       /* If we reach the end, this row */
			continue;                       /* is not what we want. Next!    */

		}
		/* Now find an entry in this row which exists in gcm */
		for (j = 0; imc_away_alias_list[i][j]; j++) {
			for (m = gcm; m; m = m->next) {
				if (g_strcasecmp(imc_away_alias_list[i][j], m->data) == 0) {
					if (!keep_message && message) {
						*message = NULL;
					}

					return m->data;
				}
			}
		}

		/* No need to look further, apparently this state doesn't
		   have any good alias for this protocol. */
		break;
	}

	return NULL;
}

void imc_add_allow(struct im_connection *ic, char *handle)
{
	if (g_slist_find_custom(ic->permit, handle, (GCompareFunc) ic->acc->prpl->handle_cmp) == NULL) {
		ic->permit = g_slist_prepend(ic->permit, g_strdup(handle));
	}

	ic->acc->prpl->add_permit(ic, handle);
}

void imc_rem_allow(struct im_connection *ic, char *handle)
{
	GSList *l;

	if ((l = g_slist_find_custom(ic->permit, handle, (GCompareFunc) ic->acc->prpl->handle_cmp))) {
		g_free(l->data);
		ic->permit = g_slist_delete_link(ic->permit, l);
	}

	ic->acc->prpl->rem_permit(ic, handle);
}

void imc_add_block(struct im_connection *ic, char *handle)
{
	if (g_slist_find_custom(ic->deny, handle, (GCompareFunc) ic->acc->prpl->handle_cmp) == NULL) {
		ic->deny = g_slist_prepend(ic->deny, g_strdup(handle));
	}

	ic->acc->prpl->add_deny(ic, handle);
}

void imc_rem_block(struct im_connection *ic, char *handle)
{
	GSList *l;

	if ((l = g_slist_find_custom(ic->deny, handle, (GCompareFunc) ic->acc->prpl->handle_cmp))) {
		g_free(l->data);
		ic->deny = g_slist_delete_link(ic->deny, l);
	}

	ic->acc->prpl->rem_deny(ic, handle);
}

/* Deprecated: using this function resulted in merging several handles accidentally
 * Also the irc layer handles this decently nowadays */
void imcb_clean_handle(struct im_connection *ic, char *handle)
{
}
