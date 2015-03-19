/********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2012 Wilmer van der Gaast and others                *
  \********************************************************************/

/* MSN module - Notification server callbacks                           */

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

#include <ctype.h>
#include <sys/utsname.h>
#include "nogaim.h"
#include "msn.h"
#include "md5.h"
#include "sha1.h"
#include "soap.h"
#include "xmltree.h"

static gboolean msn_ns_connected(gpointer data, gint source, b_input_condition cond);
static gboolean msn_ns_callback(gpointer data, gint source, b_input_condition cond);

static void msn_ns_send_adl_start(struct im_connection *ic);
static void msn_ns_send_adl(struct im_connection *ic);

int msn_ns_write(struct im_connection *ic, int fd, const char *fmt, ...)
{
	struct msn_data *md = ic->proto_data;
	va_list params;
	char *out;
	size_t len;
	int st;

	va_start(params, fmt);
	out = g_strdup_vprintf(fmt, params);
	va_end(params);

	if (fd < 0) {
		fd = md->fd;
	}

	if (getenv("BITLBEE_DEBUG")) {
		fprintf(stderr, "\x1b[91m>>>[NS%d] %s\n\x1b[97m", fd, out);
	}

	len = strlen(out);

	if (md->is_http) {
		st = len;
		msn_gw_write(md->gw, out, len);
	} else {
		st = write(fd, out, len);
	}

	g_free(out);
	if (st != len) {
		imcb_error(ic, "Short write() to main server");
		imc_logout(ic, TRUE);
		return 0;
	}

	return 1;
}

gboolean msn_ns_connect(struct im_connection *ic, const char *host, int port)
{
	struct msn_data *handler = ic->proto_data;

	if (handler->fd >= 0) {
		closesocket(handler->fd);
	}

	if (handler->is_http) {
		handler->gw = msn_gw_new(handler);
		handler->gw->callback = msn_ns_callback;
		msn_ns_connected(handler, -1, B_EV_IO_READ);
	} else {
		handler->fd = proxy_connect(host, port, msn_ns_connected, handler);
		if (handler->fd < 0) {
			imcb_error(ic, "Could not connect to server");
			imc_logout(ic, TRUE);
			return FALSE;
		}
	}

	return TRUE;
}

static gboolean msn_ns_connected(gpointer data, gint source, b_input_condition cond)
{
	struct msn_data *md = data;
	struct msn_data *handler = md;
	struct im_connection *ic = md->ic;

	if (source == -1 && !md->is_http) {
		imcb_error(ic, "Could not connect to server");
		imc_logout(ic, TRUE);
		return FALSE;
	}

	g_free(handler->rxq);
	handler->rxlen = 0;
	handler->rxq = g_new0(char, 1);

	if (md->uuid == NULL) {
		struct utsname name;
		sha1_state_t sha[1];

		/* UUID == SHA1("BitlBee" + my hostname + MSN username) */
		sha1_init(sha);
		sha1_append(sha, (void *) "BitlBee", 7);
		if (uname(&name) == 0) {
			sha1_append(sha, (void *) name.nodename, strlen(name.nodename));
		}
		sha1_append(sha, (void *) ic->acc->user, strlen(ic->acc->user));
		md->uuid = sha1_random_uuid(sha);
		memcpy(md->uuid, "b171be3e", 8);   /* :-P */
	}

	if (msn_ns_write(ic, source, "VER %d %s CVR0\r\n", ++md->trId, MSNP_VER)) {
		if (!handler->is_http) {
			handler->inpa = b_input_add(handler->fd, B_EV_IO_READ, msn_ns_callback, handler);
		}
		imcb_log(ic, "Connected to server, waiting for reply");
	}

	return FALSE;
}

void msn_ns_close(struct msn_data *handler)
{
	if (handler->gw) {
		if (handler->gw->waiting) {
			/* mark it as closed, let the request callback clean it */
			handler->gw->open = FALSE;
		} else {
			msn_gw_free(handler->gw);
		}
	}
	if (handler->fd >= 0) {
		closesocket(handler->fd);
		b_event_remove(handler->inpa);
	}

	handler->fd = handler->inpa = -1;
	g_free(handler->rxq);
	g_free(handler->cmd_text);

	handler->rxlen = 0;
	handler->rxq = NULL;
	handler->cmd_text = NULL;
}

static gboolean msn_ns_callback(gpointer data, gint source, b_input_condition cond)
{
	struct msn_data *handler = data;
	struct im_connection *ic = handler->ic;
	char *bytes;
	int st;

	if (handler->is_http) {
		st = msn_gw_read(handler->gw, &bytes);
	} else {
		bytes = g_malloc(1024);
		st = read(handler->fd, bytes, 1024);
	}

	if (st <= 0) {
		imcb_error(ic, "Error while reading from server");
		imc_logout(ic, TRUE);
		return FALSE;
	}

	msn_queue_feed(handler, bytes, st);

	g_free(bytes);

	/* Ignore ret == 0, it's already disconnected then. */
	msn_handler(handler);

	return TRUE;
	
}

int msn_ns_command(struct msn_data *handler, char **cmd, int num_parts)
{
	struct im_connection *ic = handler->ic;
	struct msn_data *md = handler;

	if (num_parts == 0) {
		/* Hrrm... Empty command...? Ignore? */
		return(1);
	}

	if (strcmp(cmd[0], "VER") == 0) {
		if (cmd[2] && strncmp(cmd[2], MSNP_VER, 5) != 0) {
			imcb_error(ic, "Unsupported protocol");
			imc_logout(ic, FALSE);
			return(0);
		}

		return(msn_ns_write(ic, handler->fd, "CVR %d 0x0409 mac 10.2.0 ppc macmsgs 3.5.1 macmsgs %s VmVyc2lvbjogMQ0KWGZyQ291bnQ6IDINClhmclNlbnRVVENUaW1lOiA2MzU2MTQ3OTU5NzgzOTAwMDANCklzR2VvWGZyOiB0cnVlDQo=\r\n",
		                    ++md->trId, ic->acc->user));
	} else if (strcmp(cmd[0], "CVR") == 0) {
		/* We don't give a damn about the information we just received */
		return msn_ns_write(ic, handler->fd, "USR %d SSO I %s\r\n", ++md->trId, ic->acc->user);
	} else if (strcmp(cmd[0], "XFR") == 0) {
		char *server;
		int port;

		if (num_parts >= 6 && strcmp(cmd[2], "NS") == 0) {
			b_event_remove(handler->inpa);
			handler->inpa = -1;

			server = strchr(cmd[3], ':');
			if (!server) {
				imcb_error(ic, "Syntax error");
				imc_logout(ic, TRUE);
				return(0);
			}
			*server = 0;
			port = atoi(server + 1);
			server = cmd[3];

			imcb_log(ic, "Transferring to other server");
			return msn_ns_connect(ic, server, port);
		} else {
			imcb_error(ic, "Syntax error");
			imc_logout(ic, TRUE);
			return(0);
		}
	} else if (strcmp(cmd[0], "USR") == 0) {
		if (num_parts >= 6 && strcmp(cmd[2], "SSO") == 0 &&
		    strcmp(cmd[3], "S") == 0) {
			g_free(md->pp_policy);
			md->pp_policy = g_strdup(cmd[4]);
			msn_soap_passport_sso_request(ic, cmd[5]);
		} else if (strcmp(cmd[2], "OK") == 0) {
			/* If the number after the handle is 0, the e-mail
			   address is unverified, which means we can't change
			   the display name. */
			if (cmd[4][0] == '0') {
				md->flags |= MSN_EMAIL_UNVERIFIED;
			}

			imcb_log(ic, "Authenticated, getting buddy list");
			msn_soap_memlist_request(ic);
		} else {
			imcb_error(ic, "Unknown authentication type");
			imc_logout(ic, FALSE);
			return(0);
		}
	} else if (strcmp(cmd[0], "MSG") == 0) {
		if (num_parts < 4) {
			imcb_error(ic, "Syntax error");
			imc_logout(ic, TRUE);
			return(0);
		}

		handler->msglen = atoi(cmd[3]);

		if (handler->msglen <= 0) {
			imcb_error(ic, "Syntax error");
			imc_logout(ic, TRUE);
			return(0);
		}
	} else if (strcmp(cmd[0], "ADL") == 0) {
		if (num_parts >= 3 && strcmp(cmd[2], "OK") == 0) {
			msn_ns_send_adl(ic);
			return msn_ns_finish_login(ic);
		} else if (num_parts >= 3) {
			handler->msglen = atoi(cmd[2]);
		}
	} else if (strcmp(cmd[0], "CHL") == 0) {
		char *resp;
		int st;

		if (num_parts < 3) {
			imcb_error(ic, "Syntax error");
			imc_logout(ic, TRUE);
			return(0);
		}

		resp = msn_p11_challenge(cmd[2]);

		st =  msn_ns_write(ic, -1, "QRY %d %s %zd\r\n%s",
		                   ++md->trId, MSNP11_PROD_ID,
		                   strlen(resp), resp);
		g_free(resp);
		return st;
	} else if (strcmp(cmd[0], "QRY") == 0) {
		/* CONGRATULATIONS */
	} else if (strcmp(cmd[0], "OUT") == 0) {
		int allow_reconnect = TRUE;

		if (cmd[1] && strcmp(cmd[1], "OTH") == 0) {
			imcb_error(ic, "Someone else logged in with your account");
			allow_reconnect = FALSE;
		} else if (cmd[1] && strcmp(cmd[1], "SSD") == 0) {
			imcb_error(ic, "Terminating session because of server shutdown");
		} else {
			imcb_error(ic, "Session terminated by remote server (%s)",
			           cmd[1] ? cmd[1] : "reason unknown)");
		}

		imc_logout(ic, allow_reconnect);
		return(0);
	} else if (strcmp(cmd[0], "GCF") == 0) {
		/* Coming up is cmd[2] bytes of stuff we're supposed to
		   censore. Meh. */
		handler->msglen = atoi(cmd[2]);
	} else if ((strcmp(cmd[0], "NFY") == 0) || (strcmp(cmd[0], "SDG") == 0)) {
		if (num_parts >= 3) {
			handler->msglen = atoi(cmd[2]);
		}
	} else if (strcmp(cmd[0], "PUT") == 0) {
		if (num_parts >= 4) {
			handler->msglen = atoi(cmd[3]);
		}
	} else if (strcmp(cmd[0], "QNG") == 0) {
		ic->flags |= OPT_PONGED;
	} else if (g_ascii_isdigit(cmd[0][0])) {
		int num = atoi(cmd[0]);
		const struct msn_status_code *err = msn_status_by_number(num);

		imcb_error(ic, "Error reported by MSN server: %s", err->text);

		if (err->flags & STATUS_FATAL) {
			imc_logout(ic, TRUE);
			return(0);
		}

		/* Oh yes, errors can have payloads too now. Discard them for now. */
		if (num_parts >= 3) {
			handler->msglen = atoi(cmd[2]);
		}
	} else {
		imcb_error(ic, "Received unknown command from main server: %s", cmd[0]);
	}

	return(1);
}

int msn_ns_message(struct msn_data *handler, char *msg, int msglen, char **cmd, int num_parts)
{
	struct im_connection *ic = handler->ic;
	char *body;
	int blen = 0;

	if (!num_parts) {
		return(1);
	}

	if ((body = strstr(msg, "\r\n\r\n"))) {
		body += 4;
		blen = msglen - (body - msg);
	}

	if (strcmp(cmd[0], "MSG") == 0) {
		if (g_strcasecmp(cmd[1], "Hotmail") == 0) {
			char *ct = get_rfc822_header(msg, "Content-Type:", msglen);

			if (!ct) {
				return(1);
			}

			if (g_strncasecmp(ct, "application/x-msmsgssystemmessage", 33) == 0) {
				char *mtype;
				char *arg1;

				if (!body) {
					return(1);
				}

				mtype = get_rfc822_header(body, "Type:", blen);
				arg1 = get_rfc822_header(body, "Arg1:", blen);

				if (mtype && strcmp(mtype, "1") == 0) {
					if (arg1) {
						imcb_log(ic, "The server is going down for maintenance in %s minutes.",
						         arg1);
					}
				}

				g_free(arg1);
				g_free(mtype);
			} else if (g_strncasecmp(ct, "text/x-msmsgsprofile", 20) == 0) {
				/* We don't care about this profile for now... */
			} else if (g_strncasecmp(ct, "text/x-msmsgsinitialemailnotification", 37) == 0) {
				if (set_getbool(&ic->acc->set, "mail_notifications")) {
					char *inbox = get_rfc822_header(body, "Inbox-Unread:", blen);
					char *folders = get_rfc822_header(body, "Folders-Unread:", blen);

					if (inbox && folders) {
						imcb_log(ic,
						         "INBOX contains %s new messages, plus %s messages in other folders.", inbox,
						         folders);
					}

					g_free(inbox);
					g_free(folders);
				}
			} else if (g_strncasecmp(ct, "text/x-msmsgsemailnotification", 30) == 0) {
				if (set_getbool(&ic->acc->set, "mail_notifications")) {
					char *from = get_rfc822_header(body, "From-Addr:", blen);
					char *fromname = get_rfc822_header(body, "From:", blen);

					if (from && fromname) {
						imcb_log(ic, "Received an e-mail message from %s <%s>.", fromname,
						         from);
					}

					g_free(from);
					g_free(fromname);
				}
			} else if (g_strncasecmp(ct, "text/x-msmsgsactivemailnotification", 35) == 0) {
				/* Notification that a message has been read... Ignore it */
			} else {
				debug("Can't handle %s packet from notification server", ct);
			}

			g_free(ct);
		}
	} else if (strcmp(cmd[0], "ADL") == 0) {
		struct xt_node *adl, *d, *c;

		if (!(adl = xt_from_string(msg, msglen))) {
			return 1;
		}

		for (d = adl->children; d; d = d->next) {
			char *dn;
			if (strcmp(d->name, "d") != 0 ||
			    (dn = xt_find_attr(d, "n")) == NULL) {
				continue;
			}
			for (c = d->children; c; c = c->next) {
				bee_user_t *bu;
				struct msn_buddy_data *bd;
				char *cn, *handle, *f, *l;
				int flags;

				if (strcmp(c->name, "c") != 0 ||
				    (l = xt_find_attr(c, "l")) == NULL ||
				    (cn = xt_find_attr(c, "n")) == NULL) {
					continue;
				}

				/* FIXME: Use "t" here, guess I should just add it
				   as a prefix like elsewhere in the protocol. */
				handle = g_strdup_printf("%s@%s", cn, dn);
				if (!((bu = bee_user_by_handle(ic->bee, ic, handle)) ||
				      (bu = bee_user_new(ic->bee, ic, handle, 0)))) {
					g_free(handle);
					continue;
				}
				g_free(handle);
				bd = bu->data;

				if ((f = xt_find_attr(c, "f"))) {
					http_decode(f);
					imcb_rename_buddy(ic, bu->handle, f);
				}

				flags = atoi(l) & 15;
				if (bd->flags != flags) {
					bd->flags = flags;
					msn_buddy_ask(bu);
				}
			}
		}
	} else if (strcmp(cmd[0], "SDG") == 0) {
		char **parts = g_strsplit(msg, "\r\n\r\n", 4);
		char *from = NULL;
		char *mt = NULL;
		char *who = NULL;
		char *s = NULL;

		if ((from = get_rfc822_header(parts[0], "From", 0)) &&
		    (mt = get_rfc822_header(parts[2], "Message-Type", 0)) &&
		    (s = strchr(from, ';'))) {

			who = g_strndup(from + 2, s - from - 2);

			if (strcmp(mt, "Control/Typing") == 0) {
				imcb_buddy_typing(ic, who, OPT_TYPING);
			} else if (strcmp(mt, "Text") == 0) {
				imcb_buddy_msg(ic, who, parts[3], 0, 0);
			}
		}
		g_free(from);
		g_free(mt);
		g_free(who);
		return 1;
	}

	return 1;
}

void msn_auth_got_passport_token(struct im_connection *ic, const char *token, const char *error)
{
	struct msn_data *md;

	/* Dead connection? */
	if (g_slist_find(msn_connections, ic) == NULL) {
		return;
	}

	md = ic->proto_data;

	if (token) {
		msn_ns_write(ic, -1, "USR %d SSO S %s %s {%s}\r\n", ++md->trId, md->tokens[0], token, md->uuid);
	} else {
		imcb_error(ic, "Error during Passport authentication: %s", error);
		imc_logout(ic, TRUE);
	}
}

void msn_auth_got_contact_list(struct im_connection *ic)
{
	/* Dead connection? */
	if (g_slist_find(msn_connections, ic) == NULL) {
		return;
	}

	msn_ns_send_adl_start(ic);
	msn_ns_finish_login(ic);
}

static gboolean msn_ns_send_adl_1(gpointer key, gpointer value, gpointer data)
{
	struct xt_node *adl = data, *d, *c, *s;
	struct bee_user *bu = value;
	struct msn_buddy_data *bd = bu->data;
	struct msn_data *md = bu->ic->proto_data;
	char handle[strlen(bu->handle) + 1];
	char *domain;
	char l[4];

	if ((bd->flags & 7) == 0 || (bd->flags & MSN_BUDDY_ADL_SYNCED)) {
		return FALSE;
	}

	strcpy(handle, bu->handle);
	if ((domain = strchr(handle, '@')) == NULL) {    /* WTF */
		return FALSE;
	}
	*domain = '\0';
	domain++;

	if ((d = adl->children) == NULL ||
	    g_strcasecmp(xt_find_attr(d, "n"), domain) != 0) {
		d = xt_new_node("d", NULL, NULL);
		xt_add_attr(d, "n", domain);
		xt_insert_child(adl, d);
	}

	g_snprintf(l, sizeof(l), "%d", bd->flags & 7);
	c = xt_new_node("c", NULL, NULL);
	xt_add_attr(c, "n", handle);
	xt_add_attr(c, "t", "1");   /* FIXME: Network type, i.e. 32 for Y!MSG */
	s = xt_new_node("s", NULL, NULL);
	xt_add_attr(s, "n", "IM");
	xt_add_attr(s, "l", l);
	xt_insert_child(c, s);
	xt_insert_child(d, c);

	/* Do this in batches of 100. */
	bd->flags |= MSN_BUDDY_ADL_SYNCED;
	return (--md->adl_todo % 140) == 0;
}

static void msn_ns_send_adl(struct im_connection *ic)
{
	struct xt_node *adl;
	struct msn_data *md = ic->proto_data;
	char *adls;

	adl = xt_new_node("ml", NULL, NULL);
	xt_add_attr(adl, "l", "1");
	g_tree_foreach(md->domaintree, msn_ns_send_adl_1, adl);
	if (adl->children == NULL) {
		/* This tells the caller that we're done now. */
		md->adl_todo = -1;
		xt_free_node(adl);
		return;
	}

	adls = xt_to_string(adl);
	xt_free_node(adl);
	msn_ns_write(ic, -1, "ADL %d %zd\r\n%s", ++md->trId, strlen(adls), adls);
	g_free(adls);
}

static void msn_ns_send_adl_start(struct im_connection *ic)
{
	struct msn_data *md;
	GSList *l;

	/* Dead connection? */
	if (g_slist_find(msn_connections, ic) == NULL) {
		return;
	}

	md = ic->proto_data;
	md->adl_todo = 0;
	for (l = ic->bee->users; l; l = l->next) {
		bee_user_t *bu = l->data;
		struct msn_buddy_data *bd = bu->data;

		if (bu->ic != ic || (bd->flags & 7) == 0) {
			continue;
		}

		bd->flags &= ~MSN_BUDDY_ADL_SYNCED;
		md->adl_todo++;
	}

	msn_ns_send_adl(ic);
}

int msn_ns_finish_login(struct im_connection *ic)
{
	struct msn_data *md = ic->proto_data;

	if (ic->flags & OPT_LOGGED_IN) {
		return 1;
	}

	if (md->adl_todo < 0) {
		md->flags |= MSN_DONE_ADL;
	}

	if ((md->flags & MSN_DONE_ADL) && (md->flags & MSN_GOT_PROFILE)) {
		imcb_connected(ic);
	}

	return 1;
}

// TODO: typing notifications, nudges lol, etc
int msn_ns_sendmessage(struct im_connection *ic, bee_user_t *bu, const char *text)
{
	struct msn_data *md = ic->proto_data;
	int retval = 0;
	char *buf;

	if (strncmp(text, "\r\r\r", 3) == 0) {
		/* Err. Shouldn't happen but I guess it can. Don't send others
		   any of the "SHAKE THAT THING" messages. :-D */
		return 1;
	}

	buf = g_strdup_printf(MSN_MESSAGE_HEADERS, bu->handle, ic->acc->user, md->uuid, strlen(text), text);
	retval = msn_ns_write(ic, -1, "SDG %d %zd\r\n%s", ++md->trId, strlen(buf), buf);
	g_free(buf);
	return retval;
}
