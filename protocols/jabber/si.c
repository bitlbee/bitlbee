/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Jabber module - SI packets                                               *
*                                                                           *
*  Copyright 2007 Uli Meis <a.sporto+bee@gmail.com>                         *
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

void jabber_si_answer_request(file_transfer_t *ft);
int jabber_si_send_request(struct im_connection *ic, char *who, struct jabber_transfer *tf);

/* file_transfer free() callback */
void jabber_si_free_transfer(file_transfer_t *ft)
{
	struct jabber_transfer *tf = ft->data;
	struct jabber_data *jd = tf->ic->proto_data;

	if (tf->watch_in) {
		b_event_remove(tf->watch_in);
		tf->watch_in = 0;
	}

	jd->filetransfers = g_slist_remove(jd->filetransfers, tf);

	if (tf->fd != -1) {
		closesocket(tf->fd);
		tf->fd = -1;
	}

	if (tf->disco_timeout) {
		b_event_remove(tf->disco_timeout);
	}

	g_free(tf->ini_jid);
	g_free(tf->tgt_jid);
	g_free(tf->iq_id);
	g_free(tf->sid);
	g_free(tf);
}

/* file_transfer canceled() callback */
void jabber_si_canceled(file_transfer_t *ft, char *reason)
{
	struct jabber_transfer *tf = ft->data;
	struct xt_node *reply, *iqnode;

	if (tf->accepted) {
		return;
	}

	iqnode = jabber_make_packet("iq", "error", tf->ini_jid, NULL);
	xt_add_attr(iqnode, "id", tf->iq_id);
	reply = jabber_make_error_packet(iqnode, "forbidden", "cancel", "403");
	xt_free_node(iqnode);

	if (!jabber_write_packet(tf->ic, reply)) {
		imcb_log(tf->ic, "WARNING: Error generating reply to file transfer request");
	}
	xt_free_node(reply);

}

int jabber_si_check_features(struct jabber_transfer *tf, GSList *features)
{
	int foundft = FALSE, foundbt = FALSE, foundsi = FALSE;

	while (features) {
		if (!strcmp(features->data, XMLNS_FILETRANSFER)) {
			foundft = TRUE;
		}
		if (!strcmp(features->data, XMLNS_BYTESTREAMS)) {
			foundbt = TRUE;
		}
		if (!strcmp(features->data, XMLNS_SI)) {
			foundsi = TRUE;
		}

		features = g_slist_next(features);
	}

	if (!foundft) {
		imcb_file_canceled(tf->ic, tf->ft, "Buddy's client doesn't feature file transfers");
	} else if (!foundbt) {
		imcb_file_canceled(tf->ic, tf->ft, "Buddy's client doesn't feature byte streams (required)");
	} else if (!foundsi) {
		imcb_file_canceled(tf->ic, tf->ft, "Buddy's client doesn't feature stream initiation (required)");
	}

	return foundft && foundbt && foundsi;
}

void jabber_si_transfer_start(struct jabber_transfer *tf)
{

	if (!jabber_si_check_features(tf, tf->bud->features)) {
		return;
	}

	/* send the request to our buddy */
	jabber_si_send_request(tf->ic, tf->bud->full_jid, tf);

	/* and start the receive logic */
	imcb_file_recv_start(tf->ic, tf->ft);

}

gboolean jabber_si_waitfor_disco(gpointer data, gint fd, b_input_condition cond)
{
	struct jabber_transfer *tf = data;
	struct jabber_data *jd = tf->ic->proto_data;

	tf->disco_timeout_fired++;

	if (tf->bud->features && jd->have_streamhosts == 1) {
		tf->disco_timeout = 0;
		jabber_si_transfer_start(tf);
		return FALSE;
	}

	/* 8 seconds should be enough for server and buddy to respond */
	if (tf->disco_timeout_fired < 16) {
		return TRUE;
	}

	if (!tf->bud->features && jd->have_streamhosts != 1) {
		imcb_log(tf->ic, "Couldn't get buddy's features nor discover all services of the server");
	} else if (!tf->bud->features) {
		imcb_log(tf->ic, "Couldn't get buddy's features");
	} else {
		imcb_log(tf->ic, "Couldn't discover some of the server's services");
	}

	tf->disco_timeout = 0;
	jabber_si_transfer_start(tf);
	return FALSE;
}

void jabber_si_transfer_request(struct im_connection *ic, file_transfer_t *ft, char *who)
{
	struct jabber_transfer *tf;
	struct jabber_data *jd = ic->proto_data;
	struct jabber_buddy *bud;
	char *server = jd->server, *s;

	if ((s = strchr(who, '=')) && jabber_chat_by_jid(ic, s + 1)) {
		bud = jabber_buddy_by_ext_jid(ic, who, 0);
	} else {
		bud = jabber_buddy_by_jid(ic, who, 0);
	}

	if (bud == NULL) {
		imcb_file_canceled(ic, ft, "Couldn't find buddy (BUG?)");
		return;
	}

	imcb_log(ic, "Trying to send %s(%zd bytes) to %s", ft->file_name, ft->file_size, who);

	tf = g_new0(struct jabber_transfer, 1);

	tf->ic = ic;
	tf->ft = ft;
	tf->fd = -1;
	tf->ft->data = tf;
	tf->ft->free = jabber_si_free_transfer;
	tf->bud = bud;
	ft->write = jabber_bs_send_write;

	jd->filetransfers = g_slist_prepend(jd->filetransfers, tf);

	/* query buddy's features and server's streaming proxies if necessary */

	if (!tf->bud->features) {
		jabber_iq_query_features(ic, bud->full_jid);
	}

	/* If <auto> is not set don't check for proxies */
	if ((jd->have_streamhosts != 1) && (jd->streamhosts == NULL) &&
	    (strstr(set_getstr(&ic->acc->set, "proxy"), "<auto>") != NULL)) {
		jd->have_streamhosts = 0;
		jabber_iq_query_server(ic, server, XMLNS_DISCO_ITEMS);
	} else if (jd->streamhosts != NULL) {
		jd->have_streamhosts = 1;
	}

	/* if we had to do a query, wait for the result.
	 * Otherwise fire away. */
	if (!tf->bud->features || jd->have_streamhosts != 1) {
		tf->disco_timeout = b_timeout_add(500, jabber_si_waitfor_disco, tf);
	} else {
		jabber_si_transfer_start(tf);
	}
}

/*
 * First function that gets called when a file transfer request comes in.
 * A lot to parse.
 *
 * We choose a stream type from the options given by the initiator.
 * Then we wait for imcb to call the accept or cancel callbacks.
 */
int jabber_si_handle_request(struct im_connection *ic, struct xt_node *node, struct xt_node *sinode)
{
	struct xt_node *c, *d, *reply;
	char *sid, *ini_jid, *tgt_jid, *iq_id, *s, *ext_jid, *size_s;
	struct jabber_buddy *bud;
	int requestok = FALSE;
	char *name, *cmp;
	size_t size;
	struct jabber_transfer *tf;
	struct jabber_data *jd = ic->proto_data;
	file_transfer_t *ft;

	/* All this means we expect something like this: ( I think )
	 * <iq from=... to=... id=...>
	 *      <si id=id xmlns=si profile=ft>
	 *              <file xmlns=ft/>
	 *              <feature xmlns=feature>
	 *                      <x xmlns=xdata type=submit>
	 *                              <field var=stream-method>
	 *
	 */
	if (!(ini_jid          = xt_find_attr(node, "from")) ||
	    !(tgt_jid          = xt_find_attr(node, "to")) ||
	    !(iq_id            = xt_find_attr(node, "id")) ||
	    !(sid              = xt_find_attr(sinode, "id")) ||
	    !(cmp              = xt_find_attr(sinode, "profile")) ||
	    !(0               == strcmp(cmp, XMLNS_FILETRANSFER)) ||
	    !(d                = xt_find_node(sinode->children, "file")) ||
	    !(cmp = xt_find_attr(d, "xmlns")) ||
	    !(0               == strcmp(cmp, XMLNS_FILETRANSFER)) ||
	    !(name             = xt_find_attr(d, "name")) ||
	    !(size_s           = xt_find_attr(d, "size")) ||
	    !(1               == sscanf(size_s, "%zd", &size)) ||
	    !(d                = xt_find_node(sinode->children, "feature")) ||
	    !(cmp              = xt_find_attr(d, "xmlns")) ||
	    !(0               == strcmp(cmp, XMLNS_FEATURE)) ||
	    !(d                = xt_find_node(d->children, "x")) ||
	    !(cmp              = xt_find_attr(d, "xmlns")) ||
	    !(0               == strcmp(cmp, XMLNS_XDATA)) ||
	    !(cmp              = xt_find_attr(d, "type")) ||
	    !(0               == strcmp(cmp, "form")) ||
	    !(d                = xt_find_node(d->children, "field")) ||
	    !(cmp              = xt_find_attr(d, "var")) ||
	    !(0               == strcmp(cmp, "stream-method"))) {
		imcb_log(ic, "WARNING: Received incomplete Stream Initiation request");
	} else {
		/* Check if we support one of the options */

		c = d->children;
		while ((c = xt_find_node(c, "option"))) {
			if ((d = xt_find_node(c->children, "value")) &&
			    (d->text != NULL) &&
			    (strcmp(d->text, XMLNS_BYTESTREAMS) == 0)) {
				requestok = TRUE;
				break;
			} else {
				c = c->next;
			}
		}

		if (!requestok) {
			imcb_log(ic, "WARNING: Unsupported file transfer request from %s", ini_jid);
		}
	}

	if (requestok) {
		/* Figure out who the transfer should come from... */

		ext_jid = ini_jid;
		if ((s = strchr(ini_jid, '/'))) {
			if ((bud = jabber_buddy_by_jid(ic, ini_jid, GET_BUDDY_EXACT))) {
				bud->last_msg = time(NULL);
				ext_jid = bud->ext_jid ? : bud->bare_jid;
			} else {
				*s = 0; /* We need to generate a bare JID now. */
			}
		}

		if (!(ft = imcb_file_send_start(ic, ext_jid, name, size))) {
			imcb_log(ic, "WARNING: Error handling transfer request from %s", ini_jid);
			requestok = FALSE;
		}

		if (s) {
			*s = '/';
		}
	}

	if (!requestok) {
		reply = jabber_make_error_packet(node, "item-not-found", "cancel", NULL);
		if (!jabber_write_packet(ic, reply)) {
			imcb_log(ic, "WARNING: Error generating reply to file transfer request");
		}
		xt_free_node(reply);
		return XT_HANDLED;
	}

	/* Request is fine. */

	tf = g_new0(struct jabber_transfer, 1);

	tf->ini_jid = g_strdup(ini_jid);
	tf->tgt_jid = g_strdup(tgt_jid);
	tf->iq_id = g_strdup(iq_id);
	tf->sid = g_strdup(sid);
	tf->ic = ic;
	tf->ft = ft;
	tf->fd = -1;
	tf->ft->data = tf;
	tf->ft->accept = jabber_si_answer_request;
	tf->ft->free = jabber_si_free_transfer;
	tf->ft->canceled = jabber_si_canceled;

	jd->filetransfers = g_slist_prepend(jd->filetransfers, tf);

	return XT_HANDLED;
}

/*
 * imc called the accept callback which probably means that the user accepted this file transfer.
 * We send our response to the initiator.
 * In the next step, the initiator will send us a request for the given stream type.
 * (currently that can only be a SOCKS5 bytestream)
 */
void jabber_si_answer_request(file_transfer_t *ft)
{
	struct jabber_transfer *tf = ft->data;
	struct xt_node *node, *sinode, *reply;

	/* generate response, start with the SI tag */
	sinode = xt_new_node("si", NULL, NULL);
	xt_add_attr(sinode, "xmlns", XMLNS_SI);
	xt_add_attr(sinode, "profile", XMLNS_FILETRANSFER);
	xt_add_attr(sinode, "id", tf->sid);

	/* now the file tag */
	node = xt_new_node("file", NULL, NULL);
	xt_add_attr(node, "xmlns", XMLNS_FILETRANSFER);

	xt_add_child(sinode, node);

	/* and finally the feature tag */
	node = xt_new_node("field", NULL, NULL);
	xt_add_attr(node, "var", "stream-method");
	xt_add_attr(node, "type", "list-single");

	/* Currently all we can do. One could also implement in-band (IBB) */
	xt_add_child(node, xt_new_node("value", XMLNS_BYTESTREAMS, NULL));

	node = xt_new_node("x", NULL, node);
	xt_add_attr(node, "xmlns", XMLNS_XDATA);
	xt_add_attr(node, "type", "submit");

	node = xt_new_node("feature", NULL, node);
	xt_add_attr(node, "xmlns", XMLNS_FEATURE);

	xt_add_child(sinode, node);

	reply = jabber_make_packet("iq", "result", tf->ini_jid, sinode);
	xt_add_attr(reply, "id", tf->iq_id);

	if (!jabber_write_packet(tf->ic, reply)) {
		imcb_log(tf->ic, "WARNING: Error generating reply to file transfer request");
	} else {
		tf->accepted = TRUE;
	}
	xt_free_node(reply);
}

static xt_status jabber_si_handle_response(struct im_connection *ic, struct xt_node *node, struct xt_node *orig)
{
	struct xt_node *c, *d;
	char *ini_jid = NULL, *tgt_jid, *iq_id, *cmp;
	GSList *tflist;
	struct jabber_transfer *tf = NULL;
	struct jabber_data *jd = ic->proto_data;

	if (!(tgt_jid = xt_find_attr(node, "from")) ||
	    !(ini_jid = xt_find_attr(node, "to"))) {
		imcb_log(ic, "Invalid SI response from=%s to=%s", tgt_jid, ini_jid);
		return XT_HANDLED;
	}

	/* All this means we expect something like this: ( I think )
	 * <iq from=... to=... id=...>
	 *      <si xmlns=si>
	 *      [	<file xmlns=ft/>    ] <-- not necessary
	 *              <feature xmlns=feature>
	 *                      <x xmlns=xdata type=submit>
	 *                              <field var=stream-method>
	 *                                      <value>
	 */
	if (!(tgt_jid = xt_find_attr(node, "from")) ||
	    !(ini_jid = xt_find_attr(node, "to")) ||
	    !(iq_id   = xt_find_attr(node, "id")) ||
	    !(c = xt_find_node(node->children, "si")) ||
	    !(cmp = xt_find_attr(c, "xmlns")) ||
	    !(strcmp(cmp, XMLNS_SI) == 0) ||
	    !(d = xt_find_node(c->children, "feature")) ||
	    !(cmp = xt_find_attr(d, "xmlns")) ||
	    !(strcmp(cmp, XMLNS_FEATURE) == 0) ||
	    !(d = xt_find_node(d->children, "x")) ||
	    !(cmp = xt_find_attr(d, "xmlns")) ||
	    !(strcmp(cmp, XMLNS_XDATA) == 0) ||
	    !(cmp = xt_find_attr(d, "type")) ||
	    !(strcmp(cmp, "submit") == 0) ||
	    !(d = xt_find_node(d->children, "field")) ||
	    !(cmp = xt_find_attr(d, "var")) ||
	    !(strcmp(cmp, "stream-method") == 0) ||
	    !(d = xt_find_node(d->children, "value"))) {
		imcb_log(ic, "WARNING: Received incomplete Stream Initiation response");
		return XT_HANDLED;
	}

	if (!(strcmp(d->text, XMLNS_BYTESTREAMS) == 0)) {
		/* since we should only have advertised what we can do and the peer should
		 * only have chosen what we offered, this should never happen */
		imcb_log(ic, "WARNING: Received invalid Stream Initiation response, method %s", d->text);

		return XT_HANDLED;
	}

	/* Let's see if we can find out what this bytestream should be for... */

	for (tflist = jd->filetransfers; tflist; tflist = g_slist_next(tflist)) {
		struct jabber_transfer *tft = tflist->data;
		if ((strcmp(tft->iq_id, iq_id) == 0)) {
			tf = tft;
			break;
		}
	}

	if (!tf) {
		imcb_log(ic, "WARNING: Received bytestream request from %s that doesn't match an SI request", ini_jid);
		return XT_HANDLED;
	}

	tf->ini_jid = g_strdup(ini_jid);
	tf->tgt_jid = g_strdup(tgt_jid);

	imcb_log(ic, "File %s: %s accepted the transfer!", tf->ft->file_name, tgt_jid);

	jabber_bs_send_start(tf);

	return XT_HANDLED;
}

int jabber_si_send_request(struct im_connection *ic, char *who, struct jabber_transfer *tf)
{
	struct xt_node *node, *sinode;
	struct jabber_buddy *bud;

	/* who knows how many bits the future holds :) */
	char filesizestr[ 1 + ( int ) (0.301029995663981198f * sizeof(size_t) * 8) ];

	const char *methods[] =
	{
		XMLNS_BYTESTREAMS,
		//XMLNS_IBB,
		NULL
	};
	const char **m;
	char *s;

	/* Maybe we should hash this? */
	tf->sid = g_strdup_printf("BitlBeeJabberSID%d", tf->ft->local_id);

	if ((s = strchr(who, '=')) && jabber_chat_by_jid(ic, s + 1)) {
		bud = jabber_buddy_by_ext_jid(ic, who, 0);
	} else {
		bud = jabber_buddy_by_jid(ic, who, 0);
	}

	/* start with the SI tag */
	sinode = xt_new_node("si", NULL, NULL);
	xt_add_attr(sinode, "xmlns", XMLNS_SI);
	xt_add_attr(sinode, "profile", XMLNS_FILETRANSFER);
	xt_add_attr(sinode, "id", tf->sid);

/*	if( mimetype )
                xt_add_attr( node, "mime-type", mimetype ); */

	/* now the file tag */
/*	if( desc )
                node = xt_new_node( "desc", descr, NULL ); */
	node = xt_new_node("range", NULL, NULL);

	sprintf(filesizestr, "%zd", tf->ft->file_size);
	node = xt_new_node("file", NULL, node);
	xt_add_attr(node, "xmlns", XMLNS_FILETRANSFER);
	xt_add_attr(node, "name", tf->ft->file_name);
	xt_add_attr(node, "size", filesizestr);
/*	if (hash)
                xt_add_attr( node, "hash", hash );
        if (date)
                xt_add_attr( node, "date", date ); */

	xt_add_child(sinode, node);

	/* and finally the feature tag */
	node = xt_new_node("field", NULL, NULL);
	xt_add_attr(node, "var", "stream-method");
	xt_add_attr(node, "type", "list-single");

	for (m = methods; *m; m++) {
		xt_add_child(node, xt_new_node("option", NULL, xt_new_node("value", (char *) *m, NULL)));
	}

	node = xt_new_node("x", NULL, node);
	xt_add_attr(node, "xmlns", XMLNS_XDATA);
	xt_add_attr(node, "type", "form");

	node = xt_new_node("feature", NULL, node);
	xt_add_attr(node, "xmlns", XMLNS_FEATURE);

	xt_add_child(sinode, node);

	/* and we are there... */
	node = jabber_make_packet("iq", "set", bud ? bud->full_jid : who, sinode);
	jabber_cache_add(ic, node, jabber_si_handle_response);
	tf->iq_id = g_strdup(xt_find_attr(node, "id"));

	return jabber_write_packet(ic, node);
}
