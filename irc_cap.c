/********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2013 Wilmer van der Gaast and others                *
  \********************************************************************/

/* IRCv3 CAP command
 *
 * Specs:
 *  - v3.1: http://ircv3.net/specs/core/capability-negotiation-3.1.html
 *  - v3.2: http://ircv3.net/specs/core/capability-negotiation-3.2.html
 *
 * */

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

typedef struct {
	char *name;
	irc_cap_flag_t flag;
} cap_info_t;

static const cap_info_t supported_caps[] = {
	{"sasl", CAP_SASL},
	{"multi-prefix", CAP_MULTI_PREFIX},
	{"extended-join", CAP_EXTENDED_JOIN},
	{"away-notify", CAP_AWAY_NOTIFY},
	{"userhost-in-names", CAP_USERHOST_IN_NAMES},
	{NULL},
};

static irc_cap_flag_t cap_flag_from_string(char *cap_name)
{
	int i;

	if (!cap_name || !cap_name[0]) {
		return 0;
	}

	if (cap_name[0] == '-') {
		cap_name++;
	}

	for (i = 0; supported_caps[i].name; i++) {
		if (strcmp(supported_caps[i].name, cap_name) == 0) {
			return supported_caps[i].flag;
		}
	}
	return 0;
}

static gboolean irc_cmd_cap_req(irc_t *irc, char *caps)
{
	int i;
	char *lower = NULL;
	char **split = NULL;
	irc_cap_flag_t new_caps = irc->caps;

	if (!caps || !caps[0]) {
		return FALSE;
	}

	lower = g_ascii_strdown(caps, -1);
	split = g_strsplit(lower, " ", -1);
	g_free(lower);

	for (i = 0; split[i]; i++) {
		gboolean remove;
		irc_cap_flag_t flag;

		if (!split[i][0]) {
			continue;   /* skip empty items (consecutive spaces) */
		}

		remove = (split[i][0] == '-');
		flag = cap_flag_from_string(split[i]);
		
		if (!flag || (remove && !(irc->caps & flag))) {
			/* unsupported cap, or removing something that isn't there */
			g_strfreev(split);
			return FALSE;
		}

		if (remove) {
			new_caps &= ~flag;
		} else {
			new_caps |= flag;
		}
	}

	/* if we got here, set the new caps and ack */
	irc->caps = new_caps;

	g_strfreev(split);
	return TRUE;
}

/* version can be 0, 302, 303, or garbage from user input. thanks user input */
static void irc_cmd_cap_ls(irc_t *irc, long version)
{
	int i;
	GString *str = g_string_sized_new(256);

	for (i = 0; supported_caps[i].name; i++) {
		if (i != 0) {
			g_string_append_c(str, ' ');
		}
		g_string_append(str, supported_caps[i].name);

		if (version >= 302 && supported_caps[i].flag == CAP_SASL) {
			g_string_append(str, "=PLAIN");
		}
	}

	irc_send_cap(irc, "LS", str->str);

	g_string_free(str, TRUE);
}

/* this one looks suspiciously similar to cap ls,
 * but cap-3.2 will make them very different */
static void irc_cmd_cap_list(irc_t *irc)
{
	int i;
	gboolean first = TRUE;
	GString *str = g_string_sized_new(256);

	for (i = 0; supported_caps[i].name; i++) {
		if (irc->caps & supported_caps[i].flag) {
			if (!first) {
				g_string_append_c(str, ' ');
			}
			first = FALSE;

			g_string_append(str, supported_caps[i].name);
		}
	}

	irc_send_cap(irc, "LIST", str->str);

	g_string_free(str, TRUE);
}

void irc_cmd_cap(irc_t *irc, char **cmd)
{
	if (!(irc->status & USTATUS_LOGGED_IN)) {
		/* Put registration on hold until CAP END */
		irc->status |= USTATUS_CAP_PENDING;
	}

	if (g_strcasecmp(cmd[1], "LS") == 0) {
		irc_cmd_cap_ls(irc, cmd[2] ? strtol(cmd[2], NULL, 10) : 0);

	} else if (g_strcasecmp(cmd[1], "LIST") == 0) {
		irc_cmd_cap_list(irc);

	} else if (g_strcasecmp(cmd[1], "REQ") == 0) {
		gboolean ack = irc_cmd_cap_req(irc, cmd[2]);

		irc_send_cap(irc, ack ? "ACK" : "NAK", cmd[2] ? : "");

	} else if (g_strcasecmp(cmd[1], "END") == 0) {
		irc->status &= ~USTATUS_CAP_PENDING;

		if (irc->status & USTATUS_SASL_PLAIN_PENDING) {
			irc_send_num(irc, 906, ":SASL authentication aborted");
			irc->status &= ~USTATUS_SASL_PLAIN_PENDING;
		}

		irc_check_login(irc);

	} else {
		irc_send_num(irc, 410, "%s :Invalid CAP command", cmd[1]);
	}

}

