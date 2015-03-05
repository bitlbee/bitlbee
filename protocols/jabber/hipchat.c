/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Jabber module - HipChat specific functions                               *
*                                                                           *
*  Copyright 2015 Xamarin Inc                                               *
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

xt_status hipchat_handle_success(struct im_connection *ic, struct xt_node *node)
{
	struct jabber_data *jd = ic->proto_data;
	char *sep, *jid, *muc_host;

	jid = xt_find_attr(node, "jid");
	muc_host = xt_find_attr(node, "muc_host");

	sep = strchr(jid, '/');
	if (sep) {
		*sep = '\0';
	}

	jabber_set_me(ic, jid);
	imcb_log(ic, "Setting Hipchat JID to %s", jid);

	if (sep) {
		*sep = '/';
	}

	/* Hipchat's auth doesn't expect a restart here */
	jd->flags &= ~JFLAG_STREAM_RESTART;

	if (!jabber_get_roster(ic)) {
		return XT_ABORT;
	}
	if (!jabber_iq_disco_server(ic)) {
		return XT_ABORT;
	}
	if (!jabber_get_hipchat_profile(ic)) {
		return XT_ABORT;
	}
	if (!jabber_iq_disco_muc(ic, muc_host)) {
		return XT_ABORT;
	}

	return XT_HANDLED;
}

int jabber_get_hipchat_profile(struct im_connection *ic)
{
	struct jabber_data *jd = ic->proto_data;
	struct xt_node *node;
	int st;

	imcb_log(ic, "Fetching hipchat profile for %s", jd->me);

	node = xt_new_node("query", NULL, NULL);
	xt_add_attr(node, "xmlns", XMLNS_HIPCHAT_PROFILE);
	node = jabber_make_packet("iq", "get", jd->me, node);

	jabber_cache_add(ic, node, jabber_parse_hipchat_profile);
	st = jabber_write_packet(ic, node);

	return st;
}

xt_status jabber_parse_hipchat_profile(struct im_connection *ic, struct xt_node *node, struct xt_node *orig)
{
	struct xt_node *query, *name_node;

	//char *name;

	if (!(query = xt_find_node(node->children, "query"))) {
		imcb_log(ic, "Warning: Received NULL profile packet");
		return XT_ABORT;
	}

	name_node = xt_find_node(query->children, "name");
	if (!name_node) {
		imcb_log(ic, "Warning: Can't find real name in profile. Joining groupchats will not be possible.");
		return XT_ABORT;
	}

	set_setstr(&ic->acc->set, "display_name", name_node->text);
	return XT_HANDLED;

}

int jabber_iq_disco_muc(struct im_connection *ic, char *muc_server)
{
	struct xt_node *node;
	int st;

	imcb_log(ic, "Fetching MUC list");

	node = xt_new_node("query", NULL, NULL);
	xt_add_attr(node, "xmlns", XMLNS_DISCO_ITEMS);
	node = jabber_make_packet("iq", "get", muc_server, node);

	jabber_cache_add(ic, node, jabber_parse_muc_list);
	st = jabber_write_packet(ic, node);

	return st;
}

xt_status jabber_parse_muc_list(struct im_connection *ic, struct xt_node *node, struct xt_node *orig)
{
	struct xt_node *query, *c;

	if (!(query = xt_find_node(node->children, "query"))) {
		imcb_log(ic, "Warning: Received NULL MUC list packet");
		return XT_HANDLED;
	}

	c = query->children;
	while ((c = xt_find_node(c, "item"))) {
		struct xt_node *c2;
		char *topic = NULL;
		char *jid = xt_find_attr(c, "jid");
		char *name = xt_find_attr(c, "name");

		imcb_log(ic, "Debug: adding MUC to channel list: %s - '%s'", jid, name);

		if ((c2 = xt_find_node_by_attr(c->children, "x", "xmlns", XMLNS_HIPCHAT_MUC)) &&
		    (c2 = xt_find_node(c2->children, "topic"))) {
			topic = c2->text;
		}

		imcb_chat_placeholder_new(ic, jid, name, topic);
		c = c->next;
	}
	return XT_HANDLED;

}
