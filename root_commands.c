/********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2013 Wilmer van der Gaast and others                *
  \********************************************************************/

/* User manager (root) commands                                         */

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
#include "commands.h"
#include "bitlbee.h"
#include "help.h"
#include "ipc.h"

void root_command_string(irc_t *irc, char *command)
{
	root_command(irc, split_command_parts(command, 0));
}

#define MIN_ARGS(x, y ...)                                                    \
	do                                                                     \
	{                                                                      \
		int blaat;                                                     \
		for (blaat = 0; blaat <= x; blaat++) {                         \
			if (cmd[blaat] == NULL)                               \
			{                                                      \
				irc_rootmsg(irc, "Not enough parameters given (need %d).", x); \
				return y;                                      \
			} }                                                      \
	} while (0)

void root_command(irc_t *irc, char *cmd[])
{
	int i, len;

	if (!cmd[0]) {
		return;
	}

	len = strlen(cmd[0]);
	for (i = 0; root_commands[i].command; i++) {
		if (g_strncasecmp(root_commands[i].command, cmd[0], len) == 0) {
			if (root_commands[i + 1].command &&
			    g_strncasecmp(root_commands[i + 1].command, cmd[0], len) == 0) {
				/* Only match on the first letters if the match is unique. */
				break;
			}

			MIN_ARGS(root_commands[i].required_parameters);

			root_commands[i].execute(irc, cmd);
			return;
		}
	}

	irc_rootmsg(irc, "Unknown command: %s. Please use \x02help commands\x02 to get a list of available commands.",
	            cmd[0]);
}

static void cmd_help(irc_t *irc, char **cmd)
{
	char param[80];
	int i;
	char *s;

	memset(param, 0, sizeof(param));
	for (i = 1; (cmd[i] != NULL && (strlen(param) < (sizeof(param) - 1))); i++) {
		if (i != 1) {   // prepend space except for the first parameter
			strcat(param, " ");
		}
		strncat(param, cmd[i], sizeof(param) - strlen(param) - 1);
	}

	s = help_get(&(global.help), param);
	if (!s) {
		s = help_get(&(global.help), "");
	}

	if (s) {
		irc_rootmsg(irc, "%s", s);
		g_free(s);
	} else {
		irc_rootmsg(irc, "Error opening helpfile.");
	}
}

static void cmd_account(irc_t *irc, char **cmd);
static void bitlbee_whatsnew(irc_t *irc);

static void cmd_identify(irc_t *irc, char **cmd)
{
	storage_status_t status;
	gboolean load = TRUE;
	char *password = cmd[1];

	if (irc->status & USTATUS_IDENTIFIED) {
		irc_rootmsg(irc, "You're already logged in.");
		return;
	}

	if (cmd[1] == NULL) {
	} else if (strncmp(cmd[1], "-no", 3) == 0) {
		load = FALSE;
		password = cmd[2];
		if (password == NULL) {
			irc->status |= OPER_HACK_IDENTIFY_NOLOAD;
		}
	} else if (strncmp(cmd[1], "-force", 6) == 0) {
		password = cmd[2];
		if (password == NULL) {
			irc->status |= OPER_HACK_IDENTIFY_FORCE;
		}
	} else if (irc->b->accounts != NULL) {
		irc_rootmsg(irc,
		            "You're trying to identify yourself, but already have "
		            "at least one IM account set up. "
		            "Use \x02identify -noload\x02 or \x02identify -force\x02 "
		            "instead (see \x02help identify\x02).");
		return;
	}

	if (password == NULL) {
		irc_rootmsg(irc, "About to identify, use /OPER to enter the password");
		irc->status |= OPER_HACK_IDENTIFY;
		return;
	}

	status = auth_check_pass(irc, irc->user->nick, password);
	if (load && (status == STORAGE_OK)) {
		status = storage_load(irc, password);
	}

	switch (status) {
	case STORAGE_INVALID_PASSWORD:
		irc_rootmsg(irc, "Incorrect password");
		break;
	case STORAGE_NO_SUCH_USER:
		irc_rootmsg(irc, "The nick is (probably) not registered");
		break;
	case STORAGE_OK:
		irc_rootmsg(irc, "Password accepted%s",
		            load ? ", settings and accounts loaded" : "");
		irc->status |= USTATUS_IDENTIFIED;
		irc_umode_set(irc, "+R", 1);

		if (irc->caps & CAP_SASL) {
			irc_user_t *iu = irc->user;
			irc_send_num(irc, 900, "%s!%s@%s %s :You are now logged in as %s",
				iu->nick, iu->user, iu->host, iu->nick, iu->nick);
		}

		bitlbee_whatsnew(irc);

		/* The following code is a bit hairy now. With takeover
		   support, we shouldn't immediately auto_connect in case
		   we're going to offer taking over an existing session.
		   Do it in 200ms since that should give the parent process
		   enough time to come back to us. */
		if (load) {
			irc_channel_auto_joins(irc, NULL);
			if (!set_getbool(&irc->default_channel->set, "auto_join")) {
				irc_channel_del_user(irc->default_channel, irc->user,
				                     IRC_CDU_PART, "auto_join disabled "
				                     "for this channel.");
			}
			if (set_getbool(&irc->b->set, "auto_connect")) {
				irc->login_source_id = b_timeout_add(200,
				                                     cmd_identify_finish, irc);
			}
		}

		/* If ipc_child_identify() returns FALSE, it means we're
		   already sure that there's no takeover target (only
		   possible in 1-process daemon mode). Start auto_connect
		   immediately. */
		if (!ipc_child_identify(irc) && load) {
			cmd_identify_finish(irc, 0, 0);
		}

		break;
	case STORAGE_OTHER_ERROR:
	default:
		irc_rootmsg(irc, "Unknown error while loading configuration");
		break;
	}
}

gboolean cmd_identify_finish(gpointer data, gint fd, b_input_condition cond)
{
	char *account_on[] = { "account", "on", NULL };
	irc_t *irc = data;

	if (set_getbool(&irc->b->set, "auto_connect")) {
		cmd_account(irc, account_on);
	}

	b_event_remove(irc->login_source_id);
	irc->login_source_id = -1;
	return FALSE;
}

static void cmd_register(irc_t *irc, char **cmd)
{
	char s[16];

	if (global.conf->authmode == AUTHMODE_REGISTERED) {
		irc_rootmsg(irc, "This server does not allow registering new accounts");
		return;
	}

	if (cmd[1] == NULL) {
		irc_rootmsg(irc, "About to register, use /OPER to enter the password");
		irc->status |= OPER_HACK_REGISTER;
		return;
	}

	switch (storage_save(irc, cmd[1], FALSE)) {
	case STORAGE_ALREADY_EXISTS:
		irc_rootmsg(irc, "Nick is already registered");
		break;

	case STORAGE_OK:
		irc_rootmsg(irc, "Account successfully created");
		irc_setpass(irc, cmd[1]);
		irc->status |= USTATUS_IDENTIFIED;
		irc_umode_set(irc, "+R", 1);

		if (irc->caps & CAP_SASL) {
			irc_user_t *iu = irc->user;
			irc_send_num(irc, 900, "%s!%s@%s %s :You are now logged in as %s",
				iu->nick, iu->user, iu->host, iu->nick, iu->nick);
		}

		/* Set this var now, or anyone who logs in to his/her
		   newly created account for the first time gets the
		   whatsnew story. */
		g_snprintf(s, sizeof(s), "%d", BITLBEE_VERSION_CODE);
		set_setstr(&irc->b->set, "last_version", s);
		break;

	default:
		irc_rootmsg(irc, "Error registering");
		break;
	}
}

static void cmd_drop(irc_t *irc, char **cmd)
{
	storage_status_t status;

	status = auth_check_pass(irc, irc->user->nick, cmd[1]);
	if (status == STORAGE_OK) {
		status = storage_remove(irc->user->nick);
	}

	switch (status) {
	case STORAGE_NO_SUCH_USER:
		irc_rootmsg(irc, "That account does not exist");
		break;
	case STORAGE_INVALID_PASSWORD:
		irc_rootmsg(irc, "Password invalid");
		break;
	case STORAGE_OK:
		irc_setpass(irc, NULL);
		irc->status &= ~USTATUS_IDENTIFIED;
		irc_umode_set(irc, "-R", 1);
		irc_rootmsg(irc, "Account `%s' removed", irc->user->nick);
		break;
	default:
		irc_rootmsg(irc, "Error: `%d'", status);
		break;
	}
}

static void cmd_save(irc_t *irc, char **cmd)
{
	if ((irc->status & USTATUS_IDENTIFIED) == 0) {
		irc_rootmsg(irc, "Please create an account first (see \x02help register\x02)");
	} else if (storage_save(irc, NULL, TRUE) == STORAGE_OK) {
		irc_rootmsg(irc, "Configuration saved");
	} else {
		irc_rootmsg(irc, "Configuration could not be saved!");
	}
}

static void cmd_showset(irc_t *irc, set_t **head, char *key)
{
	set_t *set;
	char *val;

	if ((val = set_getstr(head, key))) {
		irc_rootmsg(irc, "%s = `%s'", key, val);
	} else if (!(set = set_find(head, key))) {
		irc_rootmsg(irc, "Setting `%s' does not exist.", key);
		if (*head == irc->b->set) {
			irc_rootmsg(irc, "It might be an account or channel setting. "
			            "See \x02help account set\x02 and \x02help channel set\x02.");
		}
	} else if (set->flags & SET_PASSWORD) {
		irc_rootmsg(irc, "%s = `********' (hidden)", key);
	} else {
		irc_rootmsg(irc, "%s is empty", key);
	}
}

typedef set_t** (*cmd_set_findhead)(irc_t*, char*);
typedef int (*cmd_set_checkflags)(irc_t*, set_t *set);

static int cmd_set_real(irc_t *irc, char **cmd, set_t **head, cmd_set_checkflags checkflags)
{
	char *set_name = NULL, *value = NULL;
	gboolean del = FALSE;

	if (cmd[1] && g_strncasecmp(cmd[1], "-del", 4) == 0) {
		MIN_ARGS(2, 0);
		set_name = cmd[2];
		del = TRUE;
	} else {
		set_name = cmd[1];
		value = cmd[2];
	}

	if (set_name && (value || del)) {
		set_t *s = set_find(head, set_name);
		int st;

		if (s && s->flags & SET_LOCKED) {
			irc_rootmsg(irc, "This setting can not be changed");
			return 0;
		}
		if (s && checkflags && checkflags(irc, s) == 0) {
			return 0;
		}

		if (del) {
			st = set_reset(head, set_name);
		} else {
			st = set_setstr(head, set_name, value);
		}

		if (set_getstr(head, set_name) == NULL &&
		    set_find(head, set_name)) {
			/* This happens when changing the passwd, for example.
			   Showing these msgs instead gives slightly clearer
			   feedback. */
			if (st) {
				irc_rootmsg(irc, "Setting changed successfully");
			} else {
				irc_rootmsg(irc, "Failed to change setting");
			}
		} else {
			cmd_showset(irc, head, set_name);
		}
	} else if (set_name) {
		cmd_showset(irc, head, set_name);
	} else {
		set_t *s = *head;
		while (s) {
			if (set_isvisible(s)) {
				cmd_showset(irc, &s, s->key);
			}
			s = s->next;
		}
	}

	return 1;
}

static int cmd_account_set_checkflags(irc_t *irc, set_t *s)
{
	account_t *a = s->data;

	if (a->ic && s && s->flags & ACC_SET_OFFLINE_ONLY) {
		irc_rootmsg(irc, "This setting can only be changed when the account is %s-line", "off");
		return 0;
	} else if (!a->ic && s && s->flags & ACC_SET_ONLINE_ONLY) {
		irc_rootmsg(irc, "This setting can only be changed when the account is %s-line", "on");
		return 0;
	} else if (a->flags & ACC_FLAG_LOCKED && s && s->flags & ACC_SET_LOCKABLE) {
		irc_rootmsg(irc, "This setting can not be changed for locked accounts");
		return 0;
	}

	return 1;
}

static void cmd_account(irc_t *irc, char **cmd)
{
	account_t *a;
	int len;

	if (global.conf->authmode == AUTHMODE_REGISTERED && !(irc->status & USTATUS_IDENTIFIED)) {
		irc_rootmsg(irc, "This server only accepts registered users");
		return;
	}

	len = strlen(cmd[1]);

	if (len >= 1 && g_strncasecmp(cmd[1], "add", len) == 0) {
		struct prpl *prpl;

		MIN_ARGS(3);

		if (!global.conf->allow_account_add) {
			irc_rootmsg(irc, "This server does not allow adding new accounts");
			return;
		}

		if (cmd[4] == NULL) {
			for (a = irc->b->accounts; a; a = a->next) {
				if (strcmp(a->pass, PASSWORD_PENDING) == 0) {
					irc_rootmsg(irc, "Enter password for account %s "
					            "first (use /OPER)", a->tag);
					return;
				}
			}

			irc->status |= OPER_HACK_ACCOUNT_PASSWORD;
		}

		prpl = find_protocol(cmd[2]);

		if (prpl == NULL) {
			if (is_protocol_disabled(cmd[2])) {
				irc_rootmsg(irc, "Protocol disabled in global config");
			} else {
				irc_rootmsg(irc, "Unknown protocol");
			}
			return;
		}

		for (a = irc->b->accounts; a; a = a->next) {
			if (a->prpl == prpl && prpl->handle_cmp(a->user, cmd[3]) == 0) {
				irc_rootmsg(irc, "Warning: You already have an account with "
				            "protocol `%s' and username `%s'. Are you accidentally "
				            "trying to add it twice?", prpl->name, cmd[3]);
			}
		}

		a = account_add(irc->b, prpl, cmd[3], cmd[4] ? cmd[4] : PASSWORD_PENDING);
		if (cmd[5]) {
			irc_rootmsg(irc, "Warning: Passing a servername/other flags to `account add' "
			            "is now deprecated. Use `account set' instead.");
			set_setstr(&a->set, "server", cmd[5]);
		}

		irc_rootmsg(irc, "Account successfully added with tag %s", a->tag);

		if (cmd[4] == NULL) {
			set_t *oauth = set_find(&a->set, "oauth");
			if (oauth && bool2int(set_value(oauth))) {
				*a->pass = '\0';
				irc_rootmsg(irc, "No need to enter a password for this "
				            "account since it's using OAuth");
			} else {
				irc_rootmsg(irc, "You can now use the /OPER command to "
				            "enter the password");
				if (oauth) {
					irc_rootmsg(irc, "Alternatively, enable OAuth if "
					            "the account supports it: account %s "
					            "set oauth on", a->tag);
				}
			}
		}

		return;
	} else if (len >= 1 && g_strncasecmp(cmd[1], "list", len) == 0) {
		int i = 0;

		if (strchr(irc->umode, 'b')) {
			irc_rootmsg(irc, "Account list:");
		}

		for (a = irc->b->accounts; a; a = a->next) {
			char *con;

			if (a->ic && (a->ic->flags & OPT_LOGGED_IN)) {
				con = " (connected)";
			} else if (a->ic) {
				con = " (connecting)";
			} else if (a->reconnect) {
				con = " (awaiting reconnect)";
			} else {
				con = "";
			}

			irc_rootmsg(irc, "%2d (%s): %s, %s%s", i, a->tag, a->prpl->name, a->user, con);

			i++;
		}
		irc_rootmsg(irc, "End of account list");

		return;
	} else if (cmd[2]) {
		/* Try the following two only if cmd[2] == NULL */
	} else if (len >= 2 && g_strncasecmp(cmd[1], "on", len) == 0) {
		if (irc->b->accounts) {
			irc_rootmsg(irc, "Trying to get all accounts connected...");

			for (a = irc->b->accounts; a; a = a->next) {
				if (!a->ic && a->auto_connect) {
					if (strcmp(a->pass, PASSWORD_PENDING) == 0) {
						irc_rootmsg(irc, "Enter password for account %s "
						            "first (use /OPER)", a->tag);
					} else {
						account_on(irc->b, a);
					}
				}
			}
		} else {
			irc_rootmsg(irc, "No accounts known. Use `account add' to add one.");
		}

		return;
	} else if (len >= 2 && g_strncasecmp(cmd[1], "off", len) == 0) {
		irc_rootmsg(irc, "Deactivating all active (re)connections...");

		for (a = irc->b->accounts; a; a = a->next) {
			if (a->ic) {
				account_off(irc->b, a);
			} else if (a->reconnect) {
				cancel_auto_reconnect(a);
			}
		}

		return;
	}

	MIN_ARGS(2);
	len = strlen(cmd[2]);

	/* At least right now, don't accept on/off/set/del as account IDs even
	   if they're a proper match, since people not familiar with the new
	   syntax yet may get a confusing/nasty surprise. */
	if (g_strcasecmp(cmd[1], "on") == 0 ||
	    g_strcasecmp(cmd[1], "off") == 0 ||
	    g_strcasecmp(cmd[1], "set") == 0 ||
	    g_strcasecmp(cmd[1], "del") == 0 ||
	    (a = account_get(irc->b, cmd[1])) == NULL) {
		irc_rootmsg(irc, "Could not find account `%s'.", cmd[1]);

		return;
	}

	if (len >= 1 && g_strncasecmp(cmd[2], "del", len) == 0) {
		if (a->flags & ACC_FLAG_LOCKED) {
			irc_rootmsg(irc, "Account is locked, can't delete");
		}
		else if (a->ic) {
			irc_rootmsg(irc, "Account is still logged in, can't delete");
		} else {
			account_del(irc->b, a);
			irc_rootmsg(irc, "Account deleted");
		}
	} else if (len >= 2 && g_strncasecmp(cmd[2], "on", len) == 0) {
		if (a->ic) {
			irc_rootmsg(irc, "Account already online");
		} else if (strcmp(a->pass, PASSWORD_PENDING) == 0) {
			irc_rootmsg(irc, "Enter password for account %s "
			            "first (use /OPER)", a->tag);
		} else {
			account_on(irc->b, a);
		}
	} else if (len >= 2 && g_strncasecmp(cmd[2], "off", len) == 0) {
		if (a->ic) {
			account_off(irc->b, a);
		} else if (a->reconnect) {
			cancel_auto_reconnect(a);
			irc_rootmsg(irc, "Reconnect cancelled");
		} else {
			irc_rootmsg(irc, "Account already offline");
		}
	} else if (len >= 1 && g_strncasecmp(cmd[2], "set", len) == 0) {
		cmd_set_real(irc, cmd + 2, &a->set, cmd_account_set_checkflags);
	} else {
		irc_rootmsg(irc,
		            "Unknown command: %s [...] %s. Please use \x02help commands\x02 to get a list of available commands.", "account",
		            cmd[2]);
	}
}

static void cmd_channel(irc_t *irc, char **cmd)
{
	irc_channel_t *ic;
	int len;

	len = strlen(cmd[1]);

	if (len >= 1 && g_strncasecmp(cmd[1], "list", len) == 0) {
		GSList *l;
		int i = 0;

		if (strchr(irc->umode, 'b')) {
			irc_rootmsg(irc, "Channel list:");
		}

		for (l = irc->channels; l; l = l->next) {
			irc_channel_t *ic = l->data;

			irc_rootmsg(irc, "%2d. %s, %s channel%s", i, ic->name,
			            set_getstr(&ic->set, "type"),
			            ic->flags & IRC_CHANNEL_JOINED ? " (joined)" : "");

			i++;
		}
		irc_rootmsg(irc, "End of channel list");

		return;
	}

	if ((ic = irc_channel_get(irc, cmd[1])) == NULL) {
		/* If this doesn't match any channel, maybe this is the short
		   syntax (only works when used inside a channel). */
		if ((ic = irc->root->last_channel) &&
		    (len = strlen(cmd[1])) &&
		    g_strncasecmp(cmd[1], "set", len) == 0) {
			cmd_set_real(irc, cmd + 1, &ic->set, NULL);
		} else {
			irc_rootmsg(irc, "Could not find channel `%s'", cmd[1]);
		}

		return;
	}

	MIN_ARGS(2);
	len = strlen(cmd[2]);

	if (len >= 1 && g_strncasecmp(cmd[2], "set", len) == 0) {
		cmd_set_real(irc, cmd + 2, &ic->set, NULL);
	} else if (len >= 1 && g_strncasecmp(cmd[2], "del", len) == 0) {
		if (!(ic->flags & IRC_CHANNEL_JOINED) &&
		    ic != ic->irc->default_channel) {
			irc_rootmsg(irc, "Channel %s deleted.", ic->name);
			irc_channel_free(ic);
		} else {
			irc_rootmsg(irc, "Couldn't remove channel (main channel %s or "
			            "channels you're still in cannot be deleted).",
			            irc->default_channel->name);
		}
	} else {
		irc_rootmsg(irc,
		            "Unknown command: %s [...] %s. Please use \x02help commands\x02 to get a list of available commands.", "channel",
		            cmd[1]);
	}
}

static void cmd_add(irc_t *irc, char **cmd)
{
	account_t *a;
	int add_on_server = 1;
	char *handle = NULL, *s;

	if (g_strcasecmp(cmd[1], "-tmp") == 0) {
		MIN_ARGS(3);
		add_on_server = 0;
		cmd++;
	}

	if (!(a = account_get(irc->b, cmd[1]))) {
		irc_rootmsg(irc, "Invalid account");
		return;
	} else if (!(a->ic && (a->ic->flags & OPT_LOGGED_IN))) {
		irc_rootmsg(irc, "That account is not on-line");
		return;
	}

	if (cmd[3]) {
		if (!nick_ok(irc, cmd[3])) {
			irc_rootmsg(irc, "The requested nick `%s' is invalid", cmd[3]);
			return;
		} else if (irc_user_by_name(irc, cmd[3])) {
			irc_rootmsg(irc, "The requested nick `%s' already exists", cmd[3]);
			return;
		} else {
			nick_set_raw(a, cmd[2], cmd[3]);
		}
	}

	if ((a->flags & ACC_FLAG_HANDLE_DOMAINS) && cmd[2][0] != '_' &&
	    (!(s = strchr(cmd[2], '@')) || s[1] == '\0')) {
		/* If there's no @ or it's the last char, append the user's
		   domain name now. Exclude handles starting with a _ so
		   adding _xmlconsole will keep working. */
		if (s) {
			*s = '\0';
		}
		if ((s = strchr(a->user, '@'))) {
			cmd[2] = handle = g_strconcat(cmd[2], s, NULL);
		}
	}

	if (add_on_server) {
		irc_channel_t *ic;
		char *s, *group = NULL;;

		if ((ic = irc->root->last_channel) &&
		    (s = set_getstr(&ic->set, "fill_by")) &&
		    strcmp(s, "group") == 0 &&
		    (group = set_getstr(&ic->set, "group"))) {
			irc_rootmsg(irc, "Adding `%s' to contact list (group %s)",
			            cmd[2], group);
		} else {
			irc_rootmsg(irc, "Adding `%s' to contact list", cmd[2]);
		}

		a->prpl->add_buddy(a->ic, cmd[2], group);
	} else {
		bee_user_t *bu;
		irc_user_t *iu;

		/* Only for add -tmp. For regular adds, this callback will
		   be called once the IM server confirms. */
		if ((bu = bee_user_new(irc->b, a->ic, cmd[2], BEE_USER_LOCAL)) &&
		    (iu = bu->ui_data)) {
			irc_rootmsg(irc, "Temporarily assigned nickname `%s' "
			            "to contact `%s'", iu->nick, cmd[2]);
		}
	}

	g_free(handle);
}

static void cmd_remove(irc_t *irc, char **cmd)
{
	irc_user_t *iu;
	bee_user_t *bu;
	char *s;

	if (!(iu = irc_user_by_name(irc, cmd[1])) || !(bu = iu->bu)) {
		irc_rootmsg(irc, "Buddy `%s' not found", cmd[1]);
		return;
	}
	s = g_strdup(bu->handle);

	bu->ic->acc->prpl->remove_buddy(bu->ic, bu->handle, NULL);
	nick_del(bu);
	if (g_slist_find(irc->users, iu)) {
		bee_user_free(irc->b, bu);
	}

	irc_rootmsg(irc, "Buddy `%s' (nick %s) removed from contact list", s, cmd[1]);
	g_free(s);

	return;
}

static void cmd_info(irc_t *irc, char **cmd)
{
	struct im_connection *ic;
	account_t *a;

	if (!cmd[2]) {
		irc_user_t *iu = irc_user_by_name(irc, cmd[1]);
		if (!iu || !iu->bu) {
			irc_rootmsg(irc, "Nick `%s' does not exist", cmd[1]);
			return;
		}
		ic = iu->bu->ic;
		cmd[2] = iu->bu->handle;
	} else if (!(a = account_get(irc->b, cmd[1]))) {
		irc_rootmsg(irc, "Invalid account");
		return;
	} else if (!((ic = a->ic) && (a->ic->flags & OPT_LOGGED_IN))) {
		irc_rootmsg(irc, "That account is not on-line");
		return;
	}

	if (!ic->acc->prpl->get_info) {
		irc_rootmsg(irc, "Command `%s' not supported by this protocol", cmd[0]);
	} else {
		ic->acc->prpl->get_info(ic, cmd[2]);
	}
}

static void cmd_rename(irc_t *irc, char **cmd)
{
	irc_user_t *iu, *old;
	gboolean del = g_strcasecmp(cmd[1], "-del") == 0;

	iu = irc_user_by_name(irc, cmd[del ? 2 : 1]);

	if (iu == NULL) {
		irc_rootmsg(irc, "Nick `%s' does not exist", cmd[1]);
	} else if (del) {
		if (iu->bu) {
			bee_irc_user_nick_reset(iu);
		}
		irc_rootmsg(irc, "Nickname reset to `%s'", iu->nick);
	} else if (iu == irc->user) {
		irc_rootmsg(irc, "Use /nick to change your own nickname");
	} else if (!nick_ok(irc, cmd[2])) {
		irc_rootmsg(irc, "Nick `%s' is invalid", cmd[2]);
	} else if ((old = irc_user_by_name(irc, cmd[2])) && old != iu) {
		irc_rootmsg(irc, "Nick `%s' already exists", cmd[2]);
	} else {
		if (!irc_user_set_nick(iu, cmd[2])) {
			irc_rootmsg(irc, "Error while changing nick");
			return;
		}

		if (iu == irc->root) {
			/* If we're called internally (user did "set root_nick"),
			   let's not go O(INF). :-) */
			if (strcmp(cmd[0], "set_rename") != 0) {
				set_setstr(&irc->b->set, "root_nick", cmd[2]);
			}
		} else if (iu->bu) {
			nick_set(iu->bu, cmd[2]);
		}

		irc_rootmsg(irc, "Nick successfully changed");
	}
}

char *set_eval_root_nick(set_t *set, char *new_nick)
{
	irc_t *irc = set->data;

	if (strcmp(irc->root->nick, new_nick) != 0) {
		char *cmd[] = { "set_rename", irc->root->nick, new_nick, NULL };

		cmd_rename(irc, cmd);
	}

	return strcmp(irc->root->nick, new_nick) == 0 ? new_nick : SET_INVALID;
}

static void cmd_block(irc_t *irc, char **cmd)
{
	struct im_connection *ic;
	account_t *a;

	if (!cmd[2] && (a = account_get(irc->b, cmd[1])) && a->ic) {
		char *format;
		GSList *l;

		if (strchr(irc->umode, 'b') != NULL) {
			format = "%s\t%s";
		} else {
			format = "%-32.32s  %-16.16s";
		}

		irc_rootmsg(irc, format, "Handle", "Nickname");
		for (l = a->ic->deny; l; l = l->next) {
			bee_user_t *bu = bee_user_by_handle(irc->b, a->ic, l->data);
			irc_user_t *iu = bu ? bu->ui_data : NULL;
			irc_rootmsg(irc, format, l->data, iu ? iu->nick : "(none)");
		}
		irc_rootmsg(irc, "End of list.");

		return;
	} else if (!cmd[2]) {
		irc_user_t *iu = irc_user_by_name(irc, cmd[1]);
		if (!iu || !iu->bu) {
			irc_rootmsg(irc, "Nick `%s' does not exist", cmd[1]);
			return;
		}
		ic = iu->bu->ic;
		cmd[2] = iu->bu->handle;
	} else if (!(a = account_get(irc->b, cmd[1]))) {
		irc_rootmsg(irc, "Invalid account");
		return;
	} else if (!((ic = a->ic) && (a->ic->flags & OPT_LOGGED_IN))) {
		irc_rootmsg(irc, "That account is not on-line");
		return;
	}

	if (!ic->acc->prpl->add_deny || !ic->acc->prpl->rem_permit) {
		irc_rootmsg(irc, "Command `%s' not supported by this protocol", cmd[0]);
	} else {
		imc_rem_allow(ic, cmd[2]);
		imc_add_block(ic, cmd[2]);
		irc_rootmsg(irc, "Buddy `%s' moved from allow- to block-list", cmd[2]);
	}
}

static void cmd_allow(irc_t *irc, char **cmd)
{
	struct im_connection *ic;
	account_t *a;

	if (!cmd[2] && (a = account_get(irc->b, cmd[1])) && a->ic) {
		char *format;
		GSList *l;

		if (strchr(irc->umode, 'b') != NULL) {
			format = "%s\t%s";
		} else {
			format = "%-32.32s  %-16.16s";
		}

		irc_rootmsg(irc, format, "Handle", "Nickname");
		for (l = a->ic->permit; l; l = l->next) {
			bee_user_t *bu = bee_user_by_handle(irc->b, a->ic, l->data);
			irc_user_t *iu = bu ? bu->ui_data : NULL;
			irc_rootmsg(irc, format, l->data, iu ? iu->nick : "(none)");
		}
		irc_rootmsg(irc, "End of list.");

		return;
	} else if (!cmd[2]) {
		irc_user_t *iu = irc_user_by_name(irc, cmd[1]);
		if (!iu || !iu->bu) {
			irc_rootmsg(irc, "Nick `%s' does not exist", cmd[1]);
			return;
		}
		ic = iu->bu->ic;
		cmd[2] = iu->bu->handle;
	} else if (!(a = account_get(irc->b, cmd[1]))) {
		irc_rootmsg(irc, "Invalid account");
		return;
	} else if (!((ic = a->ic) && (a->ic->flags & OPT_LOGGED_IN))) {
		irc_rootmsg(irc, "That account is not on-line");
		return;
	}

	if (!ic->acc->prpl->rem_deny || !ic->acc->prpl->add_permit) {
		irc_rootmsg(irc, "Command `%s' not supported by this protocol", cmd[0]);
	} else {
		imc_rem_block(ic, cmd[2]);
		imc_add_allow(ic, cmd[2]);

		irc_rootmsg(irc, "Buddy `%s' moved from block- to allow-list", cmd[2]);
	}
}

static void cmd_yesno(irc_t *irc, char **cmd)
{
	query_t *q = NULL;
	int numq = 0;

	if (irc->queries == NULL) {
		/* Alright, alright, let's add a tiny easter egg here. */
		static irc_t *last_irc = NULL;
		static time_t last_time = 0;
		static int times = 0;
		static const char *msg[] = {
			"Oh yeah, that's right.",
			"Alright, alright. Now go back to work.",
			"Buuuuuuuuuuuuuuuurp... Excuse me!",
			"Yes?",
			"No?",
		};

		if (last_irc == irc && time(NULL) - last_time < 15) {
			if ((++times >= 3)) {
				irc_rootmsg(irc, "%s", msg[rand() % (sizeof(msg) / sizeof(char*))]);
				last_irc = NULL;
				times = 0;
				return;
			}
		} else {
			last_time = time(NULL);
			last_irc = irc;
			times = 0;
		}

		irc_rootmsg(irc, "Did I ask you something?");
		return;
	}

	/* If there's an argument, the user seems to want to answer another question than the
	   first/last (depending on the query_order setting) one. */
	if (cmd[1]) {
		if (sscanf(cmd[1], "%d", &numq) != 1) {
			irc_rootmsg(irc, "Invalid query number");
			return;
		}

		for (q = irc->queries; q; q = q->next, numq--) {
			if (numq == 0) {
				break;
			}
		}

		if (!q) {
			irc_rootmsg(irc, "Uhm, I never asked you something like that...");
			return;
		}
	}

	if (g_strcasecmp(cmd[0], "yes") == 0) {
		query_answer(irc, q, 1);
	} else if (g_strcasecmp(cmd[0], "no") == 0) {
		query_answer(irc, q, 0);
	}
}

static void cmd_set(irc_t *irc, char **cmd)
{
	cmd_set_real(irc, cmd, &irc->b->set, NULL);
}

static void cmd_blist(irc_t *irc, char **cmd)
{
	int online = 0, away = 0, offline = 0, ismatch = 0;
	GSList *l;
	GRegex *regex = NULL;
	GError *error = NULL;
	char s[256];
	char *format;
	int n_online = 0, n_away = 0, n_offline = 0;

	if (cmd[1] && g_strcasecmp(cmd[1], "all") == 0) {
		online = offline = away = 1;
	} else if (cmd[1] && g_strcasecmp(cmd[1], "offline") == 0) {
		offline = 1;
	} else if (cmd[1] && g_strcasecmp(cmd[1], "away") == 0) {
		away = 1;
	} else if (cmd[1] && g_strcasecmp(cmd[1], "online") == 0) {
		online = 1;
	} else {
		online = away = 1;
	}

	if (cmd[2]) {
		regex = g_regex_new(cmd[2], G_REGEX_CASELESS, 0, &error);
	}

	if (error) {
		irc_rootmsg(irc, error->message);
		g_error_free(error);
	}

	if (strchr(irc->umode, 'b') != NULL) {
		format = "%s\t%s\t%s";
	} else {
		format = "%-16.16s  %-40.40s  %s";
	}

	irc_rootmsg(irc, format, "Nick", "Handle/Account", "Status");

	if (irc->root->last_channel &&
	    strcmp(set_getstr(&irc->root->last_channel->set, "type"), "control") != 0) {
		irc->root->last_channel = NULL;
	}

	for (l = irc->users; l; l = l->next) {
		irc_user_t *iu = l->data;
		bee_user_t *bu = iu->bu;

		if (!regex || g_regex_match(regex, iu->nick, 0, NULL)) {
			ismatch = 1;
		} else {
			ismatch = 0;
		}

		if (!bu || (irc->root->last_channel && !irc_channel_wants_user(irc->root->last_channel, iu))) {
			continue;
		}

		if ((bu->flags & (BEE_USER_ONLINE | BEE_USER_AWAY)) == BEE_USER_ONLINE) {
			if (ismatch == 1 && online == 1) {
				char st[256] = "Online";

				if (bu->status_msg) {
					g_snprintf(st, sizeof(st) - 1, "Online (%s)", bu->status_msg);
				}

				g_snprintf(s, sizeof(s) - 1, "%s %s", bu->handle, bu->ic->acc->tag);
				irc_rootmsg(irc, format, iu->nick, s, st);
			}

			n_online++;
		}

		if ((bu->flags & BEE_USER_ONLINE) && (bu->flags & BEE_USER_AWAY)) {
			if (ismatch == 1 && away == 1) {
				g_snprintf(s, sizeof(s) - 1, "%s %s", bu->handle, bu->ic->acc->tag);
				irc_rootmsg(irc, format, iu->nick, s, irc_user_get_away(iu));
			}
			n_away++;
		}

		if (!(bu->flags & BEE_USER_ONLINE)) {
			if (ismatch == 1 && offline == 1) {
				g_snprintf(s, sizeof(s) - 1, "%s %s", bu->handle, bu->ic->acc->tag);
				irc_rootmsg(irc, format, iu->nick, s, "Offline");
			}
			n_offline++;
		}
	}

	irc_rootmsg(irc, "%d buddies (%d available, %d away, %d offline)", n_online + n_away + n_offline, n_online,
	            n_away, n_offline);

	if (regex) {
		g_regex_unref(regex);
	}
}

static gint prplcmp(gconstpointer a, gconstpointer b)
{
	const struct prpl *pa = a;
	const struct prpl *pb = b;

	return g_strcasecmp(pa->name, pb->name);
}

static void prplstr(GList *prpls, GString *gstr)
{
	const char *last = NULL;
	GList *l;
	struct prpl *p;

	prpls = g_list_copy(prpls);
	prpls = g_list_sort(prpls, prplcmp);

	for (l = prpls; l; l = l->next) {
		p = l->data;

		if (last && g_strcasecmp(p->name, last) == 0) {
			/* Ignore duplicates (mainly for libpurple) */
			continue;
		}

		if (gstr->len != 0) {
			g_string_append(gstr, ", ");
		}

		g_string_append(gstr, p->name);
		last = p->name;
	}

	g_list_free(prpls);
}

static void cmd_plugins(irc_t *irc, char **cmd)
{
	GList *prpls;
	GString *gstr;

#ifdef WITH_PLUGINS
	GList *l;
	struct plugin_info *info;

	for (l = get_plugins(); l; l = l->next) {
		info = l->data;
		irc_rootmsg(irc, "%s:", info->name);
		irc_rootmsg(irc, "  Version: %s", info->version);

		if (info->description) {
			irc_rootmsg(irc, "  Description: %s", info->description);
		}

		if (info->author) {
			irc_rootmsg(irc, "  Author: %s", info->author);
		}

		if (info->url) {
			irc_rootmsg(irc, "  URL: %s", info->url);
		}

		irc_rootmsg(irc, "");
	}
#endif

	gstr = g_string_new(NULL);
	prpls = get_protocols();

	if (prpls) {
		prplstr(prpls, gstr);
		irc_rootmsg(irc, "Enabled Protocols: %s", gstr->str);
		g_string_truncate(gstr, 0);
	}

	prpls = get_protocols_disabled();

	if (prpls) {
		prplstr(prpls, gstr);
		irc_rootmsg(irc, "Disabled Protocols: %s", gstr->str);
	}

	g_string_free(gstr, TRUE);
}

static void cmd_qlist(irc_t *irc, char **cmd)
{
	query_t *q = irc->queries;
	int num;

	if (!q) {
		irc_rootmsg(irc, "There are no pending questions.");
		return;
	}

	irc_rootmsg(irc, "Pending queries:");

	for (num = 0; q; q = q->next, num++) {
		if (q->ic) { /* Not necessary yet, but it might come later */
			irc_rootmsg(irc, "%d, %s: %s", num, q->ic->acc->tag, q->question);
		} else {
			irc_rootmsg(irc, "%d, BitlBee: %s", num, q->question);
		}
	}
}

static void cmd_chat(irc_t *irc, char **cmd)
{
	account_t *acc;

	if (g_strcasecmp(cmd[1], "add") == 0) {
		bee_chat_info_t *ci;
		char *channel, *room, *s;
		struct irc_channel *ic;
		guint i;

		MIN_ARGS(3);

		if (!(acc = account_get(irc->b, cmd[2]))) {
			irc_rootmsg(irc, "Invalid account");
			return;
		} else if (!acc->prpl->chat_join) {
			irc_rootmsg(irc, "Named chatrooms not supported on that account.");
			return;
		}

		if (cmd[3][0] == '!') {
			if (!acc->prpl->chat_list) {
				irc_rootmsg(irc, "Listing chatrooms not supported on that account.");
				return;
			}

			i = g_ascii_strtoull(cmd[3] + 1, NULL, 10);
			ci = g_slist_nth_data(acc->ic->chatlist, i - 1);

			if (ci == NULL) {
				irc_rootmsg(irc, "Invalid chatroom index");
				return;
			}

			room = ci->title;
		} else {
			room = cmd[3];
		}

		if (cmd[4] == NULL) {
			channel = g_strdup(room);
			if ((s = strchr(channel, '@'))) {
				*s = 0;
			}
		} else {
			channel = g_strdup(cmd[4]);
		}

		if (strchr(CTYPES, channel[0]) == NULL) {
			s = g_strdup_printf("#%s", channel);
			g_free(channel);
			channel = s;

			irc_channel_name_strip(channel);
		}

		if ((ic = irc_channel_new(irc, channel)) &&
		    set_setstr(&ic->set, "type", "chat") &&
		    set_setstr(&ic->set, "chat_type", "room") &&
		    set_setstr(&ic->set, "account", cmd[2]) &&
		    set_setstr(&ic->set, "room", room)) {
			irc_rootmsg(irc, "Chatroom successfully added.");
		} else {
			if (ic) {
				irc_channel_free(ic);
			}

			irc_rootmsg(irc, "Could not add chatroom.");
		}
		g_free(channel);
	} else if (g_strcasecmp(cmd[1], "list") == 0) {
		MIN_ARGS(2);

		if (!(acc = account_get(irc->b, cmd[2]))) {
			irc_rootmsg(irc, "Invalid account");
			return;
		} else if (!acc->prpl->chat_list) {
			irc_rootmsg(irc, "Listing chatrooms not supported on that account.");
			return;
		}

		acc->prpl->chat_list(acc->ic, cmd[3]);
	} else if (g_strcasecmp(cmd[1], "with") == 0) {
		irc_user_t *iu;

		MIN_ARGS(2);

		if ((iu = irc_user_by_name(irc, cmd[2])) &&
		    iu->bu && iu->bu->ic->acc->prpl->chat_with) {
			if (!iu->bu->ic->acc->prpl->chat_with(iu->bu->ic, iu->bu->handle)) {
				irc_rootmsg(irc, "(Possible) failure while trying to open "
				            "a groupchat with %s.", iu->nick);
			}
		} else {
			irc_rootmsg(irc, "Can't open a groupchat with %s.", cmd[2]);
		}
	} else if (g_strcasecmp(cmd[1], "set") == 0 ||
	           g_strcasecmp(cmd[1], "del") == 0) {
		irc_rootmsg(irc,
		            "Warning: The \002chat\002 command was mostly replaced with the \002channel\002 command.");
		cmd_channel(irc, cmd);
	} else {
		irc_rootmsg(irc,
		            "Unknown command: %s %s. Please use \x02help commands\x02 to get a list of available commands.", "chat",
		            cmd[1]);
	}
}

void cmd_chat_list_finish(struct im_connection *ic)
{
	account_t *acc = ic->acc;
	bee_chat_info_t *ci;
	char *hformat, *iformat, *topic;
	GSList *l;
	guint i = 0;
	irc_t *irc = ic->bee->ui_data;

	if (ic->chatlist == NULL) {
		irc_rootmsg(irc, "No existing chatrooms");
		return;
	}

	if (strchr(irc->umode, 'b') != NULL) {
		hformat = "%s\t%s\t%s";
		iformat = "%u\t%s\t%s";
	} else {
		hformat = "%s  %-20s  %s";
		iformat = "%5u  %-20.20s  %s";
	}

	irc_rootmsg(irc, hformat, "Index", "Title", "Topic");

	for (l = ic->chatlist; l; l = l->next) {
		ci = l->data;
		topic = ci->topic ? ci->topic : "";
		irc_rootmsg(irc, iformat, ++i, ci->title, topic);
	}

	irc_rootmsg(irc, "%u %s chatrooms", i, acc->tag);
}

static void cmd_group(irc_t *irc, char **cmd)
{
	GSList *l;
	int len;

	len = strlen(cmd[1]);
	if (g_strncasecmp(cmd[1], "list", len) == 0) {
		int n = 0;

		if (strchr(irc->umode, 'b')) {
			irc_rootmsg(irc, "Group list:");
		}

		for (l = irc->b->groups; l; l = l->next) {
			bee_group_t *bg = l->data;
			irc_rootmsg(irc, "%d. %s", n++, bg->name);
		}
		irc_rootmsg(irc, "End of group list");
	} else if (g_strncasecmp(cmd[1], "info", len) == 0) {
		bee_group_t *bg;
		int n = 0;

		MIN_ARGS(2);
		bg = bee_group_by_name(irc->b, cmd[2], FALSE);

		if (bg) {
			if (strchr(irc->umode, 'b')) {
				irc_rootmsg(irc, "Members of %s:", cmd[2]);
			}
			for (l = irc->b->users; l; l = l->next) {
				bee_user_t *bu = l->data;
				if (bu->group == bg) {
					irc_rootmsg(irc, "%d. %s", n++, bu->nick ? : bu->handle);
				}
			}
			irc_rootmsg(irc, "End of member list");
		} else {
			irc_rootmsg(irc,
			            "Unknown group: %s. Please use \x02group list\x02 to get a list of available groups.",
			            cmd[2]);
		}
	} else {
		irc_rootmsg(irc,
		            "Unknown command: %s %s. Please use \x02help commands\x02 to get a list of available commands.", "group",
		            cmd[1]);
	}
}

static void cmd_transfer(irc_t *irc, char **cmd)
{
	GSList *files = irc->file_transfers;
	GSList *next;

	enum { LIST, REJECT, CANCEL };
	int subcmd = LIST;
	int fid;

	if (!files) {
		irc_rootmsg(irc, "No pending transfers");
		return;
	}

	if (cmd[1] && (strcmp(cmd[1], "reject") == 0)) {
		subcmd = REJECT;
	} else if (cmd[1] && (strcmp(cmd[1], "cancel") == 0) &&
	           cmd[2] && (sscanf(cmd[2], "%d", &fid) == 1)) {
		subcmd = CANCEL;
	}

	for (; files; files = next) {
		next = files->next;
		file_transfer_t *file = files->data;

		switch (subcmd) {
		case LIST:
			if (file->status == FT_STATUS_LISTENING) {
				irc_rootmsg(irc,
				            "Pending file(id %d): %s (Listening...)", file->local_id, file->file_name);
			} else {
				int kb_per_s = 0;
				time_t diff = time(NULL) - file->started ? : 1;
				if ((file->started > 0) && (file->bytes_transferred > 0)) {
					kb_per_s = file->bytes_transferred / 1024 / diff;
				}

				irc_rootmsg(irc,
				            "Pending file(id %d): %s (%10zd/%zd kb, %d kb/s)", file->local_id,
				            file->file_name,
				            file->bytes_transferred / 1024, file->file_size / 1024, kb_per_s);
			}
			break;
		case REJECT:
			if (file->status == FT_STATUS_LISTENING) {
				irc_rootmsg(irc, "Rejecting file transfer for %s", file->file_name);
				imcb_file_canceled(file->ic, file, "Denied by user");
			}
			break;
		case CANCEL:
			if (file->local_id == fid) {
				irc_rootmsg(irc, "Canceling file transfer for %s", file->file_name);
				imcb_file_canceled(file->ic, file, "Canceled by user");
			}
			break;
		}
	}
}

static void cmd_nick(irc_t *irc, char **cmd)
{
	irc_rootmsg(irc, "This command is deprecated. Try: account %s set display_name", cmd[1]);
}

/* Maybe this should be a stand-alone command as well? */
static void bitlbee_whatsnew(irc_t *irc)
{
	int last = set_getint(&irc->b->set, "last_version");
	char s[16], *msg;

	if (last >= BITLBEE_VERSION_CODE) {
		return;
	}

	msg = help_get_whatsnew(&(global.help), last);

	if (msg) {
		irc_rootmsg(irc, "%s: This seems to be your first time using this "
		            "this version of BitlBee. Here's a list of new "
		            "features you may like to know about:\n\n%s\n",
		            irc->user->nick, msg);
	}

	g_free(msg);

	g_snprintf(s, sizeof(s), "%d", BITLBEE_VERSION_CODE);
	set_setstr(&irc->b->set, "last_version", s);
}

/* IMPORTANT: Keep this list sorted! The short command logic needs that. */
command_t root_commands[] = {
	{ "account",        1, cmd_account,        0 },
	{ "add",            2, cmd_add,            0 },
	{ "allow",          1, cmd_allow,          0 },
	{ "blist",          0, cmd_blist,          0 },
	{ "block",          1, cmd_block,          0 },
	{ "channel",        1, cmd_channel,        0 },
	{ "chat",           1, cmd_chat,           0 },
	{ "drop",           1, cmd_drop,           0 },
	{ "ft",             0, cmd_transfer,       0 },
	{ "group",          1, cmd_group,          0 },
	{ "help",           0, cmd_help,           0 },
	{ "identify",       0, cmd_identify,       0 },
	{ "info",           1, cmd_info,           0 },
	{ "nick",           1, cmd_nick,           0 },
	{ "no",             0, cmd_yesno,          0 },
	{ "plugins",        0, cmd_plugins,        0 },
	{ "qlist",          0, cmd_qlist,          0 },
	{ "register",       0, cmd_register,       0 },
	{ "remove",         1, cmd_remove,         0 },
	{ "rename",         2, cmd_rename,         0 },
	{ "save",           0, cmd_save,           0 },
	{ "set",            0, cmd_set,            0 },
	{ "transfer",       0, cmd_transfer,       0 },
	{ "yes",            0, cmd_yesno,          0 },
	/* Not expecting too many plugins adding root commands so just make a
	   dumb array with some empty entried at the end. */
	{ NULL },
	{ NULL },
	{ NULL },
	{ NULL },
	{ NULL },
	{ NULL },
	{ NULL },
	{ NULL },
	{ NULL },
};
static const int num_root_commands = sizeof(root_commands) / sizeof(command_t);

gboolean root_command_add(const char *command, int params, void (*func)(irc_t *, char **args), int flags)
{
	int i;

	if (root_commands[num_root_commands - 2].command) {
		/* Planning fail! List is full. */
		return FALSE;
	}

	for (i = 0; root_commands[i].command; i++) {
		if (g_strcasecmp(root_commands[i].command, command) == 0) {
			return FALSE;
		} else if (g_strcasecmp(root_commands[i].command, command) > 0) {
			break;
		}
	}
	memmove(root_commands + i + 1, root_commands + i,
	        sizeof(command_t) * (num_root_commands - i - 1));

	root_commands[i].command = g_strdup(command);
	root_commands[i].required_parameters = params;
	root_commands[i].execute = func;
	root_commands[i].flags = flags;

	return TRUE;
}
