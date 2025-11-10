/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Jabber module - Handling of blocking                                     *
*                                                                           *
*  Copyright 2021 / <>                                                      *
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

void jabber_buddy_blockunblock(struct im_connection *ic, char *who, int block)
{
	struct xt_node *node, *query;
	struct jabber_buddy *bud;
	char *s;

	if ((s = strchr(who, '=')) && jabber_chat_by_jid(ic, s + 1)) {
		bud = jabber_buddy_by_ext_jid(ic, who, 0);
	} else {
		bud = jabber_buddy_by_jid(ic, who, GET_BUDDY_BARE_OK);
	}

	node = xt_new_node("item", NULL, NULL);
	xt_add_attr(node, "jid", bud ? bud->full_jid : who);

	node = xt_new_node(block ? "block" : "unblock", NULL, node);
	xt_add_attr(node, "xmlns", XMLNS_BLOCK);

	query = jabber_make_packet("iq", "set", NULL, node);

	jabber_write_packet(ic, query);
	xt_free_node(query);
}

void jabber_buddy_block(struct im_connection *ic, char *who)
{
	jabber_buddy_blockunblock(ic, who, TRUE);
}

void jabber_buddy_unblock(struct im_connection *ic, char *who)
{
	jabber_buddy_blockunblock(ic, who, FALSE);
}

//jabber doesn't have permit lists so these are empty
void jabber_buddy_permit(struct im_connection *ic, char *who)
{
}

void jabber_buddy_unpermit(struct im_connection *ic, char *who)
{
}
