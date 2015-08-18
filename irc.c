/********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2012 Wilmer van der Gaast and others                *
  \********************************************************************/

/* The IRC-based UI (for now the only one)                              */

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
#include "ipc.h"
#include "dcc.h"
#include "lib/ssl_client.h"

GSList *irc_connection_list;
GSList *irc_plugins;

static gboolean irc_userping(gpointer _irc, gint fd, b_input_condition cond);
static char *set_eval_charset(set_t *set, char *value);
static char *set_eval_password(set_t *set, char *value);
static char *set_eval_bw_compat(set_t *set, char *value);
static char *set_eval_utf8_nicks(set_t *set, char *value);

irc_t *irc_new(int fd)
{
	irc_t *irc;
	struct sockaddr_storage sock;
	socklen_t socklen = sizeof(sock);
	char *host = NULL, *myhost = NULL;
	irc_user_t *iu;
	GSList *l;
	set_t *s;
	bee_t *b;

	irc = g_new0(irc_t, 1);

	irc->fd = fd;
	sock_make_nonblocking(irc->fd);

	irc->r_watch_source_id = b_input_add(irc->fd, B_EV_IO_READ, bitlbee_io_current_client_read, irc);

	irc->status = USTATUS_OFFLINE;
	irc->last_pong = gettime();

	irc->nick_user_hash = g_hash_table_new(g_str_hash, g_str_equal);
	irc->watches = g_hash_table_new(g_str_hash, g_str_equal);

	irc->iconv = (GIConv) - 1;
	irc->oconv = (GIConv) - 1;

	if (global.conf->hostname) {
		myhost = g_strdup(global.conf->hostname);
	} else if (getsockname(irc->fd, (struct sockaddr*) &sock, &socklen) == 0) {
		char buf[NI_MAXHOST + 1];

		if (getnameinfo((struct sockaddr *) &sock, socklen, buf,
		                NI_MAXHOST, NULL, 0, 0) == 0) {
			myhost = g_strdup(ipv6_unwrap(buf));
		}
	}

	if (getpeername(irc->fd, (struct sockaddr*) &sock, &socklen) == 0) {
		char buf[NI_MAXHOST + 1];

		if (getnameinfo((struct sockaddr *) &sock, socklen, buf,
		                NI_MAXHOST, NULL, 0, 0) == 0) {
			host = g_strdup(ipv6_unwrap(buf));
		}
	}

	if (host == NULL) {
		host = g_strdup("localhost.localdomain");
	}
	if (myhost == NULL) {
		myhost = g_strdup("localhost.localdomain");
	}

	if (global.conf->ping_interval > 0 && global.conf->ping_timeout > 0) {
		irc->ping_source_id = b_timeout_add(global.conf->ping_interval * 1000, irc_userping, irc);
	}

	irc_connection_list = g_slist_append(irc_connection_list, irc);

	b = irc->b = bee_new();
	b->ui_data = irc;
	b->ui = &irc_ui_funcs;

	s = set_add(&b->set, "allow_takeover", "true", set_eval_bool, irc);
	s = set_add(&b->set, "away_devoice", "true", set_eval_bw_compat, irc);
	s->flags |= SET_HIDDEN;
	s = set_add(&b->set, "away_reply_timeout", "3600", set_eval_int, irc);
	s = set_add(&b->set, "charset", "utf-8", set_eval_charset, irc);
	s = set_add(&b->set, "default_target", "root", NULL, irc);
	s = set_add(&b->set, "display_namechanges", "false", set_eval_bool, irc);
	s = set_add(&b->set, "display_timestamps", "true", set_eval_bool, irc);
	s = set_add(&b->set, "handle_unknown", "add_channel", NULL, irc);
	s = set_add(&b->set, "last_version", "0", NULL, irc);
	s->flags |= SET_HIDDEN;
	s = set_add(&b->set, "lcnicks", "true", set_eval_bool, irc);
	s = set_add(&b->set, "nick_format", "%-@nick", NULL, irc);
	s = set_add(&b->set, "offline_user_quits", "true", set_eval_bool, irc);
	s = set_add(&b->set, "ops", "both", set_eval_irc_channel_ops, irc);
	s = set_add(&b->set, "paste_buffer", "false", set_eval_bool, irc);
	s->old_key = g_strdup("buddy_sendbuffer");
	s = set_add(&b->set, "paste_buffer_delay", "200", set_eval_int, irc);
	s->old_key = g_strdup("buddy_sendbuffer_delay");
	s = set_add(&b->set, "password", NULL, set_eval_password, irc);
	s->flags |= SET_NULL_OK | SET_PASSWORD;
	s = set_add(&b->set, "private", "true", set_eval_bool, irc);
	s = set_add(&b->set, "query_order", "lifo", NULL, irc);
	s = set_add(&b->set, "root_nick", ROOT_NICK, set_eval_root_nick, irc);
	s->flags |= SET_HIDDEN;
	s = set_add(&b->set, "show_offline", "false", set_eval_bw_compat, irc);
	s->flags |= SET_HIDDEN;
	s = set_add(&b->set, "simulate_netsplit", "true", set_eval_bool, irc);
	s = set_add(&b->set, "timezone", "local", set_eval_timezone, irc);
	s = set_add(&b->set, "to_char", ": ", set_eval_to_char, irc);
	s = set_add(&b->set, "typing_notice", "false", set_eval_bool, irc);
	s = set_add(&b->set, "utf8_nicks", "false", set_eval_utf8_nicks, irc);

	irc->root = iu = irc_user_new(irc, ROOT_NICK);
	iu->host = g_strdup(myhost);
	iu->fullname = g_strdup(ROOT_FN);
	iu->f = &irc_user_root_funcs;

	iu = irc_user_new(irc, NS_NICK);
	iu->host = g_strdup(myhost);
	iu->fullname = g_strdup(ROOT_FN);
	iu->f = &irc_user_root_funcs;

	irc->user = g_new0(irc_user_t, 1);
	irc->user->host = g_strdup(host);

	conf_loaddefaults(irc);

	/* Evaluator sets the iconv/oconv structures. */
	set_eval_charset(set_find(&b->set, "charset"), set_getstr(&b->set, "charset"));

	irc_write(irc, ":%s NOTICE * :%s", irc->root->host, "BitlBee-IRCd initialized, please go on");
	if (isatty(irc->fd)) {
		irc_write(irc, ":%s NOTICE * :%s", irc->root->host,
		          "If you read this, you most likely accidentally "
		          "started BitlBee in inetd mode on the command line. "
		          "You probably want to run it in (Fork)Daemon mode. "
		          "See doc/README for more information.");
	}

	g_free(myhost);
	g_free(host);

	/* libpurple doesn't like fork()s after initializing itself, so this
	   is the right moment to initialize it. */
#ifdef WITH_PURPLE
	nogaim_init();
#endif

	/* SSL library initialization also should be done after the fork, to
	   avoid shared CSPRNG state. This is required by NSS, which refuses to
	   work if a fork is detected */
	ssl_init();

	for (l = irc_plugins; l; l = l->next) {
		irc_plugin_t *p = l->data;
		if (p->irc_new) {
			p->irc_new(irc);
		}
	}

	return irc;
}

/* immed=1 makes this function pretty much equal to irc_free(), except that
   this one will "log". In case the connection is already broken and we
   shouldn't try to write to it. */
void irc_abort(irc_t *irc, int immed, char *format, ...)
{
	char *reason = NULL;

	if (format != NULL) {
		va_list params;

		va_start(params, format);
		reason = g_strdup_vprintf(format, params);
		va_end(params);
	}

	if (reason) {
		irc_write(irc, "ERROR :Closing link: %s", reason);
	}

	ipc_to_master_str("OPERMSG :Client exiting: %s@%s [%s]\r\n",
	                  irc->user->nick ? irc->user->nick : "(NONE)",
	                  irc->user->host, reason ? : "");

	g_free(reason);

	irc_flush(irc);
	if (immed) {
		irc_free(irc);
	} else {
		b_event_remove(irc->ping_source_id);
		irc->ping_source_id = b_timeout_add(1, (b_event_handler) irc_free, irc);
	}
}

static gboolean irc_free_hashkey(gpointer key, gpointer value, gpointer data);

void irc_free(irc_t * irc)
{
	GSList *l;

	irc->status |= USTATUS_SHUTDOWN;

	log_message(LOGLVL_INFO, "Destroying connection with fd %d", irc->fd);

	if (irc->status & USTATUS_IDENTIFIED && set_getbool(&irc->b->set, "save_on_quit")) {
		if (storage_save(irc, NULL, TRUE) != STORAGE_OK) {
			log_message(LOGLVL_WARNING, "Error while saving settings for user %s", irc->user->nick);
		}
	}

	for (l = irc_plugins; l; l = l->next) {
		irc_plugin_t *p = l->data;
		if (p->irc_free) {
			p->irc_free(irc);
		}
	}

	irc_connection_list = g_slist_remove(irc_connection_list, irc);

	while (irc->queries != NULL) {
		query_del(irc, irc->queries);
	}

	/* This is a little bit messy: bee_free() frees all b->users which
	   calls us back to free the corresponding irc->users. So do this
	   before we clear the remaining ones ourselves. */
	bee_free(irc->b);

	while (irc->users) {
		irc_user_free(irc, (irc_user_t *) irc->users->data);
	}

	while (irc->channels) {
		irc_channel_free(irc->channels->data);
	}

	if (irc->ping_source_id > 0) {
		b_event_remove(irc->ping_source_id);
	}
	if (irc->r_watch_source_id > 0) {
		b_event_remove(irc->r_watch_source_id);
	}
	if (irc->w_watch_source_id > 0) {
		b_event_remove(irc->w_watch_source_id);
	}

	closesocket(irc->fd);
	irc->fd = -1;

	g_hash_table_foreach_remove(irc->nick_user_hash, irc_free_hashkey, NULL);
	g_hash_table_destroy(irc->nick_user_hash);

	g_hash_table_foreach_remove(irc->watches, irc_free_hashkey, NULL);
	g_hash_table_destroy(irc->watches);

	if (irc->iconv != (GIConv) - 1) {
		g_iconv_close(irc->iconv);
	}
	if (irc->oconv != (GIConv) - 1) {
		g_iconv_close(irc->oconv);
	}

	g_free(irc->sendbuffer);
	g_free(irc->readbuffer);
	g_free(irc->password);

	g_free(irc);

	if (global.conf->runmode == RUNMODE_INETD ||
	    global.conf->runmode == RUNMODE_FORKDAEMON ||
	    (global.conf->runmode == RUNMODE_DAEMON &&
	     global.listen_socket == -1 &&
	     irc_connection_list == NULL)) {
		b_main_quit();
	}
}

static gboolean irc_free_hashkey(gpointer key, gpointer value, gpointer data)
{
	g_free(key);

	return(TRUE);
}

/* USE WITH CAUTION!
   Sets pass without checking */
void irc_setpass(irc_t *irc, const char *pass)
{
	g_free(irc->password);

	if (pass) {
		irc->password = g_strdup(pass);
	} else {
		irc->password = NULL;
	}
}

static char *set_eval_password(set_t *set, char *value)
{
	irc_t *irc = set->data;

	if (irc->status & USTATUS_IDENTIFIED && value) {
		irc_setpass(irc, value);
		return NULL;
	} else {
		return SET_INVALID;
	}
}

static char **irc_splitlines(char *buffer);

void irc_process(irc_t *irc)
{
	char **lines, *temp, **cmd;
	int i;

	if (irc->readbuffer != NULL) {
		lines = irc_splitlines(irc->readbuffer);

		for (i = 0; *lines[i] != '\0'; i++) {
			char *conv = NULL;

			/* [WvG] If the last line isn't empty, it's an incomplete line and we
			   should wait for the rest to come in before processing it. */
			if (lines[i + 1] == NULL) {
				temp = g_strdup(lines[i]);
				g_free(irc->readbuffer);
				irc->readbuffer = temp;
				i++;
				break;
			}

			if (irc->iconv != (GIConv) - 1) {
				gsize bytes_read, bytes_written;

				conv = g_convert_with_iconv(lines[i], -1, irc->iconv,
				                            &bytes_read, &bytes_written, NULL);

				if (conv == NULL || bytes_read != strlen(lines[i])) {
					/* GLib can do strange things if things are not in the expected charset,
					   so let's be a little bit paranoid here: */
					if (irc->status & USTATUS_LOGGED_IN) {
						irc_rootmsg(irc, "Error: Charset mismatch detected. The charset "
						            "setting is currently set to %s, so please make "
						            "sure your IRC client will send and accept text in "
						            "that charset, or tell BitlBee which charset to "
						            "expect by changing the charset setting. See "
						            "`help set charset' for more information. Your "
						            "message was ignored.",
						            set_getstr(&irc->b->set, "charset"));

						g_free(conv);
						conv = NULL;
					} else {
						irc_write(irc, ":%s NOTICE * :%s", irc->root->host,
						          "Warning: invalid characters received at login time.");

						conv = g_strdup(lines[i]);
						for (temp = conv; *temp; temp++) {
							if (*temp & 0x80) {
								*temp = '?';
							}
						}
					}
				}
				lines[i] = conv;
			}

			if (lines[i] && (cmd = irc_parse_line(lines[i]))) {
				irc_exec(irc, cmd);
				g_free(cmd);
			}

			g_free(conv);

			/* Shouldn't really happen, but just in case... */
			if (!g_slist_find(irc_connection_list, irc)) {
				g_free(lines);
				return;
			}
		}

		if (lines[i] != NULL) {
			g_free(irc->readbuffer);
			irc->readbuffer = NULL;
		}

		g_free(lines);
	}
}

/* Splits a long string into separate lines. The array is NULL-terminated
   and, unless the string contains an incomplete line at the end, ends with
   an empty string. Could use g_strsplit() but this one does it in-place.
   (So yes, it's destructive.) */
static char **irc_splitlines(char *buffer)
{
	int i, j, n = 3;
	char **lines;

	/* Allocate n+1 elements. */
	lines = g_new(char *, n + 1);

	lines[0] = buffer;

	/* Split the buffer in several strings, and accept any kind of line endings,
	 * knowing that ERC on Windows may send something interesting like \r\r\n,
	 * and surely there must be clients that think just \n is enough... */
	for (i = 0, j = 0; buffer[i] != '\0'; i++) {
		if (buffer[i] == '\r' || buffer[i] == '\n') {
			while (buffer[i] == '\r' || buffer[i] == '\n') {
				buffer[i++] = '\0';
			}

			lines[++j] = buffer + i;

			if (j >= n) {
				n *= 2;
				lines = g_renew(char *, lines, n + 1);
			}

			if (buffer[i] == '\0') {
				break;
			}
		}
	}

	/* NULL terminate our list. */
	lines[++j] = NULL;

	return lines;
}

/* Split an IRC-style line into little parts/arguments. */
char **irc_parse_line(char *line)
{
	int i, j;
	char **cmd;

	/* Move the line pointer to the start of the command, skipping spaces and the optional prefix. */
	if (line[0] == ':') {
		for (i = 0; line[i] && line[i] != ' '; i++) {
			;
		}
		line = line + i;
	}
	for (i = 0; line[i] == ' '; i++) {
		;
	}
	line = line + i;

	/* If we're already at the end of the line, return. If not, we're going to need at least one element. */
	if (line[0] == '\0') {
		return NULL;
	}

	/* Count the number of char **cmd elements we're going to need. */
	j = 1;
	for (i = 0; line[i] != '\0'; i++) {
		if (line[i] == ' ') {
			j++;

			if (line[i + 1] == ':') {
				break;
			}
		}
	}

	/* Allocate the space we need. */
	cmd = g_new(char *, j + 1);
	cmd[j] = NULL;

	/* Do the actual line splitting, format is:
	 * Input: "PRIVMSG #bitlbee :foo bar"
	 * Output: cmd[0]=="PRIVMSG", cmd[1]=="#bitlbee", cmd[2]=="foo bar", cmd[3]==NULL
	 */

	cmd[0] = line;
	for (i = 0, j = 0; line[i] != '\0'; i++) {
		if (line[i] == ' ') {
			line[i] = '\0';
			cmd[++j] = line + i + 1;

			if (line[i + 1] == ':') {
				cmd[j]++;
				break;
			}
		}
	}

	return cmd;
}

/* Converts such an array back into a command string. Mainly used for the IPC code right now. */
char *irc_build_line(char **cmd)
{
	int i, len;
	char *s;

	if (cmd[0] == NULL) {
		return NULL;
	}

	len = 1;
	for (i = 0; cmd[i]; i++) {
		len += strlen(cmd[i]) + 1;
	}

	if (strchr(cmd[i - 1], ' ') != NULL) {
		len++;
	}

	s = g_new0(char, len + 1);
	for (i = 0; cmd[i]; i++) {
		if (cmd[i + 1] == NULL && strchr(cmd[i], ' ') != NULL) {
			strcat(s, ":");
		}

		strcat(s, cmd[i]);

		if (cmd[i + 1]) {
			strcat(s, " ");
		}
	}
	strcat(s, "\r\n");

	return s;
}

void irc_write(irc_t *irc, char *format, ...)
{
	va_list params;

	va_start(params, format);
	irc_vawrite(irc, format, params);
	va_end(params);

	return;
}

void irc_write_all(int now, char *format, ...)
{
	va_list params;
	GSList *temp;

	va_start(params, format);

	temp = irc_connection_list;
	while (temp != NULL) {
		irc_t *irc = temp->data;

		if (now) {
			g_free(irc->sendbuffer);
			irc->sendbuffer = g_strdup("\r\n");
		}
		irc_vawrite(temp->data, format, params);
		if (now) {
			bitlbee_io_current_client_write(irc, irc->fd, B_EV_IO_WRITE);
		}
		temp = temp->next;
	}

	va_end(params);
	return;
}

void irc_vawrite(irc_t *irc, char *format, va_list params)
{
	int size;
	char line[IRC_MAX_LINE + 1];

	/* Don't try to write anything new anymore when shutting down. */
	if (irc->status & USTATUS_SHUTDOWN) {
		return;
	}

	memset(line, 0, sizeof(line));
	g_vsnprintf(line, IRC_MAX_LINE - 2, format, params);
	strip_newlines(line);

	if (irc->oconv != (GIConv) - 1) {
		gsize bytes_read, bytes_written;
		char *conv;

		conv = g_convert_with_iconv(line, -1, irc->oconv,
		                            &bytes_read, &bytes_written, NULL);

		if (bytes_read == strlen(line)) {
			strncpy(line, conv, IRC_MAX_LINE - 2);
		}

		g_free(conv);
	}
	g_strlcat(line, "\r\n", IRC_MAX_LINE + 1);

	if (irc->sendbuffer != NULL) {
		size = strlen(irc->sendbuffer) + strlen(line);
		irc->sendbuffer = g_renew(char, irc->sendbuffer, size + 1);
		strcpy((irc->sendbuffer + strlen(irc->sendbuffer)), line);
	} else {
		irc->sendbuffer = g_strdup(line);
	}

	if (irc->w_watch_source_id == 0) {
		/* If the buffer is empty we can probably write, so call the write event handler
		   immediately. If it returns TRUE, it should be called again, so add the event to
		   the queue. If it's FALSE, we emptied the buffer and saved ourselves some work
		   in the event queue. */
		/* Really can't be done as long as the code doesn't do error checking very well:
		if( bitlbee_io_current_client_write( irc, irc->fd, B_EV_IO_WRITE ) ) */

		/* So just always do it via the event handler. */
		irc->w_watch_source_id = b_input_add(irc->fd, B_EV_IO_WRITE, bitlbee_io_current_client_write, irc);
	}

	return;
}

/* Flush sendbuffer if you can. If it fails, fail silently and let some
   I/O event handler clean up. */
void irc_flush(irc_t *irc)
{
	ssize_t n;
	size_t len;

	if (irc->sendbuffer == NULL) {
		return;
	}

	len = strlen(irc->sendbuffer);
	if ((n = send(irc->fd, irc->sendbuffer, len, 0)) == len) {
		g_free(irc->sendbuffer);
		irc->sendbuffer = NULL;

		b_event_remove(irc->w_watch_source_id);
		irc->w_watch_source_id = 0;
	} else if (n > 0) {
		char *s = g_strdup(irc->sendbuffer + n);
		g_free(irc->sendbuffer);
		irc->sendbuffer = s;
	}
	/* Otherwise something went wrong and we don't currently care
	   what the error was. We may or may not succeed later, we
	   were just trying to flush the buffer immediately. */
}

/* Meant for takeover functionality. Transfer an IRC connection to a different
   socket. */
void irc_switch_fd(irc_t *irc, int fd)
{
	irc_write(irc, "ERROR :Transferring session to a new connection");
	irc_flush(irc);   /* Write it now or forget about it forever. */

	if (irc->sendbuffer) {
		b_event_remove(irc->w_watch_source_id);
		irc->w_watch_source_id = 0;
		g_free(irc->sendbuffer);
		irc->sendbuffer = NULL;
	}

	b_event_remove(irc->r_watch_source_id);
	closesocket(irc->fd);
	irc->fd = fd;
	irc->r_watch_source_id = b_input_add(irc->fd, B_EV_IO_READ, bitlbee_io_current_client_read, irc);
}

void irc_sync(irc_t *irc)
{
	GSList *l;

	irc_write(irc, ":%s!%s@%s MODE %s :+%s", irc->user->nick,
	          irc->user->user, irc->user->host, irc->user->nick,
	          irc->umode);

	for (l = irc->channels; l; l = l->next) {
		irc_channel_t *ic = l->data;
		if (ic->flags & IRC_CHANNEL_JOINED) {
			irc_send_join(ic, irc->user);
		}
	}

	/* We may be waiting for a PONG from the previous client connection. */
	irc->pinging = FALSE;
}

void irc_desync(irc_t *irc)
{
	GSList *l;

	for (l = irc->channels; l; l = l->next) {
		irc_channel_del_user(l->data, irc->user, IRC_CDU_KICK,
		                     "Switching to old session");
	}

	irc_write(irc, ":%s!%s@%s MODE %s :-%s", irc->user->nick,
	          irc->user->user, irc->user->host, irc->user->nick,
	          irc->umode);
}

int irc_check_login(irc_t *irc)
{
	if (irc->user->user && irc->user->nick) {
		if (global.conf->authmode == AUTHMODE_CLOSED && !(irc->status & USTATUS_AUTHORIZED)) {
			irc_send_num(irc, 464, ":This server is password-protected.");
			return 0;
		} else {
			irc_channel_t *ic;
			irc_user_t *iu = irc->user;

			irc->user = irc_user_new(irc, iu->nick);
			irc->user->user = iu->user;
			irc->user->host = iu->host;
			irc->user->fullname = iu->fullname;
			irc->user->f = &irc_user_self_funcs;
			g_free(iu->nick);
			g_free(iu);

			if (global.conf->runmode == RUNMODE_FORKDAEMON || global.conf->runmode == RUNMODE_DAEMON) {
				ipc_to_master_str("CLIENT %s %s :%s\r\n", irc->user->host, irc->user->nick,
				                  irc->user->fullname);
			}

			irc->status |= USTATUS_LOGGED_IN;

			irc_send_login(irc);

			irc->umode[0] = '\0';
			irc_umode_set(irc, "+" UMODE, TRUE);

			ic = irc->default_channel = irc_channel_new(irc, ROOT_CHAN);
			irc_channel_set_topic(ic, CONTROL_TOPIC, irc->root);
			set_setstr(&ic->set, "auto_join", "true");
			irc_channel_auto_joins(irc, NULL);

			irc->root->last_channel = irc->default_channel;

			irc_rootmsg(irc,
			            "Welcome to the BitlBee gateway!\n\n"
			            "Running %s %s\n\n"
			            "If you've never used BitlBee before, please do read the help "
			            "information using the \x02help\x02 command. Lots of FAQs are "
			            "answered there.\n"
			            "If you already have an account on this server, just use the "
			            "\x02identify\x02 command to identify yourself.",
			            PACKAGE, BITLBEE_VERSION);

			/* This is for bug #209 (use PASS to identify to NickServ). */
			if (irc->password != NULL) {
				char *send_cmd[] = { "identify", g_strdup(irc->password), NULL };

				irc_setpass(irc, NULL);
				root_command(irc, send_cmd);
				g_free(send_cmd[1]);
			}

			return 1;
		}
	} else {
		/* More information needed. */
		return 0;
	}
}

/* TODO: This is a mess, but this function is a bit too complicated to be
   converted to something more generic. */
void irc_umode_set(irc_t *irc, const char *s, gboolean allow_priv)
{
	/* allow_priv: Set to 0 if s contains user input, 1 if you want
	   to set a "privileged" mode (+o, +R, etc). */
	char m[128], st = 1;
	const char *t;
	int i;
	char changes[512], st2 = 2;
	char badflag = 0;

	memset(m, 0, sizeof(m));

	/* Keep track of which modes are enabled in this array. */
	for (t = irc->umode; *t; t++) {
		if (*t < sizeof(m)) {
			m[(int) *t] = 1;
		}
	}

	i = 0;
	for (t = s; *t && i < sizeof(changes) - 3; t++) {
		if (*t == '+' || *t == '-') {
			st = *t == '+';
		} else if ((st == 0 && (!strchr(UMODES_KEEP, *t) || allow_priv)) ||
		           (st == 1 && strchr(UMODES, *t)) ||
		           (st == 1 && allow_priv && strchr(UMODES_PRIV, *t))) {
			if (m[(int) *t] != st) {
				/* If we're actually making a change, remember this
				   for the response. */
				if (st != st2) {
					st2 = st, changes[i++] = st ? '+' : '-';
				}
				changes[i++] = *t;
			}
			m[(int) *t] = st;
		} else {
			badflag = 1;
		}
	}
	changes[i] = '\0';

	/* Convert the m array back into an umode string. */
	memset(irc->umode, 0, sizeof(irc->umode));
	for (i = 'A'; i <= 'z' && strlen(irc->umode) < (sizeof(irc->umode) - 1); i++) {
		if (m[i]) {
			irc->umode[strlen(irc->umode)] = i;
		}
	}

	if (badflag) {
		irc_send_num(irc, 501, ":Unknown MODE flag");
	}
	if (*changes) {
		irc_write(irc, ":%s!%s@%s MODE %s :%s", irc->user->nick,
		          irc->user->user, irc->user->host, irc->user->nick,
		          changes);
	}
}

/* Returns 0 if everything seems to be okay, a number >0 when there was a
   timeout. The number returned is the number of seconds we received no
   pongs from the user. When not connected yet, we don't ping but drop the
   connection when the user fails to connect in IRC_LOGIN_TIMEOUT secs. */
static gboolean irc_userping(gpointer _irc, gint fd, b_input_condition cond)
{
	double now = gettime();
	irc_t *irc = _irc;
	int fail = 0;

	if (!(irc->status & USTATUS_LOGGED_IN)) {
		if (now > (irc->last_pong + IRC_LOGIN_TIMEOUT)) {
			fail = now - irc->last_pong;
		}
	} else {
		if (now > (irc->last_pong + global.conf->ping_timeout)) {
			fail = now - irc->last_pong;
		} else {
			irc_write(irc, "PING :%s", IRC_PING_STRING);
		}
	}

	if (fail > 0) {
		irc_abort(irc, 0, "Ping Timeout: %d seconds", fail);
		return FALSE;
	}

	return TRUE;
}

static char *set_eval_charset(set_t *set, char *value)
{
	irc_t *irc = (irc_t *) set->data;
	char *test;
	gsize test_bytes = 0;
	GIConv ic, oc;

	if (g_strcasecmp(value, "none") == 0) {
		value = g_strdup("utf-8");
	}

	if ((oc = g_iconv_open(value, "utf-8")) == (GIConv) - 1) {
		return NULL;
	}

	/* Do a test iconv to see if the user picked an IRC-compatible
	   charset (for example utf-16 goes *horribly* wrong). */
	if ((test = g_convert_with_iconv(" ", 1, oc, NULL, &test_bytes, NULL)) == NULL ||
	    test_bytes > 1) {
		g_free(test);
		g_iconv_close(oc);
		irc_rootmsg(irc, "Unsupported character set: The IRC protocol "
		            "only supports 8-bit character sets.");
		return NULL;
	}
	g_free(test);

	if ((ic = g_iconv_open("utf-8", value)) == (GIConv) - 1) {
		g_iconv_close(oc);
		return NULL;
	}

	if (irc->iconv != (GIConv) - 1) {
		g_iconv_close(irc->iconv);
	}
	if (irc->oconv != (GIConv) - 1) {
		g_iconv_close(irc->oconv);
	}

	irc->iconv = ic;
	irc->oconv = oc;

	return value;
}

/* Mostly meant for upgrades. If one of these is set to the non-default,
   set show_users of all channels to something with the same effect. */
static char *set_eval_bw_compat(set_t *set, char *value)
{
	irc_t *irc = set->data;
	char *val;
	GSList *l;

	irc_rootmsg(irc, "Setting `%s' is obsolete, use the `show_users' "
	            "channel setting instead.", set->key);

	if (strcmp(set->key, "away_devoice") == 0 && !bool2int(value)) {
		val = "online,special%,away";
	} else if (strcmp(set->key, "show_offline") == 0 && bool2int(value)) {
		val = "online@,special%,away+,offline";
	} else {
		val = "online+,special%,away";
	}

	for (l = irc->channels; l; l = l->next) {
		irc_channel_t *ic = l->data;
		/* No need to check channel type, if the setting doesn't exist it
		   will just be ignored. */
		set_setstr(&ic->set, "show_users", val);
	}

	return SET_INVALID;
}

static char *set_eval_utf8_nicks(set_t *set, char *value)
{
	irc_t *irc = set->data;
	gboolean val = bool2int(value);

	/* Do *NOT* unset this flag in the middle of a session. There will
	   be UTF-8 nicks around already so if we suddenly disable support
	   for them, various functions might behave strangely. */
	if (val) {
		irc->status |= IRC_UTF8_NICKS;
	} else if (irc->status & IRC_UTF8_NICKS) {
		irc_rootmsg(irc, "You need to reconnect to BitlBee for this "
		            "change to take effect.");
	}

	return set_eval_bool(set, value);
}

void register_irc_plugin(const struct irc_plugin *p)
{
	irc_plugins = g_slist_prepend(irc_plugins, (gpointer) p);
}
