/********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2012 Wilmer van der Gaast and others                *
  \********************************************************************/

/* The IRC-based UI - Sending responses to commands/etc.                */

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

void irc_send_num(irc_t *irc, int code, char *format, ...)
{
	char text[IRC_MAX_LINE];
	va_list params;

	va_start(params, format);
	g_vsnprintf(text, IRC_MAX_LINE, format, params);
	va_end(params);

	irc_write(irc, ":%s %03d %s %s", irc->root->host, code, irc->user->nick ? : "*", text);
}

void irc_send_login(irc_t *irc)
{
	irc_send_num(irc,   1, ":Welcome to the %s gateway, %s", PACKAGE, irc->user->nick);
	irc_send_num(irc,   2, ":Host %s is running %s %s %s/%s.", irc->root->host,
	             PACKAGE, BITLBEE_VERSION, ARCH, CPU);
	irc_send_num(irc,   3, ":%s", IRCD_INFO);
	irc_send_num(irc,   4, "%s %s %s %s", irc->root->host, BITLBEE_VERSION, UMODES UMODES_PRIV, CMODES);
	irc_send_num(irc,   5, "PREFIX=(ohv)@%%+ CHANTYPES=%s CHANMODES=,,,%s NICKLEN=%d CHANNELLEN=%d "
	             "NETWORK=BitlBee SAFELIST CASEMAPPING=rfc1459 MAXTARGETS=1 WATCH=128 "
	             "FLOOD=0/9999 :are supported by this server",
	             CTYPES, CMODES, MAX_NICK_LENGTH - 1, MAX_NICK_LENGTH - 1);
	irc_send_motd(irc);
}

void irc_send_motd(irc_t *irc)
{
	char motd[2048];
	ssize_t len;
	int fd;

	fd = open(global.conf->motdfile, O_RDONLY);
	if (fd == -1 || (len = read(fd, motd, sizeof(motd) - 1)) <= 0) {
		irc_send_num(irc, 422, ":We don't need MOTDs.");
	} else {
		char linebuf[80];
		char *add = "", max, *in;

		in = motd;
		motd[len] = '\0';
		linebuf[79] = len = 0;
		max = sizeof(linebuf) - 1;

		irc_send_num(irc, 375, ":- %s Message Of The Day - ", irc->root->host);
		while ((linebuf[len] = *(in++))) {
			if (linebuf[len] == '\n' || len == max) {
				linebuf[len] = 0;
				irc_send_num(irc, 372, ":- %s", linebuf);
				len = 0;
			} else if (linebuf[len] == '%') {
				linebuf[len] = *(in++);
				if (linebuf[len] == 'h') {
					add = irc->root->host;
				} else if (linebuf[len] == 'v') {
					add = BITLBEE_VERSION;
				} else if (linebuf[len] == 'n') {
					add = irc->user->nick;
				} else if (linebuf[len] == '\0') {
					in--;
				} else {
					add = "%";
				}

				strncpy(linebuf + len, add, max - len);
				while (linebuf[++len]) {
					;
				}
			} else if (len < max) {
				len++;
			}
		}
		irc_send_num(irc, 376, ":End of MOTD");
	}

	if (fd != -1) {
		close(fd);
	}
}

/* Used by some funcs that generate PRIVMSGs to figure out if we're talking to
   this person in /query or in a control channel. WARNING: callers rely on
   this returning a pointer at irc->user_nick, not a copy of it. */
const char *irc_user_msgdest(irc_user_t *iu)
{
	irc_t *irc = iu->irc;
	irc_channel_t *ic = NULL;

	if (iu->last_channel) {
		if (iu->last_channel->flags & IRC_CHANNEL_JOINED) {
			ic = iu->last_channel;
		} else {
			ic = irc_channel_with_user(irc, iu);
		}
	}

	if (ic) {
		return ic->name;
	} else {
		return irc->user->nick;
	}
}

/* cmd = "PRIVMSG" or "NOTICE" */
static void irc_usermsg_(const char *cmd, irc_user_t *iu, const char *format, va_list params)
{
	char text[2048];
	const char *dst;

	g_vsnprintf(text, sizeof(text), format, params);

	dst = irc_user_msgdest(iu);
	irc_send_msg(iu, cmd, dst, text, NULL);
}

void irc_usermsg(irc_user_t *iu, char *format, ...)
{
	va_list params;

	va_start(params, format);
	irc_usermsg_("PRIVMSG", iu, format, params);
	va_end(params);
}

void irc_usernotice(irc_user_t *iu, char *format, ...)
{
	va_list params;

	va_start(params, format);
	irc_usermsg_("NOTICE", iu, format, params);
	va_end(params);
}

void irc_rootmsg(irc_t *irc, char *format, ...)
{
	va_list params;

	va_start(params, format);
	irc_usermsg_("PRIVMSG", irc->root, format, params);
	va_end(params);
}

void irc_send_join(irc_channel_t *ic, irc_user_t *iu)
{
	irc_t *irc = ic->irc;

	irc_write(irc, ":%s!%s@%s JOIN :%s", iu->nick, iu->user, iu->host, ic->name);

	if (iu == irc->user) {
		if (ic->topic && *ic->topic) {
			irc_send_topic(ic, FALSE);
		}
		irc_send_names(ic);
	}
}

void irc_send_part(irc_channel_t *ic, irc_user_t *iu, const char *reason)
{
	irc_write(ic->irc, ":%s!%s@%s PART %s :%s", iu->nick, iu->user, iu->host, ic->name, reason ? : "");
}

void irc_send_quit(irc_user_t *iu, const char *reason)
{
	irc_write(iu->irc, ":%s!%s@%s QUIT :%s", iu->nick, iu->user, iu->host, reason ? : "");
}

void irc_send_kick(irc_channel_t *ic, irc_user_t *iu, irc_user_t *kicker, const char *reason)
{
	irc_write(ic->irc, ":%s!%s@%s KICK %s %s :%s", kicker->nick, kicker->user,
	          kicker->host, ic->name, iu->nick, reason ? : "");
}

void irc_send_names(irc_channel_t *ic)
{
	GSList *l;
	char namelist[385] = "";

	/* RFCs say there is no error reply allowed on NAMES, so when the
	   channel is invalid, just give an empty reply. */
	for (l = ic->users; l; l = l->next) {
		irc_channel_user_t *icu = l->data;
		irc_user_t *iu = icu->iu;

		if (strlen(namelist) + strlen(iu->nick) > sizeof(namelist) - 4) {
			irc_send_num(ic->irc, 353, "= %s :%s", ic->name, namelist);
			*namelist = 0;
		}

		if (icu->flags & IRC_CHANNEL_USER_OP) {
			strcat(namelist, "@");
		} else if (icu->flags & IRC_CHANNEL_USER_HALFOP) {
			strcat(namelist, "%");
		} else if (icu->flags & IRC_CHANNEL_USER_VOICE) {
			strcat(namelist, "+");
		}

		strcat(namelist, iu->nick);
		strcat(namelist, " ");
	}

	if (*namelist) {
		irc_send_num(ic->irc, 353, "= %s :%s", ic->name, namelist);
	}

	irc_send_num(ic->irc, 366, "%s :End of /NAMES list", ic->name);
}

void irc_send_topic(irc_channel_t *ic, gboolean topic_change)
{
	if (topic_change && ic->topic_who) {
		irc_write(ic->irc, ":%s TOPIC %s :%s", ic->topic_who,
		          ic->name, ic->topic && *ic->topic ? ic->topic : "");
	} else if (ic->topic) {
		irc_send_num(ic->irc, 332, "%s :%s", ic->name, ic->topic);
		if (ic->topic_who) {
			irc_send_num(ic->irc, 333, "%s %s %d",
			             ic->name, ic->topic_who, (int) ic->topic_time);
		}
	} else {
		irc_send_num(ic->irc, 331, "%s :No topic for this channel", ic->name);
	}
}

void irc_send_whois(irc_user_t *iu)
{
	irc_t *irc = iu->irc;

	irc_send_num(irc, 311, "%s %s %s * :%s",
	             iu->nick, iu->user, iu->host, iu->fullname);

	if (iu->bu) {
		bee_user_t *bu = iu->bu;

		irc_send_num(irc, 312, "%s %s.%s :%s network", iu->nick, bu->ic->acc->user,
		             bu->ic->acc->server && *bu->ic->acc->server ? bu->ic->acc->server : "",
		             bu->ic->acc->prpl->name);

		if ((bu->status && *bu->status) ||
		    (bu->status_msg && *bu->status_msg)) {
			int num = bu->flags & BEE_USER_AWAY ? 301 : 320;

			if (bu->status && bu->status_msg) {
				irc_send_num(irc, num, "%s :%s (%s)", iu->nick, bu->status, bu->status_msg);
			} else {
				irc_send_num(irc, num, "%s :%s", iu->nick, bu->status ? : bu->status_msg);
			}
		} else if (!(bu->flags & BEE_USER_ONLINE)) {
			irc_send_num(irc, 301, "%s :%s", iu->nick, "User is offline");
		}

		if (bu->idle_time || bu->login_time) {
			irc_send_num(irc, 317, "%s %d %d :seconds idle, signon time",
			             iu->nick,
			             bu->idle_time ? (int) (time(NULL) - bu->idle_time) : 0,
			             (int) bu->login_time);
		}
	} else {
		irc_send_num(irc, 312, "%s %s :%s", iu->nick, irc->root->host, IRCD_INFO);
	}

	irc_send_num(irc, 318, "%s :End of /WHOIS list", iu->nick);
}

void irc_send_who(irc_t *irc, GSList *l, const char *channel)
{
	gboolean is_channel = strchr(CTYPES, channel[0]) != NULL;

	while (l) {
		irc_user_t *iu = l->data;
		if (is_channel) {
			iu = ((irc_channel_user_t *) iu)->iu;
		}
		/* TODO(wilmer): Restore away/channel information here */
		irc_send_num(irc, 352, "%s %s %s %s %s %c :0 %s",
		             is_channel ? channel : "*", iu->user, iu->host, irc->root->host,
		             iu->nick, iu->flags & IRC_USER_AWAY ? 'G' : 'H',
		             iu->fullname);
		l = l->next;
	}

	irc_send_num(irc, 315, "%s :End of /WHO list", channel);
}

void irc_send_msg(irc_user_t *iu, const char *type, const char *dst, const char *msg, const char *prefix)
{
	char last = 0;
	const char *s = msg, *line = msg;
	char raw_msg[strlen(msg) + 1024];

	while (!last) {
		if (*s == '\r' && *(s + 1) == '\n') {
			s++;
		}
		if (*s == '\n') {
			last = s[1] == 0;
		} else {
			last = s[0] == 0;
		}
		if (*s == 0 || *s == '\n') {
			if (g_strncasecmp(line, "/me ", 4) == 0 && (!prefix || !*prefix) &&
			    g_strcasecmp(type, "PRIVMSG") == 0) {
				strcpy(raw_msg, "\001ACTION ");
				strncat(raw_msg, line + 4, s - line - 4);
				strcat(raw_msg, "\001");
				irc_send_msg_raw(iu, type, dst, raw_msg);
			} else {
				*raw_msg = '\0';
				if (prefix && *prefix) {
					strcpy(raw_msg, prefix);
				}
				strncat(raw_msg, line, s - line);
				irc_send_msg_raw(iu, type, dst, raw_msg);
			}
			line = s + 1;
		}
		s++;
	}
}

void irc_send_msg_raw(irc_user_t *iu, const char *type, const char *dst, const char *msg)
{
	irc_write(iu->irc, ":%s!%s@%s %s %s :%s",
	          iu->nick, iu->user, iu->host, type, dst, msg && *msg ? msg : " ");
}

void irc_send_msg_f(irc_user_t *iu, const char *type, const char *dst, const char *format, ...)
{
	char text[IRC_MAX_LINE];
	va_list params;

	va_start(params, format);
	g_vsnprintf(text, IRC_MAX_LINE, format, params);
	va_end(params);

	irc_write(iu->irc, ":%s!%s@%s %s %s :%s",
	          iu->nick, iu->user, iu->host, type, dst, text);
}

void irc_send_nick(irc_user_t *iu, const char *new)
{
	irc_write(iu->irc, ":%s!%s@%s NICK %s",
	          iu->nick, iu->user, iu->host, new);
}

/* Send an update of a user's mode inside a channel, compared to what it was. */
void irc_send_channel_user_mode_diff(irc_channel_t *ic, irc_user_t *iu,
                                     irc_channel_user_flags_t old, irc_channel_user_flags_t new)
{
	char changes[3 * (5 + strlen(iu->nick))];
	char from[strlen(ic->irc->root->nick) + strlen(ic->irc->root->user) + strlen(ic->irc->root->host) + 3];
	int n;

	*changes = '\0'; n = 0;
	if ((old & IRC_CHANNEL_USER_OP) != (new & IRC_CHANNEL_USER_OP)) {
		n++;
		if (new & IRC_CHANNEL_USER_OP) {
			strcat(changes, "+o");
		} else {
			strcat(changes, "-o");
		}
	}
	if ((old & IRC_CHANNEL_USER_HALFOP) != (new & IRC_CHANNEL_USER_HALFOP)) {
		n++;
		if (new & IRC_CHANNEL_USER_HALFOP) {
			strcat(changes, "+h");
		} else {
			strcat(changes, "-h");
		}
	}
	if ((old & IRC_CHANNEL_USER_VOICE) != (new & IRC_CHANNEL_USER_VOICE)) {
		n++;
		if (new & IRC_CHANNEL_USER_VOICE) {
			strcat(changes, "+v");
		} else {
			strcat(changes, "-v");
		}
	}
	while (n) {
		strcat(changes, " ");
		strcat(changes, iu->nick);
		n--;
	}

	if (set_getbool(&ic->irc->b->set, "simulate_netsplit")) {
		g_snprintf(from, sizeof(from), "%s", ic->irc->root->host);
	} else {
		g_snprintf(from, sizeof(from), "%s!%s@%s", ic->irc->root->nick,
		           ic->irc->root->user, ic->irc->root->host);
	}

	if (*changes) {
		irc_write(ic->irc, ":%s MODE %s %s", from, ic->name, changes);
	}
}

void irc_send_invite(irc_user_t *iu, irc_channel_t *ic)
{
	irc_t *irc = iu->irc;

	irc_write(iu->irc, ":%s!%s@%s INVITE %s :%s",
	          iu->nick, iu->user, iu->host, irc->user->nick, ic->name);
}

void irc_send_cap(irc_t *irc, char *subcommand, char *body)
{
	char *nick = irc->user->nick ? : "*";

	irc_write(irc, ":%s CAP %s %s :%s", irc->root->host, nick, subcommand, body);
}
