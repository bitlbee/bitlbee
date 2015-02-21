/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Jabber module - IQ packets                                               *
*                                                                           *
*  Copyright 2006-2012 Wilmer van der Gaast <wilmer@gaast.net>              *
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

#include "jabber.h"
#include "sha1.h"

static xt_status jabber_parse_roster(struct im_connection *ic, struct xt_node *node, struct xt_node *orig);
static xt_status jabber_iq_display_vcard(struct im_connection *ic, struct xt_node *node, struct xt_node *orig);

xt_status jabber_pkt_iq(struct xt_node *node, gpointer data)
{
	struct im_connection *ic = data;
	struct jabber_data *jd = ic->proto_data;
	struct xt_node *c, *reply = NULL;
	char *type, *s;
	int st, pack = 1;

	type = xt_find_attr(node, "type");

	if (!type) {
		imcb_error(ic, "Received IQ packet without type.");
		imc_logout(ic, TRUE);
		return XT_ABORT;
	}

	if (strcmp(type, "result") == 0 || strcmp(type, "error") == 0) {
		return jabber_cache_handle_packet(ic, node);
	} else if (strcmp(type, "get") == 0) {
		if (!((c = xt_find_node(node->children, "query")) ||
		      (c = xt_find_node(node->children, "ping")) ||
		      (c = xt_find_node(node->children, "time"))) ||
		    !(s = xt_find_attr(c, "xmlns"))) {
			/* Sigh. Who decided to suddenly invent new elements
			   instead of just sticking with <query/>? */
			return XT_HANDLED;
		}

		reply = xt_new_node("query", NULL, NULL);
		xt_add_attr(reply, "xmlns", s);

		/* Of course this is a very essential query to support. ;-) */
		if (strcmp(s, XMLNS_VERSION) == 0) {
			xt_add_child(reply, xt_new_node("name", set_getstr(&ic->acc->set, "user_agent"), NULL));
			xt_add_child(reply, xt_new_node("version", BITLBEE_VERSION, NULL));
			xt_add_child(reply, xt_new_node("os", ARCH, NULL));
		} else if (strcmp(s, XMLNS_TIME_OLD) == 0) {
			time_t time_ep;
			char buf[1024];

			buf[sizeof(buf) - 1] = 0;
			time_ep = time(NULL);

			strftime(buf, sizeof(buf) - 1, "%Y%m%dT%H:%M:%S", gmtime(&time_ep));
			xt_add_child(reply, xt_new_node("utc", buf, NULL));

			strftime(buf, sizeof(buf) - 1, "%Z", localtime(&time_ep));
			xt_add_child(reply, xt_new_node("tz", buf, NULL));
		} else if (strcmp(s, XMLNS_TIME) == 0) {
			time_t time_ep;
			char buf[1024];

			buf[sizeof(buf) - 1] = 0;
			time_ep = time(NULL);

			xt_free_node(reply);
			reply = xt_new_node("time", NULL, NULL);
			xt_add_attr(reply, "xmlns", XMLNS_TIME);

			strftime(buf, sizeof(buf) - 1, "%Y%m%dT%H:%M:%SZ", gmtime(&time_ep));
			xt_add_child(reply, xt_new_node("utc", buf, NULL));

			strftime(buf, sizeof(buf) - 1, "%z", localtime(&time_ep));
			if (strlen(buf) >= 5) {
				buf[6] = '\0';
				buf[5] = buf[4];
				buf[4] = buf[3];
				buf[3] = ':';
			}
			xt_add_child(reply, xt_new_node("tzo", buf, NULL));
		} else if (strcmp(s, XMLNS_PING) == 0) {
			xt_free_node(reply);
			reply = jabber_make_packet("iq", "result", xt_find_attr(node, "from"), NULL);
			if ((s = xt_find_attr(node, "id"))) {
				xt_add_attr(reply, "id", s);
			}
			pack = 0;
		} else if (strcmp(s, XMLNS_DISCO_INFO) == 0) {
			const char *features[] = { XMLNS_DISCO_INFO,
				                   XMLNS_VERSION,
				                   XMLNS_TIME_OLD,
				                   XMLNS_TIME,
				                   XMLNS_CHATSTATES,
				                   XMLNS_MUC,
				                   XMLNS_PING,
				                   XMLNS_RECEIPTS,
				                   XMLNS_SI,
				                   XMLNS_BYTESTREAMS,
				                   XMLNS_FILETRANSFER,
				                   NULL };
			const char **f;

			c = xt_new_node("identity", NULL, NULL);
			xt_add_attr(c, "category", "client");
			xt_add_attr(c, "type", "pc");
			xt_add_attr(c, "name", set_getstr(&ic->acc->set, "user_agent"));
			xt_add_child(reply, c);

			for (f = features; *f; f++) {
				c = xt_new_node("feature", NULL, NULL);
				xt_add_attr(c, "var", *f);
				xt_add_child(reply, c);
			}
		} else {
			xt_free_node(reply);
			reply = jabber_make_error_packet(node, "feature-not-implemented", "cancel", NULL);
			pack = 0;
		}
	} else if (strcmp(type, "set") == 0) {
		if ((c = xt_find_node(node->children, "si")) &&
		    (s = xt_find_attr(c, "xmlns")) &&
		    (strcmp(s, XMLNS_SI) == 0)) {
			return jabber_si_handle_request(ic, node, c);
		} else if (!(c = xt_find_node(node->children, "query")) ||
		           !(s = xt_find_attr(c, "xmlns"))) {
			return XT_HANDLED;
		} else if (strcmp(s, XMLNS_ROSTER) == 0) {
			/* This is a roster push. XMPP servers send this when someone
			   was added to (or removed from) the buddy list. AFAIK they're
			   sent even if we added this buddy in our own session. */
			int bare_len = strlen(jd->me);

			if ((s = xt_find_attr(node, "from")) == NULL ||
			    (strncmp(s, jd->me, bare_len) == 0 &&
			     (s[bare_len] == 0 || s[bare_len] == '/'))) {
				jabber_parse_roster(ic, node, NULL);

				/* Should we generate a reply here? Don't think it's
				   very important... */
			} else {
				imcb_log(ic, "Warning: %s tried to fake a roster push!", s ? s : "(unknown)");

				xt_free_node(reply);
				reply = jabber_make_error_packet(node, "not-allowed", "cancel", NULL);
				pack = 0;
			}
		} else if (strcmp(s, XMLNS_BYTESTREAMS) == 0) {
			/* Bytestream Request (stage 2 of file transfer) */
			return jabber_bs_recv_request(ic, node, c);
		} else {
			xt_free_node(reply);
			reply = jabber_make_error_packet(node, "feature-not-implemented", "cancel", NULL);
			pack = 0;
		}
	}

	/* If we recognized the xmlns and managed to generate a reply,
	   finish and send it. */
	if (reply) {
		/* Normally we still have to pack it into an iq-result
		   packet, but for errors, for example, we don't. */
		if (pack) {
			reply = jabber_make_packet("iq", "result", xt_find_attr(node, "from"), reply);
			if ((s = xt_find_attr(node, "id"))) {
				xt_add_attr(reply, "id", s);
			}
		}

		st = jabber_write_packet(ic, reply);
		xt_free_node(reply);
		if (!st) {
			return XT_ABORT;
		}
	}

	return XT_HANDLED;
}

static xt_status jabber_do_iq_auth(struct im_connection *ic, struct xt_node *node, struct xt_node *orig);
static xt_status jabber_finish_iq_auth(struct im_connection *ic, struct xt_node *node, struct xt_node *orig);

int jabber_init_iq_auth(struct im_connection *ic)
{
	struct jabber_data *jd = ic->proto_data;
	struct xt_node *node;
	int st;

	node = xt_new_node("query", NULL, xt_new_node("username", jd->username, NULL));
	xt_add_attr(node, "xmlns", XMLNS_AUTH);
	node = jabber_make_packet("iq", "get", NULL, node);

	jabber_cache_add(ic, node, jabber_do_iq_auth);
	st = jabber_write_packet(ic, node);

	return st;
}

static xt_status jabber_do_iq_auth(struct im_connection *ic, struct xt_node *node, struct xt_node *orig)
{
	struct jabber_data *jd = ic->proto_data;
	struct xt_node *reply, *query;
	xt_status st;
	char *s;

	if (!(query = xt_find_node(node->children, "query"))) {
		imcb_log(ic, "Warning: Received incomplete IQ packet while authenticating");
		imc_logout(ic, FALSE);
		return XT_HANDLED;
	}

	/* Time to authenticate ourselves! */
	reply = xt_new_node("query", NULL, NULL);
	xt_add_attr(reply, "xmlns", XMLNS_AUTH);
	xt_add_child(reply, xt_new_node("username", jd->username, NULL));
	xt_add_child(reply, xt_new_node("resource", set_getstr(&ic->acc->set, "resource"), NULL));

	if (xt_find_node(query->children, "digest") && (s = xt_find_attr(jd->xt->root, "id"))) {
		/* We can do digest authentication, it seems, and of
		   course we prefer that. */
		sha1_state_t sha;
		char hash_hex[41];
		unsigned char hash[20];
		int i;

		sha1_init(&sha);
		sha1_append(&sha, (unsigned char *) s, strlen(s));
		sha1_append(&sha, (unsigned char *) ic->acc->pass, strlen(ic->acc->pass));
		sha1_finish(&sha, hash);

		for (i = 0; i < 20; i++) {
			sprintf(hash_hex + i * 2, "%02x", hash[i]);
		}

		xt_add_child(reply, xt_new_node("digest", hash_hex, NULL));
	} else if (xt_find_node(query->children, "password")) {
		/* We'll have to stick with plaintext. Let's hope we're using SSL/TLS... */
		xt_add_child(reply, xt_new_node("password", ic->acc->pass, NULL));
	} else {
		xt_free_node(reply);

		imcb_error(ic, "Can't find suitable authentication method");
		imc_logout(ic, FALSE);
		return XT_ABORT;
	}

	reply = jabber_make_packet("iq", "set", NULL, reply);
	jabber_cache_add(ic, reply, jabber_finish_iq_auth);
	st = jabber_write_packet(ic, reply);

	return st ? XT_HANDLED : XT_ABORT;
}

static xt_status jabber_finish_iq_auth(struct im_connection *ic, struct xt_node *node, struct xt_node *orig)
{
	struct jabber_data *jd = ic->proto_data;
	char *type;

	if (!(type = xt_find_attr(node, "type"))) {
		imcb_log(ic, "Warning: Received incomplete IQ packet while authenticating");
		imc_logout(ic, FALSE);
		return XT_HANDLED;
	}

	if (strcmp(type, "error") == 0) {
		imcb_error(ic, "Authentication failure");
		imc_logout(ic, FALSE);
		return XT_ABORT;
	} else if (strcmp(type, "result") == 0) {
		/* This happens when we just successfully authenticated the
		   old (non-SASL) way. */
		jd->flags |= JFLAG_AUTHENTICATED;
		if (!jabber_get_roster(ic)) {
			return XT_ABORT;
		}
		if (!jabber_iq_disco_server(ic)) {
			return XT_ABORT;
		}
	}

	return XT_HANDLED;
}

xt_status jabber_pkt_bind_sess(struct im_connection *ic, struct xt_node *node, struct xt_node *orig)
{
	struct jabber_data *jd = ic->proto_data;
	struct xt_node *c, *reply = NULL;
	char *s;

	if (node && (c = xt_find_node(node->children, "bind"))) {
		c = xt_find_node(c->children, "jid");
		if (!c || !c->text) {
			/* Server is crap, but this is no disaster. */
		} else if (jabber_compare_jid(jd->me, c->text) == 0) {
			s = strchr(c->text, '/');
			if (s) {
				*s = '\0';
			}
			jabber_set_me(ic, c->text);
			if (s) {
				*s = '/';
			}
		} else if (c && c->text_len && (s = strchr(c->text, '/')) &&
		           strcmp(s + 1, set_getstr(&ic->acc->set, "resource")) != 0) {
			imcb_log(ic, "Server changed session resource string to `%s'", s + 1);
		}
	}

	if (jd->flags & JFLAG_WANT_BIND) {
		reply = xt_new_node("bind", NULL, xt_new_node("resource", set_getstr(&ic->acc->set, "resource"), NULL));
		xt_add_attr(reply, "xmlns", XMLNS_BIND);
		jd->flags &= ~JFLAG_WANT_BIND;
	} else if (jd->flags & JFLAG_WANT_SESSION) {
		reply = xt_new_node("session", NULL, NULL);
		xt_add_attr(reply, "xmlns", XMLNS_SESSION);
		jd->flags &= ~JFLAG_WANT_SESSION;
	}

	if (reply != NULL) {
		reply = jabber_make_packet("iq", "set", NULL, reply);
		jabber_cache_add(ic, reply, jabber_pkt_bind_sess);

		if (!jabber_write_packet(ic, reply)) {
			return XT_ABORT;
		}
	} else if ((jd->flags & (JFLAG_WANT_BIND | JFLAG_WANT_SESSION)) == 0) {
		if (!jabber_get_roster(ic)) {
			return XT_ABORT;
		}
		if (!jabber_iq_disco_server(ic)) {
			return XT_ABORT;
		}
	}

	return XT_HANDLED;
}

int jabber_get_roster(struct im_connection *ic)
{
	struct xt_node *node;
	int st;

	imcb_log(ic, "Authenticated, requesting buddy list");

	node = xt_new_node("query", NULL, NULL);
	xt_add_attr(node, "xmlns", XMLNS_ROSTER);
	node = jabber_make_packet("iq", "get", NULL, node);

	jabber_cache_add(ic, node, jabber_parse_roster);
	st = jabber_write_packet(ic, node);

	return st;
}

static xt_status jabber_parse_roster(struct im_connection *ic, struct xt_node *node, struct xt_node *orig)
{
	struct jabber_data *jd = ic->proto_data;
	struct xt_node *query, *c;
	int initial = (orig != NULL);

	if (!(query = xt_find_node(node->children, "query"))) {
		imcb_log(ic, "Warning: Received NULL roster packet");
		return XT_HANDLED;
	}

	c = query->children;
	while ((c = xt_find_node(c, "item"))) {
		struct xt_node *group = xt_find_node(c->children, "group");
		char *jid = xt_find_attr(c, "jid");
		char *name = xt_find_attr(c, "name");
		char *sub = xt_find_attr(c, "subscription");
		char *mention_name = xt_find_attr(c, "mention_name");

		if (jid && sub) {
			if ((strcmp(sub, "both") == 0 || strcmp(sub, "to") == 0)) {
				imcb_add_buddy(ic, jid, (group && group->text_len) ?
				               group->text : NULL);

				if (name) {
					imcb_rename_buddy(ic, jid, name);
				}

				/* This could also be used to set the full name as nick for fb/gtalk,
				 * but i'm keeping the old (ugly?) default behavior just to be safe */
				if (mention_name && (jd->flags & JFLAG_HIPCHAT)) {
					imcb_buddy_nick_hint(ic, jid, mention_name);
				}
			} else if (strcmp(sub, "remove") == 0) {
				jabber_buddy_remove_bare(ic, jid);
				imcb_remove_buddy(ic, jid, NULL);
			}
		}

		c = c->next;
	}

	if (initial) {
		imcb_connected(ic);
	}

	return XT_HANDLED;
}

int jabber_get_vcard(struct im_connection *ic, char *bare_jid)
{
	struct xt_node *node;

	if (strchr(bare_jid, '/')) {
		return 1;       /* This was an error, but return 0 should only be done if the connection died... */

	}
	node = xt_new_node("vCard", NULL, NULL);
	xt_add_attr(node, "xmlns", XMLNS_VCARD);
	node = jabber_make_packet("iq", "get", bare_jid, node);

	jabber_cache_add(ic, node, jabber_iq_display_vcard);
	return jabber_write_packet(ic, node);
}

static xt_status jabber_iq_display_vcard(struct im_connection *ic, struct xt_node *node, struct xt_node *orig)
{
	struct xt_node *vc, *c, *sc; /* subchild, ic is already in use ;-) */
	GString *reply;
	char *s;

	if ((s = xt_find_attr(node, "type")) == NULL ||
	    strcmp(s, "result") != 0 ||
	    (vc = xt_find_node(node->children, "vCard")) == NULL) {
		s = xt_find_attr(orig, "to");   /* If this returns NULL something's wrong.. */
		imcb_log(ic, "Could not retrieve vCard of %s", s ? s : "(NULL)");
		return XT_HANDLED;
	}

	s = xt_find_attr(orig, "to");
	reply = g_string_new("vCard information for ");
	reply = g_string_append(reply, s ? s : "(NULL)");
	reply = g_string_append(reply, ":\n");

	/* I hate this format, I really do... */

	if ((c = xt_find_node(vc->children, "FN")) && c->text_len) {
		g_string_append_printf(reply, "Name: %s\n", c->text);
	}

	if ((c = xt_find_node(vc->children, "N")) && c->children) {
		reply = g_string_append(reply, "Full name:");

		if ((sc = xt_find_node(c->children, "PREFIX")) && sc->text_len) {
			g_string_append_printf(reply, " %s", sc->text);
		}
		if ((sc = xt_find_node(c->children, "GIVEN")) && sc->text_len) {
			g_string_append_printf(reply, " %s", sc->text);
		}
		if ((sc = xt_find_node(c->children, "MIDDLE")) && sc->text_len) {
			g_string_append_printf(reply, " %s", sc->text);
		}
		if ((sc = xt_find_node(c->children, "FAMILY")) && sc->text_len) {
			g_string_append_printf(reply, " %s", sc->text);
		}
		if ((sc = xt_find_node(c->children, "SUFFIX")) && sc->text_len) {
			g_string_append_printf(reply, " %s", sc->text);
		}

		reply = g_string_append_c(reply, '\n');
	}

	if ((c = xt_find_node(vc->children, "NICKNAME")) && c->text_len) {
		g_string_append_printf(reply, "Nickname: %s\n", c->text);
	}

	if ((c = xt_find_node(vc->children, "BDAY")) && c->text_len) {
		g_string_append_printf(reply, "Date of birth: %s\n", c->text);
	}

	/* Slightly alternative use of for... ;-) */
	for (c = vc->children; (c = xt_find_node(c, "EMAIL")); c = c->next) {
		if ((sc = xt_find_node(c->children, "USERID")) == NULL || sc->text_len == 0) {
			continue;
		}

		if (xt_find_node(c->children, "HOME")) {
			s = "Home";
		} else if (xt_find_node(c->children, "WORK")) {
			s = "Work";
		} else {
			s = "Misc.";
		}

		g_string_append_printf(reply, "%s e-mail address: %s\n", s, sc->text);
	}

	if ((c = xt_find_node(vc->children, "URL")) && c->text_len) {
		g_string_append_printf(reply, "Homepage: %s\n", c->text);
	}

	/* Slightly alternative use of for... ;-) */
	for (c = vc->children; (c = xt_find_node(c, "ADR")); c = c->next) {
		if (xt_find_node(c->children, "HOME")) {
			s = "Home";
		} else if (xt_find_node(c->children, "WORK")) {
			s = "Work";
		} else {
			s = "Misc.";
		}

		g_string_append_printf(reply, "%s address: ", s);

		if ((sc = xt_find_node(c->children, "STREET")) && sc->text_len) {
			g_string_append_printf(reply, "%s ", sc->text);
		}
		if ((sc = xt_find_node(c->children, "EXTADR")) && sc->text_len) {
			g_string_append_printf(reply, "%s, ", sc->text);
		}
		if ((sc = xt_find_node(c->children, "PCODE")) && sc->text_len) {
			g_string_append_printf(reply, "%s, ", sc->text);
		}
		if ((sc = xt_find_node(c->children, "LOCALITY")) && sc->text_len) {
			g_string_append_printf(reply, "%s, ", sc->text);
		}
		if ((sc = xt_find_node(c->children, "REGION")) && sc->text_len) {
			g_string_append_printf(reply, "%s, ", sc->text);
		}
		if ((sc = xt_find_node(c->children, "CTRY")) && sc->text_len) {
			g_string_append_printf(reply, "%s", sc->text);
		}

		if (reply->str[reply->len - 2] == ',') {
			reply = g_string_truncate(reply, reply->len - 2);
		}

		reply = g_string_append_c(reply, '\n');
	}

	for (c = vc->children; (c = xt_find_node(c, "TEL")); c = c->next) {
		if ((sc = xt_find_node(c->children, "NUMBER")) == NULL || sc->text_len == 0) {
			continue;
		}

		if (xt_find_node(c->children, "HOME")) {
			s = "Home";
		} else if (xt_find_node(c->children, "WORK")) {
			s = "Work";
		} else {
			s = "Misc.";
		}

		g_string_append_printf(reply, "%s phone number: %s\n", s, sc->text);
	}

	if ((c = xt_find_node(vc->children, "DESC")) && c->text_len) {
		g_string_append_printf(reply, "Other information:\n%s", c->text);
	}

	/* *sigh* */

	imcb_log(ic, "%s", reply->str);
	g_string_free(reply, TRUE);

	return XT_HANDLED;
}

static xt_status jabber_add_to_roster_callback(struct im_connection *ic, struct xt_node *node, struct xt_node *orig);

int jabber_add_to_roster(struct im_connection *ic, const char *handle, const char *name, const char *group)
{
	struct xt_node *node;
	int st;

	/* Build the item entry */
	node = xt_new_node("item", NULL, NULL);
	xt_add_attr(node, "jid", handle);
	if (name) {
		xt_add_attr(node, "name", name);
	}
	if (group) {
		xt_add_child(node, xt_new_node("group", group, NULL));
	}

	/* And pack it into a roster-add packet */
	node = xt_new_node("query", NULL, node);
	xt_add_attr(node, "xmlns", XMLNS_ROSTER);
	node = jabber_make_packet("iq", "set", NULL, node);
	jabber_cache_add(ic, node, jabber_add_to_roster_callback);

	st = jabber_write_packet(ic, node);

	return st;
}

static xt_status jabber_add_to_roster_callback(struct im_connection *ic, struct xt_node *node, struct xt_node *orig)
{
	char *s, *jid = NULL;
	struct xt_node *c;

	if ((c = xt_find_node(orig->children, "query")) &&
	    (c = xt_find_node(c->children, "item")) &&
	    (jid = xt_find_attr(c, "jid")) &&
	    (s = xt_find_attr(node, "type")) &&
	    strcmp(s, "result") == 0) {
		if (bee_user_by_handle(ic->bee, ic, jid) == NULL) {
			imcb_add_buddy(ic, jid, NULL);
		}
	} else {
		imcb_log(ic, "Error while adding `%s' to your contact list.",
		         jid ? jid : "(unknown handle)");
	}

	return XT_HANDLED;
}

int jabber_remove_from_roster(struct im_connection *ic, char *handle)
{
	struct xt_node *node;
	int st;

	/* Build the item entry */
	node = xt_new_node("item", NULL, NULL);
	xt_add_attr(node, "jid", handle);
	xt_add_attr(node, "subscription", "remove");

	/* And pack it into a roster-add packet */
	node = xt_new_node("query", NULL, node);
	xt_add_attr(node, "xmlns", XMLNS_ROSTER);
	node = jabber_make_packet("iq", "set", NULL, node);

	st = jabber_write_packet(ic, node);

	xt_free_node(node);
	return st;
}

xt_status jabber_iq_parse_features(struct im_connection *ic, struct xt_node *node, struct xt_node *orig);

xt_status jabber_iq_query_features(struct im_connection *ic, char *bare_jid)
{
	struct xt_node *node, *query;
	struct jabber_buddy *bud;

	if ((bud = jabber_buddy_by_jid(ic, bare_jid, 0)) == NULL) {
		/* Who cares about the unknown... */
		imcb_log(ic, "Couldn't find buddy: %s", bare_jid);
		return XT_HANDLED;
	}

	if (bud->features) { /* been here already */
		return XT_HANDLED;
	}

	node = xt_new_node("query", NULL, NULL);
	xt_add_attr(node, "xmlns", XMLNS_DISCO_INFO);

	if (!(query = jabber_make_packet("iq", "get", bare_jid, node))) {
		imcb_log(ic, "WARNING: Couldn't generate feature query");
		xt_free_node(node);
		return XT_HANDLED;
	}

	jabber_cache_add(ic, query, jabber_iq_parse_features);

	return jabber_write_packet(ic, query) ? XT_HANDLED : XT_ABORT;
}

xt_status jabber_iq_parse_features(struct im_connection *ic, struct xt_node *node, struct xt_node *orig)
{
	struct xt_node *c;
	struct jabber_buddy *bud;
	char *feature, *xmlns, *from;

	if (!(from = xt_find_attr(node, "from")) ||
	    !(c = xt_find_node(node->children, "query")) ||
	    !(xmlns = xt_find_attr(c, "xmlns")) ||
	    !(strcmp(xmlns, XMLNS_DISCO_INFO) == 0)) {
		imcb_log(ic, "WARNING: Received incomplete IQ-result packet for discover");
		return XT_HANDLED;
	}
	if ((bud = jabber_buddy_by_jid(ic, from, 0)) == NULL) {
		/* Who cares about the unknown... */
		imcb_log(ic, "Couldn't find buddy: %s", from);
		return XT_HANDLED;
	}

	c = c->children;
	while ((c = xt_find_node(c, "feature"))) {
		feature = xt_find_attr(c, "var");
		if (feature) {
			bud->features = g_slist_append(bud->features, g_strdup(feature));
		}
		c = c->next;
	}

	return XT_HANDLED;
}

xt_status jabber_iq_parse_server_features(struct im_connection *ic, struct xt_node *node, struct xt_node *orig);

xt_status jabber_iq_query_server(struct im_connection *ic, char *jid, char *xmlns)
{
	struct xt_node *node, *query;
	struct jabber_data *jd = ic->proto_data;

	node = xt_new_node("query", NULL, NULL);
	xt_add_attr(node, "xmlns", xmlns);

	if (!(query = jabber_make_packet("iq", "get", jid, node))) {
		imcb_log(ic, "WARNING: Couldn't generate server query");
		xt_free_node(node);
	}

	jd->have_streamhosts--;
	jabber_cache_add(ic, query, jabber_iq_parse_server_features);

	return jabber_write_packet(ic, query) ? XT_HANDLED : XT_ABORT;
}

/*
 * Query the server for "items", query each "item" for identities, query each "item" that's a proxy for it's bytestream info
 */
xt_status jabber_iq_parse_server_features(struct im_connection *ic, struct xt_node *node, struct xt_node *orig)
{
	struct xt_node *c;
	struct jabber_data *jd = ic->proto_data;
	char *xmlns, *from;

	if (!(c = xt_find_node(node->children, "query")) ||
	    !(from = xt_find_attr(node, "from")) ||
	    !(xmlns = xt_find_attr(c, "xmlns"))) {
		imcb_log(ic, "WARNING: Received incomplete IQ-result packet for discover");
		return XT_HANDLED;
	}

	jd->have_streamhosts++;

	if (strcmp(xmlns, XMLNS_DISCO_ITEMS) == 0) {
		char *itemjid;

		/* answer from server */

		c = c->children;
		while ((c = xt_find_node(c, "item"))) {
			itemjid = xt_find_attr(c, "jid");

			if (itemjid) {
				jabber_iq_query_server(ic, itemjid, XMLNS_DISCO_INFO);
			}

			c = c->next;
		}
	} else if (strcmp(xmlns, XMLNS_DISCO_INFO) == 0) {
		char *category, *type;

		/* answer from potential proxy */

		c = c->children;
		while ((c = xt_find_node(c, "identity"))) {
			category = xt_find_attr(c, "category");
			type = xt_find_attr(c, "type");

			if (type && (strcmp(type, "bytestreams") == 0) &&
			    category && (strcmp(category, "proxy") == 0)) {
				jabber_iq_query_server(ic, from, XMLNS_BYTESTREAMS);
			}

			c = c->next;
		}
	} else if (strcmp(xmlns, XMLNS_BYTESTREAMS) == 0) {
		char *host, *jid, *port_s;
		int port;

		/* answer from proxy */

		if ((c = xt_find_node(c->children, "streamhost")) &&
		    (host = xt_find_attr(c, "host")) &&
		    (port_s = xt_find_attr(c, "port")) &&
		    (sscanf(port_s, "%d", &port) == 1) &&
		    (jid = xt_find_attr(c, "jid"))) {
			jabber_streamhost_t *sh = g_new0(jabber_streamhost_t, 1);

			sh->jid = g_strdup(jid);
			sh->host = g_strdup(host);
			g_snprintf(sh->port, sizeof(sh->port), "%u", port);

			imcb_log(ic, "Proxy found: jid %s host %s port %u", jid, host, port);
			jd->streamhosts = g_slist_append(jd->streamhosts, sh);
		}
	}

	if (jd->have_streamhosts == 0) {
		jd->have_streamhosts++;
	}

	return XT_HANDLED;
}

static xt_status jabber_iq_version_response(struct im_connection *ic,
                                            struct xt_node *node, struct xt_node *orig);

void jabber_iq_version_send(struct im_connection *ic, struct jabber_buddy *bud, void *data)
{
	struct xt_node *node, *query;

	node = xt_new_node("query", NULL, NULL);
	xt_add_attr(node, "xmlns", XMLNS_VERSION);
	query = jabber_make_packet("iq", "get", bud->full_jid, node);
	jabber_cache_add(ic, query, jabber_iq_version_response);

	jabber_write_packet(ic, query);
}

static xt_status jabber_iq_version_response(struct im_connection *ic,
                                            struct xt_node *node, struct xt_node *orig)
{
	struct xt_node *query;
	GString *rets;
	char *s;
	char *ret[2] = {};
	bee_user_t *bu;
	struct jabber_buddy *bud = NULL;

	if ((s = xt_find_attr(node, "from")) &&
	    (bud = jabber_buddy_by_jid(ic, s, 0)) &&
	    (query = xt_find_node(node->children, "query")) &&
	    (bu = bee_user_by_handle(ic->bee, ic, bud->bare_jid))) {
		rets = g_string_new("Resource ");
		g_string_append(rets, bud->resource);
	} else {
		return XT_HANDLED;
	}

	for (query = query->children; query; query = query->next) {
		if (query->text_len > 0) {
			g_string_append_printf(rets, " %s: %s,", query->name, query->text);
		}
	}

	g_string_truncate(rets, rets->len - 1);
	ret[0] = rets->str;
	imcb_buddy_action_response(bu, "VERSION", ret, NULL);
	g_string_free(rets, TRUE);

	return XT_HANDLED;
}

static xt_status jabber_iq_disco_server_response(struct im_connection *ic,
                                                 struct xt_node *node, struct xt_node *orig);

int jabber_iq_disco_server(struct im_connection *ic)
{
	struct xt_node *node, *iq;
	struct jabber_data *jd = ic->proto_data;

	node = xt_new_node("query", NULL, NULL);
	xt_add_attr(node, "xmlns", XMLNS_DISCO_INFO);
	iq = jabber_make_packet("iq", "get", jd->server, node);

	jabber_cache_add(ic, iq, jabber_iq_disco_server_response);
	return jabber_write_packet(ic, iq);
}

static xt_status jabber_iq_disco_server_response(struct im_connection *ic,
                                                 struct xt_node *node, struct xt_node *orig)
{
	struct jabber_data *jd = ic->proto_data;
	struct xt_node *id;

	if ((id = xt_find_path(node, "query/identity"))) {
		char *cat, *type, *name;

		if (!(cat = xt_find_attr(id, "category")) ||
		    !(type = xt_find_attr(id, "type")) ||
		    !(name = xt_find_attr(id, "name"))) {
			return XT_HANDLED;
		}

		if (strcmp(cat, "server") == 0 && strcmp(type, "im") == 0 &&
		    strstr(name, "Google") != NULL) {
			jd->flags |= JFLAG_GTALK;
		}
	}

	return XT_HANDLED;
}
