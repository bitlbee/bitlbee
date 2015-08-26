/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Jabber module - Conference rooms                                         *
*                                                                           *
*  Copyright 2007-2012 Wilmer van der Gaast <wilmer@gaast.net>              *
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

static xt_status jabber_chat_join_failed(struct im_connection *ic, struct xt_node *node, struct xt_node *orig);

struct groupchat *jabber_chat_join(struct im_connection *ic, const char *room, const char *nick, const char *password)
{
	struct jabber_chat *jc;
	struct xt_node *node;
	struct groupchat *c;
	char *roomjid;

	roomjid = g_strdup_printf("%s/%s", room, nick);
	node = xt_new_node("x", NULL, NULL);
	xt_add_attr(node, "xmlns", XMLNS_MUC);
	if (password) {
		xt_add_child(node, xt_new_node("password", password, NULL));
	}
	node = jabber_make_packet("presence", NULL, roomjid, node);
	jabber_cache_add(ic, node, jabber_chat_join_failed);

	if (!jabber_write_packet(ic, node)) {
		g_free(roomjid);
		return NULL;
	}

	jc = g_new0(struct jabber_chat, 1);
	jc->name = jabber_normalize(room);

	if ((jc->me = jabber_buddy_add(ic, roomjid)) == NULL) {
		g_free(roomjid);
		g_free(jc->name);
		g_free(jc);
		return NULL;
	}

	/* roomjid isn't normalized yet, and we need an original version
	   of the nick to send a proper presence update. */
	jc->my_full_jid = roomjid;

	c = imcb_chat_new(ic, room);
	c->data = jc;

	return c;
}

struct groupchat *jabber_chat_with(struct im_connection *ic, char *who)
{
	struct jabber_data *jd = ic->proto_data;
	struct jabber_chat *jc;
	struct groupchat *c;
	sha1_state_t sum;
	double now = gettime();
	char *uuid, *rjid, *cserv;

	sha1_init(&sum);
	sha1_append(&sum, (uint8_t *) ic->acc->user, strlen(ic->acc->user));
	sha1_append(&sum, (uint8_t *) &now, sizeof(now));
	sha1_append(&sum, (uint8_t *) who, strlen(who));
	uuid = sha1_random_uuid(&sum);

	if (jd->flags & JFLAG_GTALK) {
		cserv = g_strdup("groupchat.google.com");
	} else {
		/* Guess... */
		cserv = g_strdup_printf("conference.%s", jd->server);
	}

	rjid = g_strdup_printf("private-chat-%s@%s", uuid, cserv);
	g_free(uuid);
	g_free(cserv);

	c = jabber_chat_join(ic, rjid, jd->username, NULL);
	g_free(rjid);
	if (c == NULL) {
		return NULL;
	}

	jc = c->data;
	jc->invite = g_strdup(who);

	return c;
}

static xt_status jabber_chat_join_failed(struct im_connection *ic, struct xt_node *node, struct xt_node *orig)
{
	struct jabber_error *err;
	struct jabber_buddy *bud;
	char *room;

	room = xt_find_attr(orig, "to");
	bud = jabber_buddy_by_jid(ic, room, 0);
	err = jabber_error_parse(xt_find_node(node->children, "error"), XMLNS_STANZA_ERROR);
	if (err) {
		imcb_error(ic, "Error joining groupchat %s: %s%s%s", room, err->code,
		           err->text ? ": " : "", err->text ? err->text : "");
		jabber_error_free(err);
	}
	if (bud) {
		jabber_chat_free(jabber_chat_by_jid(ic, bud->bare_jid));
	}

	return XT_HANDLED;
}

struct groupchat *jabber_chat_by_jid(struct im_connection *ic, const char *name)
{
	char *normalized = jabber_normalize(name);
	GSList *l;
	struct groupchat *ret;
	struct jabber_chat *jc;

	for (l = ic->groupchats; l; l = l->next) {
		ret = l->data;
		jc = ret->data;
		if (strcmp(normalized, jc->name) == 0) {
			break;
		}
	}
	g_free(normalized);

	return l ? ret : NULL;
}

void jabber_chat_free(struct groupchat *c)
{
	struct jabber_chat *jc = c->data;

	jabber_buddy_remove_bare(c->ic, jc->name);

	g_free(jc->my_full_jid);
	g_free(jc->name);
	g_free(jc->invite);
	g_free(jc);

	imcb_chat_free(c);
}

int jabber_chat_msg(struct groupchat *c, char *message, int flags)
{
	struct im_connection *ic = c->ic;
	struct jabber_chat *jc = c->data;
	struct xt_node *node;

	jc->flags |= JCFLAG_MESSAGE_SENT;

	node = xt_new_node("body", message, NULL);
	node = jabber_make_packet("message", "groupchat", jc->name, node);

	if (!jabber_write_packet(ic, node)) {
		xt_free_node(node);
		return 0;
	}
	xt_free_node(node);

	return 1;
}

int jabber_chat_topic(struct groupchat *c, char *topic)
{
	struct im_connection *ic = c->ic;
	struct jabber_chat *jc = c->data;
	struct xt_node *node;

	node = xt_new_node("subject", topic, NULL);
	node = jabber_make_packet("message", "groupchat", jc->name, node);

	if (!jabber_write_packet(ic, node)) {
		xt_free_node(node);
		return 0;
	}
	xt_free_node(node);

	return 1;
}

int jabber_chat_leave(struct groupchat *c, const char *reason)
{
	struct im_connection *ic = c->ic;
	struct jabber_chat *jc = c->data;
	struct xt_node *node;

	node = xt_new_node("x", NULL, NULL);
	xt_add_attr(node, "xmlns", XMLNS_MUC);
	node = jabber_make_packet("presence", "unavailable", jc->my_full_jid, node);

	if (!jabber_write_packet(ic, node)) {
		xt_free_node(node);
		return 0;
	}
	xt_free_node(node);

	return 1;
}

void jabber_chat_invite(struct groupchat *c, char *who, char *message)
{
	struct xt_node *node;
	struct im_connection *ic = c->ic;
	struct jabber_chat *jc = c->data;

	node = xt_new_node("reason", message, NULL);

	node = xt_new_node("invite", NULL, node);
	xt_add_attr(node, "to", who);

	node = xt_new_node("x", NULL, node);
	xt_add_attr(node, "xmlns", XMLNS_MUC_USER);

	node = jabber_make_packet("message", NULL, jc->name, node);

	jabber_write_packet(ic, node);

	xt_free_node(node);
}

/* Not really the same syntax as the normal pkt_ functions, but this isn't
   called by the xmltree parser directly and this way I can add some extra
   parameters so we won't have to repeat too many things done by the caller
   already. */
void jabber_chat_pkt_presence(struct im_connection *ic, struct jabber_buddy *bud, struct xt_node *node)
{
	struct groupchat *chat;
	struct xt_node *c;
	char *type = xt_find_attr(node, "type");
	struct jabber_data *jd = ic->proto_data;
	struct jabber_chat *jc;
	char *s;

	if ((chat = jabber_chat_by_jid(ic, bud->bare_jid)) == NULL) {
		/* How could this happen?? We could do kill( self, 11 )
		   now or just wait for the OS to do it. :-) */
		return;
	}

	jc = chat->data;

	if (type == NULL && !(bud->flags & JBFLAG_IS_CHATROOM)) {
		bud->flags |= JBFLAG_IS_CHATROOM;
		/* If this one wasn't set yet, this buddy just joined the chat.
		   Slightly hackish way of finding out eh? ;-) */

		/* This is pretty messy... Here it sets ext_jid to the real
		   JID of the participant. Works for non-anonymized channels.
		   Might break if someone joins a chat twice, though. */
		for (c = node->children; (c = xt_find_node(c, "x")); c = c->next) {
			if ((s = xt_find_attr(c, "xmlns")) &&
			    (strcmp(s, XMLNS_MUC_USER) == 0)) {
				struct xt_node *item;

				item = xt_find_node(c->children, "item");
				if ((s = xt_find_attr(item, "jid"))) {
					/* Yay, found what we need. :-) */
					bud->ext_jid = jabber_normalize(s);
					break;
				}
			}
		}

		/* Make up some other handle, if necessary. */
		if (bud->ext_jid == NULL) {
			if (bud == jc->me) {
				bud->ext_jid = g_strdup(jd->me);
			} else {
				int i;

				/* Don't want the nick to be at the end, so let's
				   think of some slightly different notation to use
				   for anonymous groupchat participants in BitlBee. */
				bud->ext_jid = g_strdup_printf("%s=%s", bud->resource, bud->bare_jid);

				/* And strip any unwanted characters. */
				for (i = 0; bud->resource[i]; i++) {
					if (bud->ext_jid[i] == '=' || bud->ext_jid[i] == '@') {
						bud->ext_jid[i] = '_';
					}
				}

				/* Some program-specific restrictions. */
				imcb_clean_handle(ic, bud->ext_jid);
			}
			bud->flags |= JBFLAG_IS_ANONYMOUS;
		}

		if (bud != jc->me && bud->flags & JBFLAG_IS_ANONYMOUS) {
			/* If JIDs are anonymized, add them to the local
			   list for the duration of this chat. */
			imcb_add_buddy(ic, bud->ext_jid, NULL);
			imcb_buddy_nick_hint(ic, bud->ext_jid, bud->resource);
		}

		if (bud == jc->me && jc->invite != NULL) {
			char *msg = g_strdup_printf("Please join me in room %s", jc->name);
			jabber_chat_invite(chat, jc->invite, msg);
			g_free(jc->invite);
			g_free(msg);
			jc->invite = NULL;
		}

		s = strchr(bud->ext_jid, '/');
		if (s) {
			*s = 0; /* Should NEVER be NULL, but who knows... */
		}
		imcb_chat_add_buddy(chat, bud->ext_jid);
		if (s) {
			*s = '/';
		}
	} else if (type) { /* type can only be NULL or "unavailable" in this function */
		if ((bud->flags & JBFLAG_IS_CHATROOM) && bud->ext_jid) {
			char *reason = NULL;
			char *status = NULL;
			char *status_text = NULL;
			
			if ((c = xt_find_node_by_attr(node->children, "x", "xmlns", XMLNS_MUC_USER))) {
				struct xt_node *c2 = c->children;

				while ((c2 = xt_find_node(c2, "status"))) {
					char *code = xt_find_attr(c2, "code");
					if (g_strcmp0(code, "301") == 0) {
						status = "Banned";
						break;
					} else if (g_strcmp0(code, "303") == 0) {
						/* This could be handled in a cleverer way,
						 * but let's just show a literal part/join for now */
						status = "Changing nicks";
						break;
					} else if (g_strcmp0(code, "307") == 0) {
						status = "Kicked";
						break;
					}
					c2 = c2->next;
				}

				/* Sometimes the status message is in presence/x/item/reason */
				if ((c2 = xt_find_path(c, "item/reason")) && c2->text && c2->text_len) {
					status_text = c2->text;
				}
			}

			/* Sometimes the status message is right inside <presence> */
			if ((c = xt_find_node(node->children, "status")) && c->text && c->text_len) {
				status_text = c->text;
			}

			if (status_text && status) {
				reason = g_strdup_printf("%s: %s", status, status_text);
			} else {
				reason = g_strdup(status_text ? : status);
			}

			s = strchr(bud->ext_jid, '/');
			if (s) {
				*s = 0;
			}
			imcb_chat_remove_buddy(chat, bud->ext_jid, reason);
			if (bud != jc->me && bud->flags & JBFLAG_IS_ANONYMOUS) {
				imcb_remove_buddy(ic, bud->ext_jid, reason);
			}
			if (s) {
				*s = '/';
			}

			g_free(reason);
		}

		if (bud == jc->me) {
			jabber_chat_free(chat);
		}
	}
}

void jabber_chat_pkt_message(struct im_connection *ic, struct jabber_buddy *bud, struct xt_node *node)
{
	struct xt_node *subject = xt_find_node(node->children, "subject");
	struct xt_node *body = xt_find_node(node->children, "body");
	struct groupchat *chat = NULL;
	struct jabber_chat *jc = NULL;
	char *from = NULL;
	char *nick = NULL;
	char *final_from = NULL;
	char *bare_jid = NULL;

	from = (bud) ? bud->full_jid : xt_find_attr(node, "from");

	if (from) {
		nick = strchr(from, '/');
		if (nick) {
			*nick = 0;
		}
		chat = jabber_chat_by_jid(ic, from);
		if (nick) {
			*nick = '/';
			nick++;
		}
	}

	jc = (chat) ? chat->data : NULL;

	if (!bud) {
		struct xt_node *c;
		char *s;

		/* Try some clever stuff to find out the real JID here */
		c = xt_find_node_by_attr(node->children, "delay", "xmlns", XMLNS_DELAY);

		if (c && ((s = xt_find_attr(c, "from")) ||
		          (s = xt_find_attr(c, "from_jid")))) {
			/* This won't be useful if it's the MUC JID */
			if (!(jc && jabber_compare_jid(s, jc->name))) {
				/* Hopefully this one makes more sense! */
				bud = jabber_buddy_by_jid(ic, s, GET_BUDDY_FIRST | GET_BUDDY_CREAT);
			}
		}

	}

	if (subject && chat) {
		char *subject_text = subject->text_len > 0 ? subject->text : NULL;
		if (g_strcmp0(chat->topic, subject_text) != 0) {
			bare_jid = (bud) ? jabber_get_bare_jid(bud->ext_jid) : NULL;
			imcb_chat_topic(chat, bare_jid, subject_text,
			                jabber_get_timestamp(node));
			g_free(bare_jid);
		}
	}

	if (body == NULL || body->text_len == 0) {
		/* Meh. Empty messages aren't very interesting, no matter
		   how much some servers love to send them. */
		return;
	}

	if (chat == NULL) {
		if (nick == NULL) {
			imcb_log(ic, "System message from unknown groupchat %s: %s", from, body->text);
		} else {
			imcb_log(ic, "Groupchat message from unknown JID %s: %s", from, body->text);
		}

		return;
	} else if (chat != NULL && bud == NULL && nick == NULL) {
		imcb_chat_log(chat, "From conference server: %s", body->text);
		return;
	} else if (jc && jc->flags & JCFLAG_MESSAGE_SENT && bud == jc->me) {
		/* exclude self-messages since they would get filtered out
		 * but not the ones in the backlog */
		return;
	}

	if (bud && jc && bud != jc->me) {
		bare_jid = jabber_get_bare_jid(bud->ext_jid ? bud->ext_jid : bud->full_jid);
		final_from = bare_jid;
	} else {
		final_from = nick;
	}

	imcb_chat_msg(chat, final_from, body->text, 0, jabber_get_timestamp(node));

	g_free(bare_jid);
}
