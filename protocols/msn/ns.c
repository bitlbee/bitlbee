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
static void msn_ns_structured_message(struct msn_data *md, char *msg, int msglen, char **cmd);
static void msn_ns_sdg(struct msn_data *md, char *who, char **parts, char *action, gboolean selfmessage);
static void msn_ns_nfy(struct msn_data *md, char *who, char **parts, char *action, gboolean is_put);

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
	struct msn_data *md = ic->proto_data;

	if (md->fd >= 0) {
		closesocket(md->fd);
	}

	if (md->is_http) {
		md->gw = msn_gw_new(ic);
		md->gw->callback = msn_ns_callback;
		msn_ns_connected(md, -1, B_EV_IO_READ);
	} else {
		md->fd = proxy_connect(host, port, msn_ns_connected, md);
		if (md->fd < 0) {
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
	struct im_connection *ic = md->ic;

	/* this should be taken from XFR, but hardcoding it for now. it also prevents more redirects. */
	const char *redir_data = "VmVyc2lvbjogMQ0KWGZyQ291bnQ6IDINCklzR2VvWGZyOiB0cnVlDQo=";

	if (source == -1 && !md->is_http) {
		imcb_error(ic, "Could not connect to server");
		imc_logout(ic, TRUE);
		return FALSE;
	}

	g_free(md->rxq);
	md->rxlen = 0;
	md->rxq = g_new0(char, 1);

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

	/* Having to handle potential errors in each write sure makes these ifs awkward...*/

	if (msn_ns_write(ic, source, "VER %d %s CVR0\r\n", ++md->trId, MSNP_VER) &&
	    msn_ns_write(ic, source, "CVR %d 0x0409 mac 10.2.0 ppc macmsgs 3.5.1 macmsgs %s %s\r\n",
	                 ++md->trId, ic->acc->user, redir_data) &&
	    msn_ns_write(ic, md->fd, "USR %d SSO I %s\r\n", ++md->trId, ic->acc->user)) {

		if (!md->is_http) {
			md->inpa = b_input_add(md->fd, B_EV_IO_READ, msn_ns_callback, md);
		}
		imcb_log(ic, "Connected to server, waiting for reply");
	}

	return FALSE;
}

void msn_ns_close(struct msn_data *md)
{
	if (md->gw) {
		msn_gw_free(md->gw);
	}
	if (md->fd >= 0) {
		closesocket(md->fd);
		b_event_remove(md->inpa);
	}

	md->fd = md->inpa = -1;
	g_free(md->rxq);
	g_free(md->cmd_text);

	md->rxlen = 0;
	md->rxq = NULL;
	md->cmd_text = NULL;
}

static gboolean msn_ns_callback(gpointer data, gint source, b_input_condition cond)
{
	struct msn_data *md = data;
	struct im_connection *ic = md->ic;
	char *bytes;
	int st;

	if (md->is_http) {
		st = msn_gw_read(md->gw, &bytes);
	} else {
		bytes = g_malloc(1024);
		st = read(md->fd, bytes, 1024);
	}

	if (st <= 0) {
		imcb_error(ic, "Error while reading from server");
		imc_logout(ic, TRUE);
		g_free(bytes);
		return FALSE;
	}

	msn_queue_feed(md, bytes, st);

	g_free(bytes);

	return msn_handler(md);
}

int msn_ns_command(struct msn_data *md, char **cmd, int num_parts)
{
	struct im_connection *ic = md->ic;

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

	} else if (strcmp(cmd[0], "CVR") == 0) {
		/* We don't give a damn about the information we just received */
	} else if (strcmp(cmd[0], "XFR") == 0) {
		char *server;
		int port;

		if (num_parts >= 6 && strcmp(cmd[2], "NS") == 0) {
			b_event_remove(md->inpa);
			md->inpa = -1;

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

		md->msglen = atoi(cmd[3]);

		if (md->msglen <= 0) {
			imcb_error(ic, "Syntax error");
			imc_logout(ic, TRUE);
			return(0);
		}
	} else if (strcmp(cmd[0], "ADL") == 0) {
		if (num_parts >= 3 && strcmp(cmd[2], "OK") == 0) {
			msn_ns_send_adl(ic);
			return msn_ns_finish_login(ic);
		} else if (num_parts >= 3) {
			md->msglen = atoi(cmd[2]);
		}
	} else if (strcmp(cmd[0], "RML") == 0) {
		/* Move along, nothing to see here */
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
		imcb_error(ic, "Session terminated by remote server (%s)", cmd[1] ? cmd[1] : "reason unknown");
		imc_logout(ic, TRUE);
		return(0);
	} else if (strcmp(cmd[0], "GCF") == 0) {
		/* Coming up is cmd[2] bytes of stuff we're supposed to
		   censore. Meh. */
		md->msglen = atoi(cmd[2]);
	} else if ((strcmp(cmd[0], "NFY") == 0) || (strcmp(cmd[0], "SDG") == 0)) {
		if (num_parts >= 3) {
			md->msglen = atoi(cmd[2]);
		}
	} else if (strcmp(cmd[0], "PUT") == 0) {
		if (num_parts >= 4) {
			md->msglen = atoi(cmd[3]);
		}
	} else if (strcmp(cmd[0], "NOT") == 0) {
		if (num_parts >= 2) {
			md->msglen = atoi(cmd[1]);
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
			md->msglen = atoi(cmd[2]);
		}
	} else {
		imcb_error(ic, "Received unknown command from main server: %s", cmd[0]);
	}

	return(1);
}

int msn_ns_message(struct msn_data *md, char *msg, int msglen, char **cmd, int num_parts)
{
	struct im_connection *ic = md->ic;
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
						imcb_notify_email(ic,
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
						imcb_notify_email(ic, "Received an e-mail message from %s <%s>.", fromname, from);
					}

					g_free(from);
					g_free(fromname);
				}
			} else if (g_strncasecmp(ct, "text/x-msmsgsactivemailnotification", 35) == 0) {
				/* Notification that a message has been read... Ignore it */
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
	} else if ((strcmp(cmd[0], "SDG") == 0) || (strcmp(cmd[0], "NFY") == 0)) {
		msn_ns_structured_message(md, msg, msglen, cmd);
	}

	return 1;
}

/* returns newly allocated string */
static char *msn_ns_parse_header_address(struct msn_data *md, char *headers, char *header_name)
{
	char *semicolon = NULL;
	char *header = NULL;
	char *address = NULL;

	if (!(header = get_rfc822_header(headers, header_name, 0))) {
		return NULL;
	}

	/* either the semicolon or the end of the string */
	semicolon = strchr(header, ';') ? : (header + strlen(header));

	address = g_strndup(header + 2, semicolon - header - 2);

	g_free(header);
	return address;
}

static void msn_ns_structured_message(struct msn_data *md, char *msg, int msglen, char **cmd)
{
	char **parts = NULL;
	char *action = NULL;
	char *who = NULL;
	gboolean selfmessage = FALSE;

	parts = g_strsplit(msg, "\r\n\r\n", 4);

	if (!(who = msn_ns_parse_header_address(md, parts[0], "From"))) {
		goto cleanup;
	}

	if (strcmp(who, md->ic->acc->user) == 0) {
		selfmessage = TRUE;
		g_free(who);
		if (!(who = msn_ns_parse_header_address(md, parts[0], "To"))) {
			goto cleanup;
		}
	}

	if ((strcmp(cmd[0], "SDG") == 0) && (action = get_rfc822_header(parts[2], "Message-Type", 0))) {
		msn_ns_sdg(md, who, parts, action, selfmessage);

	} else if ((strcmp(cmd[0], "NFY") == 0) && (action = get_rfc822_header(parts[2], "Uri", 0))) {
		gboolean is_put = (strcmp(cmd[1], "PUT") == 0);
		msn_ns_nfy(md, who, parts, action, is_put);
	}

cleanup:
	g_strfreev(parts);
	g_free(action);
	g_free(who);
}

static void msn_ns_sdg(struct msn_data *md, char *who, char **parts, char *action, gboolean selfmessage)
{
	struct im_connection *ic = md->ic;

	if (strcmp(action, "Control/Typing") == 0 && !selfmessage) {
		imcb_buddy_typing(ic, who, OPT_TYPING);
	} else if (strcmp(action, "Text") == 0) {
		imcb_buddy_msg(ic, who, parts[3], selfmessage ? OPT_SELFMESSAGE : 0, 0);
	}
}

static void msn_ns_nfy(struct msn_data *md, char *who, char **parts, char *action, gboolean is_put)
{
	struct im_connection *ic = md->ic;
	struct xt_node *body = NULL;
	struct xt_node *s = NULL;
	const char *state = NULL;
	char *nick = NULL;
	char *psm = NULL;
	int flags = OPT_LOGGED_IN;

	if (strcmp(action, "/user") != 0) {
		return;
	}

	if (!(body = xt_from_string(parts[3], 0))) {
		goto cleanup;
	}

	s = body->children;
	while ((s = xt_find_node(s, "s"))) {
		struct xt_node *s2;
		char *n = xt_find_attr(s, "n");  /* service name: IM, PE, etc */

		if (strcmp(n, "IM") == 0) {
			/* IM has basic presence information */
			if (!is_put) {
				/* NFY DEL with a <s> usually means log out from the last endpoint */
				flags &= ~OPT_LOGGED_IN;
				break;
			}

			s2 = xt_find_node(s->children, "Status");
			if (s2 && s2->text_len) {
				const struct msn_away_state *msn_state = msn_away_state_by_code(s2->text);
				state = msn_state->name;
				if (msn_state != msn_away_state_list) {
					flags |= OPT_AWAY;
				}
			}
		} else if (strcmp(n, "PE") == 0) {
			if ((s2 = xt_find_node(s->children, "PSM")) && s2->text_len) {
				psm = s2->text;
			}
			if ((s2 = xt_find_node(s->children, "FriendlyName")) && s2->text_len) {
				nick = s2->text;
			}
		}
		s = s->next;
	}

	imcb_buddy_status(ic, who, flags, state, psm);

	if (nick) {
		imcb_rename_buddy(ic, who, nick);
	}

cleanup:
	xt_free_node(body);
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

		/* don't reconnect with auth errors */
		if (error && g_str_has_prefix(error, "wsse:FailedAuthentication")) {
			imc_logout(ic, FALSE);
		} else {
			imc_logout(ic, TRUE);
		}
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

	if ((bd->flags & (MSN_BUDDY_FL | MSN_BUDDY_AL)) == 0 || (bd->flags & MSN_BUDDY_ADL_SYNCED)) {
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

	g_snprintf(l, sizeof(l), "%d", bd->flags & (MSN_BUDDY_FL | MSN_BUDDY_AL));
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

		if (bu->ic != ic || (bd->flags & (MSN_BUDDY_FL | MSN_BUDDY_AL)) == 0) {
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

static int msn_ns_send_sdg(struct im_connection *ic, bee_user_t *bu, const char *message_type, const char *text)
{
	struct msn_data *md = ic->proto_data;
	int retval = 0;
	char *buf;

	buf = g_strdup_printf(MSN_MESSAGE_HEADERS, bu->handle, ic->acc->user, md->uuid, message_type, strlen(text), text);
	retval = msn_ns_write(ic, -1, "SDG %d %zd\r\n%s", ++md->trId, strlen(buf), buf);
	g_free(buf);
	return retval;
}

int msn_ns_send_typing(struct im_connection *ic, bee_user_t *bu)
{
	return msn_ns_send_sdg(ic, bu, "Control/Typing", "");
}

int msn_ns_send_message(struct im_connection *ic, bee_user_t *bu, const char *text)
{
	return msn_ns_send_sdg(ic, bu, "Text", text);
}

