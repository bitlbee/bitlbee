/********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2012 Wilmer van der Gaast and others                *
  \********************************************************************/

/* Storage backend that uses an XMLish format for all data. */

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
#include "lib/base64.h"
#include "lib/arc.h"
#include "lib/md5.h"
#include "lib/xmltree.h"

#include <glib/gstdio.h>

typedef enum {
	XML_PASS_CHECK_ONLY = -1,
	XML_PASS_UNKNOWN = 0,
	XML_PASS_WRONG,
	XML_PASS_OK
} xml_pass_st;

/* To make it easier later when extending the format: */
#define XML_FORMAT_VERSION "1"

struct xml_parsedata {
	irc_t *irc;
	char given_nick[MAX_NICK_LENGTH + 1];
	char *given_pass;
};

static void xml_init(void)
{
	if (g_access(global.conf->configdir, F_OK) != 0) {
		log_message(LOGLVL_WARNING,
		            "The configuration directory `%s' does not exist. Configuration won't be saved.",
		            global.conf->configdir);
	} else if (g_access(global.conf->configdir, F_OK) != 0 ||
	           g_access(global.conf->configdir, W_OK) != 0) {
		log_message(LOGLVL_WARNING, "Permission problem: Can't read/write from/to `%s'.",
		            global.conf->configdir);
	}
}

static void handle_settings(struct xt_node *node, set_t **head)
{
	struct xt_node *c;

	for (c = node->children; (c = xt_find_node(c, "setting")); c = c->next) {
		char *name = xt_find_attr(c, "name");

		if (!name) {
			continue;
		}

		if (strcmp(node->name, "account") == 0) {
			set_t *s = set_find(head, name);
			if (s && (s->flags & ACC_SET_ONLINE_ONLY)) {
				continue; /* U can't touch this! */
			}
		}
		set_setstr(head, name, c->text);
	}
}

static xt_status handle_account(struct xt_node *node, gpointer data)
{
	struct xml_parsedata *xd = data;
	char *protocol, *handle, *server, *password = NULL, *autoconnect, *tag;
	char *pass_b64 = NULL;
	unsigned char *pass_cr = NULL;
	int pass_len, local = 0;
	struct prpl *prpl = NULL;
	account_t *acc;
	struct xt_node *c;

	handle = xt_find_attr(node, "handle");
	pass_b64 = xt_find_attr(node, "password");
	server = xt_find_attr(node, "server");
	autoconnect = xt_find_attr(node, "autoconnect");
	tag = xt_find_attr(node, "tag");

	protocol = xt_find_attr(node, "protocol");
	if (protocol) {
		prpl = find_protocol(protocol);
		local = protocol_account_islocal(protocol);
	}

	if (!handle || !pass_b64 || !protocol || !prpl) {
		return XT_ABORT;
	} else if ((pass_len = base64_decode(pass_b64, (unsigned char **) &pass_cr)) &&
	           arc_decode(pass_cr, pass_len, &password, xd->given_pass) >= 0) {
		acc = account_add(xd->irc->b, prpl, handle, password);
		if (server) {
			set_setstr(&acc->set, "server", server);
		}
		if (autoconnect) {
			set_setstr(&acc->set, "auto_connect", autoconnect);
		}
		if (tag) {
			set_setstr(&acc->set, "tag", tag);
		}
		if (local) {
			acc->flags |= ACC_FLAG_LOCAL;
		}
	} else {
		return XT_ABORT;
	}

	g_free(pass_cr);
	g_free(password);

	handle_settings(node, &acc->set);

	for (c = node->children; (c = xt_find_node(c, "buddy")); c = c->next) {
		char *handle, *nick;

		handle = xt_find_attr(c, "handle");
		nick = xt_find_attr(c, "nick");

		if (handle && nick) {
			nick_set_raw(acc, handle, nick);
		} else {
			return XT_ABORT;
		}
	}
	return XT_HANDLED;
}

static xt_status handle_channel(struct xt_node *node, gpointer data)
{
	struct xml_parsedata *xd = data;
	irc_channel_t *ic;
	char *name, *type;

	name = xt_find_attr(node, "name");
	type = xt_find_attr(node, "type");

	if (!name || !type) {
		return XT_ABORT;
	}

	/* The channel may exist already, for example if it's &bitlbee.
	   Also, it's possible that the user just reconnected and the
	   IRC client already rejoined all channels it was in. They
	   should still get the right settings. */
	if ((ic = irc_channel_by_name(xd->irc, name)) ||
	    (ic = irc_channel_new(xd->irc, name))) {
		set_setstr(&ic->set, "type", type);
	}

	handle_settings(node, &ic->set);

	return XT_HANDLED;
}

static const struct xt_handler_entry handlers[] = {
	{ "account", "user", handle_account, },
	{ "channel", "user", handle_channel, },
	{ NULL,      NULL,   NULL, },
};

static storage_status_t xml_load_real(irc_t *irc, const char *my_nick, const char *password, xml_pass_st action)
{
	struct xml_parsedata xd[1];
	char *fn, buf[2048];
	int fd, st;
	struct xt_parser *xp = NULL;
	struct xt_node *node;
	storage_status_t ret = STORAGE_OTHER_ERROR;

	xd->irc = irc;
	strncpy(xd->given_nick, my_nick, MAX_NICK_LENGTH);
	xd->given_nick[MAX_NICK_LENGTH] = '\0';
	nick_lc(NULL, xd->given_nick);
	xd->given_pass = (char *) password;

	fn = g_strconcat(global.conf->configdir, xd->given_nick, ".xml", NULL);
	if ((fd = open(fn, O_RDONLY)) < 0) {
		ret = STORAGE_NO_SUCH_USER;
		goto error;
	}

	xp = xt_new(handlers, xd);
	while ((st = read(fd, buf, sizeof(buf))) > 0) {
		st = xt_feed(xp, buf, st);
		if (st != 1) {
			break;
		}
	}
	close(fd);
	if (st != 0) {
		goto error;
	}

	node = xp->root;
	if (node == NULL || node->next != NULL || strcmp(node->name, "user") != 0) {
		goto error;
	}

	{
		char *nick = xt_find_attr(node, "nick");
		char *pass = xt_find_attr(node, "password");

		if (!nick || !pass) {
			goto error;
		} else if ((st = md5_verify_password(xd->given_pass, pass)) != 0) {
			ret = STORAGE_INVALID_PASSWORD;
			goto error;
		}
	}

	if (action == XML_PASS_CHECK_ONLY) {
		ret = STORAGE_OK;
		goto error;
	}

	/* DO NOT call xt_handle() before verifying the password! */
	if (xt_handle(xp, NULL, 1) == XT_HANDLED) {
		ret = STORAGE_OK;
	}

	handle_settings(node, &xd->irc->b->set);

error:
	xt_free(xp);
	g_free(fn);
	return ret;
}

static storage_status_t xml_load(irc_t *irc, const char *password)
{
	return xml_load_real(irc, irc->user->nick, password, XML_PASS_UNKNOWN);
}

static storage_status_t xml_check_pass(const char *my_nick, const char *password)
{
	return xml_load_real(NULL, my_nick, password, XML_PASS_CHECK_ONLY);
}


static gboolean xml_generate_nick(gpointer key, gpointer value, gpointer data);
static void xml_generate_settings(struct xt_node *cur, set_t **head);

struct xt_node *xml_generate(irc_t *irc)
{
	char *pass_buf = NULL;
	account_t *acc;
	md5_byte_t pass_md5[21];
	md5_state_t md5_state;
	GSList *l;
	struct xt_node *root, *cur;

	/* Generate a salted md5sum of the password. Use 5 bytes for the salt
	   (to prevent dictionary lookups of passwords) to end up with a 21-
	   byte password hash, more convenient for base64 encoding. */
	random_bytes(pass_md5 + 16, 5);
	md5_init(&md5_state);
	md5_append(&md5_state, (md5_byte_t *) irc->password, strlen(irc->password));
	md5_append(&md5_state, pass_md5 + 16, 5);   /* Add the salt. */
	md5_finish(&md5_state, pass_md5);
	/* Save the hash in base64-encoded form. */
	pass_buf = base64_encode(pass_md5, 21);

	root = cur = xt_new_node("user", NULL, NULL);
	xt_add_attr(cur, "nick", irc->user->nick);
	xt_add_attr(cur, "password", pass_buf);
	xt_add_attr(cur, "version", XML_FORMAT_VERSION);

	g_free(pass_buf);

	xml_generate_settings(cur, &irc->b->set);

	for (acc = irc->b->accounts; acc; acc = acc->next) {
		unsigned char *pass_cr;
		char *pass_b64;
		int pass_len;

		pass_len = arc_encode(acc->pass, strlen(acc->pass), (unsigned char **) &pass_cr, irc->password, 12);
		pass_b64 = base64_encode(pass_cr, pass_len);
		g_free(pass_cr);

		cur = xt_new_node("account", NULL, NULL);
		xt_add_attr(cur, "protocol", acc->prpl->name);
		xt_add_attr(cur, "handle", acc->user);
		xt_add_attr(cur, "password", pass_b64);
		xt_add_attr(cur, "autoconnect", acc->auto_connect ? "true" : "false");
		xt_add_attr(cur, "tag", acc->tag);
		if (acc->server && acc->server[0]) {
			xt_add_attr(cur, "server", acc->server);
		}

		g_free(pass_b64);

		/* This probably looks pretty strange. g_hash_table_foreach
		   is quite a PITA already (but it can't get much better in
		   C without using #define, I'm afraid), and it
		   doesn't seem to be possible to abort the foreach on write
		   errors, so instead let's use the _find function and
		   return TRUE on write errors. Which means, if we found
		   something, there was an error. :-) */
		g_hash_table_find(acc->nicks, xml_generate_nick, cur);

		xml_generate_settings(cur, &acc->set);

		xt_add_child(root, cur);
	}

	for (l = irc->channels; l; l = l->next) {
		irc_channel_t *ic = l->data;

		if (ic->flags & IRC_CHANNEL_TEMP) {
			continue;
		}

		cur = xt_new_node("channel", NULL, NULL);
		xt_add_attr(cur, "name", ic->name);
		xt_add_attr(cur, "type", set_getstr(&ic->set, "type"));

		xml_generate_settings(cur, &ic->set);

		xt_add_child(root, cur);
	}

	return root;
}

static gboolean xml_generate_nick(gpointer key, gpointer value, gpointer data)
{
	struct xt_node *node = xt_new_node("buddy", NULL, NULL);

	xt_add_attr(node, "handle", key);
	xt_add_attr(node, "nick", value);
	xt_add_child((struct xt_node *) data, node);

	return FALSE;
}

static void xml_generate_settings(struct xt_node *cur, set_t **head)
{
	set_t *set;

	for (set = *head; set; set = set->next) {
		if (set->value && !(set->flags & SET_NOSAVE)) {
			struct xt_node *xset;
			xt_add_child(cur, xset = xt_new_node("setting", set->value, NULL));
			xt_add_attr(xset, "name", set->key);
		}
	}
}

static storage_status_t xml_save(irc_t *irc, int overwrite)
{
	storage_status_t ret = STORAGE_OK;
	char path[512], *path2 = NULL, *xml = NULL;
	struct xt_node *tree = NULL;
	size_t len;
	int fd;

	path2 = g_strdup(irc->user->nick);
	nick_lc(NULL, path2);
	g_snprintf(path, sizeof(path) - 20, "%s%s%s", global.conf->configdir, path2, ".xml");
	g_free(path2);

	if (!overwrite && g_access(path, F_OK) == 0) {
		return STORAGE_ALREADY_EXISTS;
	}

	strcat(path, ".XXXXXX");
	if ((fd = mkstemp(path)) < 0) {
		irc_rootmsg(irc, "Error while opening configuration file.");
		return STORAGE_OTHER_ERROR;
	}

	tree = xml_generate(irc);
	xml = xt_to_string_i(tree);
	len = strlen(xml);
	if (write(fd, xml, len) != len ||
	    fsync(fd) != 0 ||   /* #559 */
	    close(fd) != 0) {
		goto error;
	}

	path2 = g_strndup(path, strlen(path) - 7);
	if (rename(path, path2) != 0) {
		g_free(path2);
		goto error;
	}
	g_free(path2);

	goto finish;

error:
	irc_rootmsg(irc, "Write error. Disk full?");
	ret = STORAGE_OTHER_ERROR;

finish:
	close(fd);
	unlink(path);
	g_free(xml);
	xt_free_node(tree);

	return ret;
}


static storage_status_t xml_remove(const char *nick, const char *password)
{
	char s[512], *lc;
	storage_status_t status;

	status = xml_check_pass(nick, password);
	if (status != STORAGE_OK) {
		return status;
	}

	lc = g_strdup(nick);
	nick_lc(NULL, lc);
	g_snprintf(s, 511, "%s%s%s", global.conf->configdir, lc, ".xml");
	g_free(lc);

	if (unlink(s) == -1) {
		return STORAGE_OTHER_ERROR;
	}

	return STORAGE_OK;
}

storage_t storage_xml = {
	.name = "xml",
	.init = xml_init,
	.check_pass = xml_check_pass,
	.remove = xml_remove,
	.load = xml_load,
	.save = xml_save
};
