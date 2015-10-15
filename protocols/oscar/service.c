/*
 * Group 1.  This is a very special group.  All connections support
 * this group, as it does some particularly good things (like rate limiting).
 */

#include <aim.h>

#include "md5.h"

/* Client Online (group 1, subtype 2) */
int aim_clientready(aim_session_t *sess, aim_conn_t *conn)
{
	aim_conn_inside_t *ins = (aim_conn_inside_t *) conn->inside;
	struct snacgroup *sg;
	aim_frame_t *fr;
	aim_snacid_t snacid;

	if (!ins) {
		return -EINVAL;
	}

	if (!(fr = aim_tx_new(sess, conn, AIM_FRAMETYPE_FLAP, 0x02, 1152))) {
		return -ENOMEM;
	}

	snacid = aim_cachesnac(sess, 0x0001, 0x0002, 0x0000, NULL, 0);
	aim_putsnac(&fr->data, 0x0001, 0x0002, 0x0000, snacid);

	/*
	 * Send only the tool versions that the server cares about (that it
	 * marked as supporting in the server ready SNAC).
	 */
	for (sg = ins->groups; sg; sg = sg->next) {
		aim_module_t *mod;

		if ((mod = aim__findmodulebygroup(sess, sg->group))) {
			aimbs_put16(&fr->data, mod->family);
			aimbs_put16(&fr->data, mod->version);
			aimbs_put16(&fr->data, mod->toolid);
			aimbs_put16(&fr->data, mod->toolversion);
		}
	}

	aim_tx_enqueue(sess, fr);

	return 0;
}

/*
 * Host Online (group 1, type 3)
 *
 * See comments in conn.c about how the group associations are supposed
 * to work, and how they really work.
 *
 * This info probably doesn't even need to make it to the client.
 *
 * We don't actually call the client here.  This starts off the connection
 * initialization routine required by all AIM connections.  The next time
 * the client is called is the CONNINITDONE callback, which should be
 * shortly after the rate information is acknowledged.
 *
 */
static int hostonline(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{
	guint16 *families;
	int famcount;


	if (!(families = g_malloc(aim_bstream_empty(bs)))) {
		return 0;
	}

	for (famcount = 0; aim_bstream_empty(bs); famcount++) {
		families[famcount] = aimbs_get16(bs);
		aim_conn_addgroup(rx->conn, families[famcount]);
	}

	g_free(families);


	/*
	 * Next step is in the Host Versions handler.
	 *
	 * Note that we must send this before we request rates, since
	 * the format of the rate information depends on the versions we
	 * give it.
	 *
	 */
	aim_setversions(sess, rx->conn);

	return 1;
}

/* Service request (group 1, type 4) */
int aim_reqservice(aim_session_t *sess, aim_conn_t *conn, guint16 serviceid)
{
	return aim_genericreq_s(sess, conn, 0x0001, 0x0004, &serviceid);
}

/* Redirect (group 1, type 5) */
static int redirect(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{
	struct aim_redirect_data redir;
	aim_rxcallback_t userfunc;
	aim_tlvlist_t *tlvlist;
	aim_snac_t *origsnac = NULL;
	int ret = 0;

	memset(&redir, 0, sizeof(redir));

	tlvlist = aim_readtlvchain(bs);

	if (!aim_gettlv(tlvlist, 0x000d, 1) ||
	    !aim_gettlv(tlvlist, 0x0005, 1) ||
	    !aim_gettlv(tlvlist, 0x0006, 1)) {
		aim_freetlvchain(&tlvlist);
		return 0;
	}

	redir.group = aim_gettlv16(tlvlist, 0x000d, 1);
	redir.ip = aim_gettlv_str(tlvlist, 0x0005, 1);
	redir.cookie = (guint8 *) aim_gettlv_str(tlvlist, 0x0006, 1);

	/* Fetch original SNAC so we can get csi if needed */
	origsnac = aim_remsnac(sess, snac->id);

	if ((redir.group == AIM_CONN_TYPE_CHAT) && origsnac) {
		struct chatsnacinfo *csi = (struct chatsnacinfo *) origsnac->data;

		redir.chat.exchange = csi->exchange;
		redir.chat.room = csi->name;
		redir.chat.instance = csi->instance;
	}

	if ((userfunc = aim_callhandler(sess, rx->conn, snac->family, snac->subtype))) {
		ret = userfunc(sess, rx, &redir);
	}

	g_free((void *) redir.ip);
	g_free((void *) redir.cookie);

	if (origsnac) {
		g_free(origsnac->data);
	}
	g_free(origsnac);

	aim_freetlvchain(&tlvlist);

	return ret;
}

/* Request Rate Information. (group 1, type 6) */
int aim_reqrates(aim_session_t *sess, aim_conn_t *conn)
{
	return aim_genericreq_n(sess, conn, 0x0001, 0x0006);
}

/*
 * OSCAR defines several 'rate classes'.  Each class has separate
 * rate limiting properties (limit level, alert level, disconnect
 * level, etc), and a set of SNAC family/type pairs associated with
 * it.  The rate classes, their limiting properties, and the definitions
 * of which SNACs are belong to which class, are defined in the
 * Rate Response packet at login to each host.
 *
 * Logically, all rate offenses within one class count against further
 * offenses for other SNACs in the same class (ie, sending messages
 * too fast will limit the number of user info requests you can send,
 * since those two SNACs are in the same rate class).
 *
 * Since the rate classes are defined dynamically at login, the values
 * below may change. But they seem to be fairly constant.
 *
 * Currently, BOS defines five rate classes, with the commonly used
 * members as follows...
 *
 *  Rate class 0x0001:
 *      - Everything thats not in any of the other classes
 *
 *  Rate class 0x0002:
 *      - Buddy list add/remove
 *	- Permit list add/remove
 *	- Deny list add/remove
 *
 *  Rate class 0x0003:
 *	- User information requests
 *	- Outgoing ICBMs
 *
 *  Rate class 0x0004:
 *	- A few unknowns: 2/9, 2/b, and f/2
 *
 *  Rate class 0x0005:
 *	- Chat room create
 *	- Outgoing chat ICBMs
 *
 * The only other thing of note is that class 5 (chat) has slightly looser
 * limiting properties than class 3 (normal messages).  But thats just a
 * small bit of trivia for you.
 *
 * The last thing that needs to be learned about the rate limiting
 * system is how the actual numbers relate to the passing of time.  This
 * seems to be a big mystery.
 *
 */

static void rc_addclass(struct rateclass **head, struct rateclass *inrc)
{
	struct rateclass *rc, *rc2;

	if (!(rc = g_malloc(sizeof(struct rateclass)))) {
		return;
	}

	memcpy(rc, inrc, sizeof(struct rateclass));
	rc->next = NULL;

	for (rc2 = *head; rc2 && rc2->next; rc2 = rc2->next) {
		;
	}

	if (!rc2) {
		*head = rc;
	} else {
		rc2->next = rc;
	}

	return;
}

static struct rateclass *rc_findclass(struct rateclass **head, guint16 id)
{
	struct rateclass *rc;

	for (rc = *head; rc; rc = rc->next) {
		if (rc->classid == id) {
			return rc;
		}
	}

	return NULL;
}

static void rc_addpair(struct rateclass *rc, guint16 group, guint16 type)
{
	struct snacpair *sp, *sp2;

	if (!(sp = g_new0(struct snacpair, 1))) {
		return;
	}

	sp->group = group;
	sp->subtype = type;
	sp->next = NULL;

	for (sp2 = rc->members; sp2 && sp2->next; sp2 = sp2->next) {
		;
	}

	if (!sp2) {
		rc->members = sp;
	} else {
		sp2->next = sp;
	}

	return;
}

/* Rate Parameters (group 1, type 7) */
static int rateresp(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{
	aim_conn_inside_t *ins = (aim_conn_inside_t *) rx->conn->inside;
	guint16 numclasses, i;
	aim_rxcallback_t userfunc;


	/*
	 * First are the parameters for each rate class.
	 */
	numclasses = aimbs_get16(bs);
	for (i = 0; i < numclasses; i++) {
		struct rateclass rc;

		memset(&rc, 0, sizeof(struct rateclass));

		rc.classid = aimbs_get16(bs);
		rc.windowsize = aimbs_get32(bs);
		rc.clear = aimbs_get32(bs);
		rc.alert = aimbs_get32(bs);
		rc.limit = aimbs_get32(bs);
		rc.disconnect = aimbs_get32(bs);
		rc.current = aimbs_get32(bs);
		rc.max = aimbs_get32(bs);

		/*
		 * The server will send an extra five bytes of parameters
		 * depending on the version we advertised in 1/17.  If we
		 * didn't send 1/17 (evil!), then this will crash and you
		 * die, as it will default to the old version but we have
		 * the new version hardcoded here.
		 */
		if (mod->version >= 3) {
			aimbs_getrawbuf(bs, rc.unknown, sizeof(rc.unknown));
		}

		rc_addclass(&ins->rates, &rc);
	}

	/*
	 * Then the members of each class.
	 */
	for (i = 0; i < numclasses; i++) {
		guint16 classid, count;
		struct rateclass *rc;
		int j;

		classid = aimbs_get16(bs);
		count = aimbs_get16(bs);

		rc = rc_findclass(&ins->rates, classid);

		for (j = 0; j < count; j++) {
			guint16 group, subtype;

			group = aimbs_get16(bs);
			subtype = aimbs_get16(bs);

			if (rc) {
				rc_addpair(rc, group, subtype);
			}
		}
	}

	/*
	 * We don't pass the rate information up to the client, as it really
	 * doesn't care.  The information is stored in the connection, however
	 * so that we can do more fun stuff later (not really).
	 */

	/*
	 * Last step in the conn init procedure is to acknowledge that we
	 * agree to these draconian limitations.
	 */
	aim_rates_addparam(sess, rx->conn);

	/*
	 * Finally, tell the client it's ready to go...
	 */
	if ((userfunc = aim_callhandler(sess, rx->conn, AIM_CB_FAM_SPECIAL, AIM_CB_SPECIAL_CONNINITDONE))) {
		userfunc(sess, rx);
	}


	return 1;
}

/* Add Rate Parameter (group 1, type 8) */
int aim_rates_addparam(aim_session_t *sess, aim_conn_t *conn)
{
	aim_conn_inside_t *ins = (aim_conn_inside_t *) conn->inside;
	aim_frame_t *fr;
	aim_snacid_t snacid;
	struct rateclass *rc;

	if (!(fr = aim_tx_new(sess, conn, AIM_FRAMETYPE_FLAP, 0x02, 512))) {
		return -ENOMEM;
	}

	snacid = aim_cachesnac(sess, 0x0001, 0x0008, 0x0000, NULL, 0);
	aim_putsnac(&fr->data, 0x0001, 0x0008, 0x0000, snacid);

	for (rc = ins->rates; rc; rc = rc->next) {
		aimbs_put16(&fr->data, rc->classid);
	}

	aim_tx_enqueue(sess, fr);

	return 0;
}

/* Rate Change (group 1, type 0x0a) */
static int ratechange(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{
	aim_rxcallback_t userfunc;
	guint16 code, rateclass;
	guint32 currentavg, maxavg, windowsize, clear, alert, limit, disconnect;

	code = aimbs_get16(bs);
	rateclass = aimbs_get16(bs);

	windowsize = aimbs_get32(bs);
	clear = aimbs_get32(bs);
	alert = aimbs_get32(bs);
	limit = aimbs_get32(bs);
	disconnect = aimbs_get32(bs);
	currentavg = aimbs_get32(bs);
	maxavg = aimbs_get32(bs);

	if ((userfunc = aim_callhandler(sess, rx->conn, snac->family, snac->subtype))) {
		return userfunc(sess, rx, code, rateclass, windowsize, clear, alert, limit, disconnect, currentavg,
		                maxavg);
	}

	return 0;
}

/*
 * How Migrations work.
 *
 * The server sends a Server Pause message, which the client should respond to
 * with a Server Pause Ack, which contains the families it needs on this
 * connection. The server will send a Migration Notice with an IP address, and
 * then disconnect. Next the client should open the connection and send the
 * cookie.  Repeat the normal login process and pretend this never happened.
 *
 * The Server Pause contains no data.
 *
 */

/* Service Pause (group 1, type 0x0b) */
static int serverpause(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{
	aim_rxcallback_t userfunc;

	if ((userfunc = aim_callhandler(sess, rx->conn, snac->family, snac->subtype))) {
		return userfunc(sess, rx);
	}

	return 0;
}

/* Service Resume (group 1, type 0x0d) */
static int serverresume(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{
	aim_rxcallback_t userfunc;

	if ((userfunc = aim_callhandler(sess, rx->conn, snac->family, snac->subtype))) {
		return userfunc(sess, rx);
	}

	return 0;
}

/* Request self-info (group 1, type 0x0e) */
int aim_reqpersonalinfo(aim_session_t *sess, aim_conn_t *conn)
{
	return aim_genericreq_n(sess, conn, 0x0001, 0x000e);
}

/* Self User Info (group 1, type 0x0f) */
static int selfinfo(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{
	aim_rxcallback_t userfunc;
	aim_userinfo_t userinfo;

	aim_extractuserinfo(sess, bs, &userinfo);

	if ((userfunc = aim_callhandler(sess, rx->conn, snac->family, snac->subtype))) {
		return userfunc(sess, rx, &userinfo);
	}

	return 0;
}

/* Evil Notification (group 1, type 0x10) */
static int evilnotify(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{
	aim_rxcallback_t userfunc;
	guint16 newevil;
	aim_userinfo_t userinfo;

	memset(&userinfo, 0, sizeof(aim_userinfo_t));

	newevil = aimbs_get16(bs);

	if (aim_bstream_empty(bs)) {
		aim_extractuserinfo(sess, bs, &userinfo);
	}

	if ((userfunc = aim_callhandler(sess, rx->conn, snac->family, snac->subtype))) {
		return userfunc(sess, rx, newevil, &userinfo);
	}

	return 0;
}

/*
 * Service Migrate (group 1, type 0x12)
 *
 * This is the final SNAC sent on the original connection during a migration.
 * It contains the IP and cookie used to connect to the new server, and
 * optionally a list of the SNAC groups being migrated.
 *
 */
static int migrate(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{
	aim_rxcallback_t userfunc;
	int ret = 0;
	guint16 groupcount, i;
	aim_tlvlist_t *tl;
	char *ip = NULL;
	aim_tlv_t *cktlv;

	/*
	 * Apparently there's some fun stuff that can happen right here. The
	 * migration can actually be quite selective about what groups it
	 * moves to the new server.  When not all the groups for a connection
	 * are migrated, or they are all migrated but some groups are moved
	 * to a different server than others, it is called a bifurcated
	 * migration.
	 *
	 * Let's play dumb and not support that.
	 *
	 */
	groupcount = aimbs_get16(bs);
	for (i = 0; i < groupcount; i++) {
		aimbs_get16(bs);

		imcb_error(sess->aux_data, "bifurcated migration unsupported");
	}

	tl = aim_readtlvchain(bs);

	if (aim_gettlv(tl, 0x0005, 1)) {
		ip = aim_gettlv_str(tl, 0x0005, 1);
	}

	cktlv = aim_gettlv(tl, 0x0006, 1);

	if ((userfunc = aim_callhandler(sess, rx->conn, snac->family, snac->subtype))) {
		ret = userfunc(sess, rx, ip, cktlv ? cktlv->value : NULL);
	}

	aim_freetlvchain(&tl);
	g_free(ip);

	return ret;
}

/* Message of the Day (group 1, type 0x13) */
static int motd(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{
	aim_rxcallback_t userfunc;
	char *msg = NULL;
	int ret = 0;
	aim_tlvlist_t *tlvlist;
	guint16 id;

	/*
	 * Code.
	 *
	 * Valid values:
	 *   1 Mandatory upgrade
	 *   2 Advisory upgrade
	 *   3 System bulletin
	 *   4 Nothing's wrong ("top o the world" -- normal)
	 *   5 Lets-break-something.
	 *
	 */
	id = aimbs_get16(bs);

	/*
	 * TLVs follow
	 */
	tlvlist = aim_readtlvchain(bs);

	msg = aim_gettlv_str(tlvlist, 0x000b, 1);

	if ((userfunc = aim_callhandler(sess, rx->conn, snac->family, snac->subtype))) {
		ret = userfunc(sess, rx, id, msg);
	}

	g_free(msg);

	aim_freetlvchain(&tlvlist);

	return ret;
}

/*
 * Set privacy flags (group 1, type 0x14)
 *
 * Normally 0x03.
 *
 *  Bit 1:  Allows other AIM users to see how long you've been idle.
 *  Bit 2:  Allows other AIM users to see how long you've been a member.
 *
 */
int aim_bos_setprivacyflags(aim_session_t *sess, aim_conn_t *conn, guint32 flags)
{
	return aim_genericreq_l(sess, conn, 0x0001, 0x0014, &flags);
}


/*
 * Set client versions (group 1, subtype 0x17)
 *
 * If you've seen the clientonline/clientready SNAC you're probably
 * wondering what the point of this one is.  And that point seems to be
 * that the versions in the client online SNAC are sent too late for the
 * server to be able to use them to change the protocol for the earlier
 * login packets (client versions are sent right after Host Online is
 * received, but client online versions aren't sent until quite a bit later).
 * We can see them already making use of this by changing the format of
 * the rate information based on what version of group 1 we advertise here.
 *
 */
int aim_setversions(aim_session_t *sess, aim_conn_t *conn)
{
	aim_conn_inside_t *ins = (aim_conn_inside_t *) conn->inside;
	struct snacgroup *sg;
	aim_frame_t *fr;
	aim_snacid_t snacid;

	if (!ins) {
		return -EINVAL;
	}

	if (!(fr = aim_tx_new(sess, conn, AIM_FRAMETYPE_FLAP, 0x02, 1152))) {
		return -ENOMEM;
	}

	snacid = aim_cachesnac(sess, 0x0001, 0x0017, 0x0000, NULL, 0);
	aim_putsnac(&fr->data, 0x0001, 0x0017, 0x0000, snacid);

	/*
	 * Send only the versions that the server cares about (that it
	 * marked as supporting in the server ready SNAC).
	 */
	for (sg = ins->groups; sg; sg = sg->next) {
		aim_module_t *mod;

		if ((mod = aim__findmodulebygroup(sess, sg->group))) {
			aimbs_put16(&fr->data, mod->family);
			aimbs_put16(&fr->data, mod->version);
		}
	}

	aim_tx_enqueue(sess, fr);

	return 0;
}

/* Host versions (group 1, subtype 0x18) */
static int hostversions(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{
	guint8 *versions;

	/* This is frivolous. (Thank you SmarterChild.) */
	aim_bstream_empty(bs); /* == vercount * 4 */
	versions = aimbs_getraw(bs, aim_bstream_empty(bs));
	g_free(versions);

	/*
	 * Now request rates.
	 */
	aim_reqrates(sess, rx->conn);

	return 1;
}

/*
 * Subtype 0x001e - Extended Status
 *
 * Sets your ICQ status (available, away, do not disturb, etc.)
 *
 * These are the same TLVs seen in user info.  You can
 * also set 0x0008 and 0x000c.
 */
int aim_setextstatus(aim_session_t *sess, aim_conn_t *conn, guint32 status)
{
	aim_frame_t *fr;
	aim_snacid_t snacid;
	aim_tlvlist_t *tl = NULL;
	guint32 data;
	struct im_connection *ic = sess ? sess->aux_data : NULL;

	data = AIM_ICQ_STATE_HIDEIP | status; /* yay for error checking ;^) */

	if (ic && set_getbool(&ic->acc->set, "web_aware")) {
		data |= AIM_ICQ_STATE_WEBAWARE;
	}

	aim_addtlvtochain32(&tl, 0x0006, data); /* tlvlen */

	if (!(fr = aim_tx_new(sess, conn, AIM_FRAMETYPE_FLAP, 0x02, 10 + 8))) {
		return -ENOMEM;
	}

	snacid = aim_cachesnac(sess, 0x0001, 0x001e, 0x0000, NULL, 0);
	aim_putsnac(&fr->data, 0x0001, 0x001e, 0x0000, snacid);

	aim_writetlvchain(&fr->data, &tl);
	aim_freetlvchain(&tl);

	aim_tx_enqueue(sess, fr);

	return 0;
}

/*
 * Starting this past week (26 Mar 2001, say), AOL has started sending
 * this nice little extra SNAC.  AFAIK, it has never been used until now.
 *
 * The request contains eight bytes.  The first four are an offset, the
 * second four are a length.
 *
 * The offset is an offset into aim.exe when it is mapped during execution
 * on Win32.  So far, AOL has only been requesting bytes in static regions
 * of memory.  (I won't put it past them to start requesting data in
 * less static regions -- regions that are initialized at run time, but still
 * before the client receives this request.)
 *
 * When the client receives the request, it adds it to the current ds
 * (0x00400000) and dereferences it, copying the data into a buffer which
 * it then runs directly through the MD5 hasher.  The 16 byte output of
 * the hash is then sent back to the server.
 *
 * If the client does not send any data back, or the data does not match
 * the data that the specific client should have, the client will get the
 * following message from "AOL Instant Messenger":
 *    "You have been disconnected from the AOL Instant Message Service (SM)
 *     for accessing the AOL network using unauthorized software.  You can
 *     download a FREE, fully featured, and authorized client, here
 *     http://www.aol.com/aim/download2.html"
 * The connection is then closed, receiving disconnect code 1, URL
 * http://www.aim.aol.com/errors/USER_LOGGED_OFF_NEW_LOGIN.html.
 *
 * Note, however, that numerous inconsistencies can cause the above error,
 * not just sending back a bad hash.  Do not immediately suspect this code
 * if you get disconnected.  AOL and the open/free software community have
 * played this game for a couple years now, generating the above message
 * on numerous occasions.
 *
 * Anyway, neener.  We win again.
 *
 */
/* Client verification (group 1, subtype 0x1f) */
static int memrequest(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{
	aim_rxcallback_t userfunc;
	guint32 offset, len;
	aim_tlvlist_t *list;
	char *modname;

	offset = aimbs_get32(bs);
	len = aimbs_get32(bs);
	list = aim_readtlvchain(bs);

	modname = aim_gettlv_str(list, 0x0001, 1);

	if ((userfunc = aim_callhandler(sess, rx->conn, snac->family, snac->subtype))) {
		return userfunc(sess, rx, offset, len, modname);
	}

	g_free(modname);
	aim_freetlvchain(&list);

	return 0;
}

static int snachandler(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{

	if (snac->subtype == 0x0003) {
		return hostonline(sess, mod, rx, snac, bs);
	} else if (snac->subtype == 0x0005) {
		return redirect(sess, mod, rx, snac, bs);
	} else if (snac->subtype == 0x0007) {
		return rateresp(sess, mod, rx, snac, bs);
	} else if (snac->subtype == 0x000a) {
		return ratechange(sess, mod, rx, snac, bs);
	} else if (snac->subtype == 0x000b) {
		return serverpause(sess, mod, rx, snac, bs);
	} else if (snac->subtype == 0x000d) {
		return serverresume(sess, mod, rx, snac, bs);
	} else if (snac->subtype == 0x000f) {
		return selfinfo(sess, mod, rx, snac, bs);
	} else if (snac->subtype == 0x0010) {
		return evilnotify(sess, mod, rx, snac, bs);
	} else if (snac->subtype == 0x0012) {
		return migrate(sess, mod, rx, snac, bs);
	} else if (snac->subtype == 0x0013) {
		return motd(sess, mod, rx, snac, bs);
	} else if (snac->subtype == 0x0018) {
		return hostversions(sess, mod, rx, snac, bs);
	} else if (snac->subtype == 0x001f) {
		return memrequest(sess, mod, rx, snac, bs);
	}

	return 0;
}

int general_modfirst(aim_session_t *sess, aim_module_t *mod)
{

	mod->family = 0x0001;
	mod->version = 0x0003;
	mod->toolid = 0x0110;
	mod->toolversion = 0x0629;
	mod->flags = 0;
	strncpy(mod->name, "general", sizeof(mod->name));
	mod->snachandler = snachandler;

	return 0;
}

