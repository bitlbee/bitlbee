/*
 * aim_rxhandlers.c
 *
 * This file contains most all of the incoming packet handlers, along
 * with aim_rxdispatch(), the Rx dispatcher.  Queue/list management is
 * actually done in aim_rxqueue.c.
 *
 */

#include <aim.h>

struct aim_rxcblist_s {
	guint16 family;
	guint16 type;
	aim_rxcallback_t handler;
	u_short flags;
	struct aim_rxcblist_s *next;
};

aim_module_t *aim__findmodulebygroup(aim_session_t *sess, guint16 group)
{
	aim_module_t *cur;

	for (cur = (aim_module_t *) sess->modlistv; cur; cur = cur->next) {
		if (cur->family == group) {
			return cur;
		}
	}

	return NULL;
}

static aim_module_t *aim__findmodule(aim_session_t *sess, const char *name)
{
	aim_module_t *cur;

	for (cur = (aim_module_t *) sess->modlistv; cur; cur = cur->next) {
		if (strcmp(name, cur->name) == 0) {
			return cur;
		}
	}

	return NULL;
}

int aim__registermodule(aim_session_t *sess, int (*modfirst)(aim_session_t *, aim_module_t *))
{
	aim_module_t *mod;

	if (!sess || !modfirst) {
		return -1;
	}

	if (!(mod = g_new0(aim_module_t, 1))) {
		return -1;
	}

	if (modfirst(sess, mod) == -1) {
		g_free(mod);
		return -1;
	}

	if (aim__findmodule(sess, mod->name)) {
		if (mod->shutdown) {
			mod->shutdown(sess, mod);
		}
		g_free(mod);
		return -1;
	}

	mod->next = (aim_module_t *) sess->modlistv;
	sess->modlistv = mod;


	return 0;
}

void aim__shutdownmodules(aim_session_t *sess)
{
	aim_module_t *cur;

	for (cur = (aim_module_t *) sess->modlistv; cur; ) {
		aim_module_t *tmp;

		tmp = cur->next;

		if (cur->shutdown) {
			cur->shutdown(sess, cur);
		}

		g_free(cur);

		cur = tmp;
	}

	sess->modlistv = NULL;

	return;
}

static int consumesnac(aim_session_t *sess, aim_frame_t *rx)
{
	aim_module_t *cur;
	aim_modsnac_t snac;

	if (aim_bstream_empty(&rx->data) < 10) {
		return 0;
	}

	snac.family = aimbs_get16(&rx->data);
	snac.subtype = aimbs_get16(&rx->data);
	snac.flags = aimbs_get16(&rx->data);
	snac.id = aimbs_get32(&rx->data);

	/* Contains TLV(s) in the FNAC header */
	if (snac.flags & 0x8000) {
		aim_bstream_advance(&rx->data, aimbs_get16(&rx->data));
	} else if (snac.flags & 0x0001) {
		/* Following SNAC will be related */
	}

	for (cur = (aim_module_t *) sess->modlistv; cur; cur = cur->next) {

		if (!(cur->flags & AIM_MODFLAG_MULTIFAMILY) &&
		    (cur->family != snac.family)) {
			continue;
		}

		if (cur->snachandler(sess, cur, rx, &snac, &rx->data)) {
			return 1;
		}

	}

	return 0;
}

static int consumenonsnac(aim_session_t *sess, aim_frame_t *rx, guint16 family, guint16 subtype)
{
	aim_module_t *cur;
	aim_modsnac_t snac;

	snac.family = family;
	snac.subtype = subtype;
	snac.flags = snac.id = 0;

	for (cur = (aim_module_t *) sess->modlistv; cur; cur = cur->next) {

		if (!(cur->flags & AIM_MODFLAG_MULTIFAMILY) &&
		    (cur->family != snac.family)) {
			continue;
		}

		if (cur->snachandler(sess, cur, rx, &snac, &rx->data)) {
			return 1;
		}

	}

	return 0;
}

static int negchan_middle(aim_session_t *sess, aim_frame_t *fr)
{
	aim_tlvlist_t *tlvlist;
	char *msg = NULL;
	guint16 code = 0;
	aim_rxcallback_t userfunc;
	int ret = 1;

	if (aim_bstream_empty(&fr->data) == 0) {
		/* XXX should do something with this */
		return 1;
	}

	/* Used only by the older login protocol */
	/* XXX remove this special case? */
	if (fr->conn->type == AIM_CONN_TYPE_AUTH) {
		return consumenonsnac(sess, fr, 0x0017, 0x0003);
	}

	tlvlist = aim_readtlvchain(&fr->data);

	if (aim_gettlv(tlvlist, 0x0009, 1)) {
		code = aim_gettlv16(tlvlist, 0x0009, 1);
	}

	if (aim_gettlv(tlvlist, 0x000b, 1)) {
		msg = aim_gettlv_str(tlvlist, 0x000b, 1);
	}

	if ((userfunc = aim_callhandler(sess, fr->conn, AIM_CB_FAM_SPECIAL, AIM_CB_SPECIAL_CONNERR))) {
		ret = userfunc(sess, fr, code, msg);
	}

	aim_freetlvchain(&tlvlist);

	g_free(msg);

	return ret;
}

/*
 * Some SNACs we do not allow to be hooked, for good reason.
 */
static int checkdisallowed(guint16 group, guint16 type)
{
	static const struct {
		guint16 group;
		guint16 type;
	} dontuse[] = {
		{ 0x0001, 0x0002 },
		{ 0x0001, 0x0003 },
		{ 0x0001, 0x0006 },
		{ 0x0001, 0x0007 },
		{ 0x0001, 0x0008 },
		{ 0x0001, 0x0017 },
		{ 0x0001, 0x0018 },
		{ 0x0000, 0x0000 }
	};
	int i;

	for (i = 0; dontuse[i].group != 0x0000; i++) {
		if ((dontuse[i].group == group) && (dontuse[i].type == type)) {
			return 1;
		}
	}

	return 0;
}

int aim_conn_addhandler(aim_session_t *sess, aim_conn_t *conn, guint16 family, guint16 type,
                        aim_rxcallback_t newhandler, guint16 flags)
{
	struct aim_rxcblist_s *newcb;

	if (!conn) {
		return -1;
	}

	if (checkdisallowed(family, type)) {
		g_assert(0);
		return -1;
	}

	if (!(newcb = (struct aim_rxcblist_s *) g_new0(struct aim_rxcblist_s, 1))) {
		return -1;
	}

	newcb->family = family;
	newcb->type = type;
	newcb->flags = flags;
	newcb->handler = newhandler;
	newcb->next = NULL;

	if (!conn->handlerlist) {
		conn->handlerlist = (void *) newcb;
	} else {
		struct aim_rxcblist_s *cur;

		for (cur = (struct aim_rxcblist_s *) conn->handlerlist; cur->next; cur = cur->next) {
			;
		}
		cur->next = newcb;
	}

	return 0;
}

int aim_clearhandlers(aim_conn_t *conn)
{
	struct aim_rxcblist_s *cur;

	if (!conn) {
		return -1;
	}

	for (cur = (struct aim_rxcblist_s *) conn->handlerlist; cur; ) {
		struct aim_rxcblist_s *tmp;

		tmp = cur->next;
		g_free(cur);
		cur = tmp;
	}
	conn->handlerlist = NULL;

	return 0;
}

aim_rxcallback_t aim_callhandler(aim_session_t *sess, aim_conn_t *conn, guint16 family, guint16 type)
{
	struct aim_rxcblist_s *cur;

	if (!conn) {
		return NULL;
	}

	for (cur = (struct aim_rxcblist_s *) conn->handlerlist; cur; cur = cur->next) {
		if ((cur->family == family) && (cur->type == type)) {
			return cur->handler;
		}
	}

	if (type == AIM_CB_SPECIAL_DEFAULT) {
		return NULL; /* prevent infinite recursion */
	}

	return aim_callhandler(sess, conn, family, AIM_CB_SPECIAL_DEFAULT);
}

static int aim_callhandler_noparam(aim_session_t *sess, aim_conn_t *conn, guint16 family, guint16 type,
                                   aim_frame_t *ptr)
{
	aim_rxcallback_t userfunc;

	if ((userfunc = aim_callhandler(sess, conn, family, type))) {
		return userfunc(sess, ptr);
	}

	return 1; /* XXX */
}

/*
 * aim_rxdispatch()
 *
 * Basically, heres what this should do:
 *   1) Determine correct packet handler for this packet
 *   2) Mark the packet handled (so it can be dequeued in purge_queue())
 *   3) Send the packet to the packet handler
 *   4) Go to next packet in the queue and start over
 *   5) When done, run purge_queue() to purge handled commands
 *
 * TODO: Clean up.
 * TODO: More support for mid-level handlers.
 * TODO: Allow for NULL handlers.
 *
 */
void aim_rxdispatch(aim_session_t *sess)
{
	int i;
	aim_frame_t *cur;

	for (cur = sess->queue_incoming, i = 0; cur; cur = cur->next, i++) {

		/*
		 * XXX: This is still fairly ugly.
		 */

		if (cur->handled) {
			continue;
		}

		if (cur->hdr.flap.type == 0x01) {

			cur->handled = aim_callhandler_noparam(sess, cur->conn, AIM_CB_FAM_SPECIAL,
			                                       AIM_CB_SPECIAL_FLAPVER, cur);                                      /* XXX use consumenonsnac */

			continue;

		} else if (cur->hdr.flap.type == 0x02) {

			if ((cur->handled = consumesnac(sess, cur))) {
				continue;
			}

		} else if (cur->hdr.flap.type == 0x04) {

			cur->handled = negchan_middle(sess, cur);
			continue;

		} else if (cur->hdr.flap.type == 0x05) {
			;
		}

		if (!cur->handled) {
			consumenonsnac(sess, cur, 0xffff, 0xffff); /* last chance! */
			cur->handled = 1;
		}
	}

	/*
	 * This doesn't have to be called here.  It could easily be done
	 * by a separate thread or something. It's an administrative operation,
	 * and can take a while. Though the less you call it the less memory
	 * you'll have :)
	 */
	aim_purge_rxqueue(sess);

	return;
}
