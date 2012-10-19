/*
 * aim_info.c
 *
 * The functions here are responsible for requesting and parsing information-
 * gathering SNACs.  Or something like that. 
 *
 */

#include <aim.h>
#include "info.h"

struct aim_priv_inforeq {
	char sn[MAXSNLEN+1];
	guint16 infotype;
};

int aim_getinfo(aim_session_t *sess, aim_conn_t *conn, const char *sn, guint16 infotype)
{
	struct aim_priv_inforeq privdata;
	aim_frame_t *fr;
	aim_snacid_t snacid;

	if (!sess || !conn || !sn)
		return -EINVAL;

	if (!(fr = aim_tx_new(sess, conn, AIM_FRAMETYPE_FLAP, 0x02, 12+1+strlen(sn))))
		return -ENOMEM;

	strncpy(privdata.sn, sn, sizeof(privdata.sn));
	privdata.infotype = infotype;
	snacid = aim_cachesnac(sess, 0x0002, 0x0005, 0x0000, &privdata, sizeof(struct aim_priv_inforeq));
	
	aim_putsnac(&fr->data, 0x0002, 0x0005, 0x0000, snacid);
	aimbs_put16(&fr->data, infotype);
	aimbs_put8(&fr->data, strlen(sn));
	aimbs_putraw(&fr->data, (guint8 *)sn, strlen(sn));

	aim_tx_enqueue(sess, fr);

	return 0;
}

/*
 * Capability blocks. 
 *
 * These are CLSIDs. They should actually be of the form:
 *
 * {0x0946134b, 0x4c7f, 0x11d1,
 *  {0x82, 0x22, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}}},
 *
 * But, eh.
 */
static const struct {
	guint32 flag;
	guint8 data[16];
} aim_caps[] = {

	/*
	 * Chat is oddball.
	 */
	{AIM_CAPS_CHAT,
	 {0x74, 0x8f, 0x24, 0x20, 0x62, 0x87, 0x11, 0xd1, 
	  0x82, 0x22, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}},

	/*
	 * These are mostly in order.
	 */
	{AIM_CAPS_VOICE,
	 {0x09, 0x46, 0x13, 0x41, 0x4c, 0x7f, 0x11, 0xd1, 
	  0x82, 0x22, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}},

	{AIM_CAPS_SENDFILE,
	 {0x09, 0x46, 0x13, 0x43, 0x4c, 0x7f, 0x11, 0xd1, 
	  0x82, 0x22, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}},

	/*
	 * Advertised by the EveryBuddy client.
	 */
	{AIM_CAPS_ICQ,
	 {0x09, 0x46, 0x13, 0x44, 0x4c, 0x7f, 0x11, 0xd1, 
	  0x82, 0x22, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}},

	{AIM_CAPS_IMIMAGE,
	 {0x09, 0x46, 0x13, 0x45, 0x4c, 0x7f, 0x11, 0xd1, 
	  0x82, 0x22, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}},

	{AIM_CAPS_BUDDYICON,
	 {0x09, 0x46, 0x13, 0x46, 0x4c, 0x7f, 0x11, 0xd1, 
	  0x82, 0x22, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}},

	{AIM_CAPS_SAVESTOCKS,
	 {0x09, 0x46, 0x13, 0x47, 0x4c, 0x7f, 0x11, 0xd1,
	  0x82, 0x22, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}},

	{AIM_CAPS_GETFILE,
	 {0x09, 0x46, 0x13, 0x48, 0x4c, 0x7f, 0x11, 0xd1,
	  0x82, 0x22, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}},

    /*
     * Client supports channel 2 extended, TLV(0x2711) based messages.
     * Currently used only by ICQ clients. ICQ clients and clones use this GUID
     * as message format sign. Trillian client use another GUID in channel 2
     * messages to implement its own message format (trillian doesn't use
     * TLV(x2711) in SecureIM channel 2 messages!).
     */
	{AIM_CAPS_ICQSERVERRELAY,
	 {0x09, 0x46, 0x13, 0x49, 0x4c, 0x7f, 0x11, 0xd1,
	  0x82, 0x22, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}},

	/*
	 * Indeed, there are two of these.  The former appears to be correct, 
	 * but in some versions of winaim, the second one is set.  Either they 
	 * forgot to fix endianness, or they made a typo. It really doesn't 
	 * matter which.
	 */
	{AIM_CAPS_GAMES,
	 {0x09, 0x46, 0x13, 0x4a, 0x4c, 0x7f, 0x11, 0xd1,
	  0x82, 0x22, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}},
	{AIM_CAPS_GAMES2,
	 {0x09, 0x46, 0x13, 0x4a, 0x4c, 0x7f, 0x11, 0xd1,
	  0x22, 0x82, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}},

	{AIM_CAPS_SENDBUDDYLIST,
	 {0x09, 0x46, 0x13, 0x4b, 0x4c, 0x7f, 0x11, 0xd1,
	  0x82, 0x22, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}},

	{AIM_CAPS_UTF8,
	 {0x09, 0x46, 0x13, 0x4E, 0x4C, 0x7F, 0x11, 0xD1,
	  0x82, 0x22, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}},

	{AIM_CAPS_ICQRTF,
	 {0x97, 0xb1, 0x27, 0x51, 0x24, 0x3c, 0x43, 0x34, 
	  0xad, 0x22, 0xd6, 0xab, 0xf7, 0x3f, 0x14, 0x92}},

	{AIM_CAPS_ICQUNKNOWN,
	 {0x2e, 0x7a, 0x64, 0x75, 0xfa, 0xdf, 0x4d, 0xc8,
	  0x88, 0x6f, 0xea, 0x35, 0x95, 0xfd, 0xb6, 0xdf}},

	{AIM_CAPS_EMPTY,
	 {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},

	{AIM_CAPS_TRILLIANCRYPT,
	 {0xf2, 0xe7, 0xc7, 0xf4, 0xfe, 0xad, 0x4d, 0xfb,
	  0xb2, 0x35, 0x36, 0x79, 0x8b, 0xdf, 0x00, 0x00}},

	{AIM_CAPS_APINFO, 
     {0xAA, 0x4A, 0x32, 0xB5, 0xF8, 0x84, 0x48, 0xc6,
      0xA3, 0xD7, 0x8C, 0x50, 0x97, 0x19, 0xFD, 0x5B}},

	{AIM_CAPS_INTEROP,
	 {0x09, 0x46, 0x13, 0x4d, 0x4c, 0x7f, 0x11, 0xd1,
	  0x82, 0x22, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}},

	{AIM_CAPS_ICHAT,
	 {0x09, 0x46, 0x00, 0x00, 0x4c, 0x7f, 0x11, 0xd1, 
	  0x82, 0x22, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}},

	{AIM_CAPS_LAST}
};

/*
 * This still takes a length parameter even with a bstream because capabilities
 * are not naturally bounded.
 * 
 */
guint32 aim_getcap(aim_session_t *sess, aim_bstream_t *bs, int len)
{
	guint32 flags = 0;
	int offset;

	for (offset = 0; aim_bstream_empty(bs) && (offset < len); offset += 0x10) {
		guint8 *cap;
		int i, identified;

		cap = aimbs_getraw(bs, 0x10);

		for (i = 0, identified = 0; !(aim_caps[i].flag & AIM_CAPS_LAST); i++) {

			if (memcmp(&aim_caps[i].data, cap, 0x10) == 0) {
				flags |= aim_caps[i].flag;
				identified++;
				break; /* should only match once... */

			}
		}

		if (!identified) {
			/*FIXME*/
			/*REMOVEME :-)
			g_strdup_printf("unknown capability: {%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x}\n",
					cap[0], cap[1], cap[2], cap[3],
					cap[4], cap[5],
					cap[6], cap[7],
					cap[8], cap[9],
					cap[10], cap[11], cap[12], cap[13],
					cap[14], cap[15]);
			*/
		}

		g_free(cap);
	}

	return flags;
}

int aim_putcap(aim_bstream_t *bs, guint32 caps)
{
	int i;

	if (!bs)
		return -EINVAL;

	for (i = 0; aim_bstream_empty(bs); i++) {

		if (aim_caps[i].flag == AIM_CAPS_LAST)
			break;

		if (caps & aim_caps[i].flag)
			aimbs_putraw(bs, aim_caps[i].data, 0x10);

	}

	return 0;
}

/*
 * AIM is fairly regular about providing user info.  This is a generic 
 * routine to extract it in its standard form.
 */
int aim_extractuserinfo(aim_session_t *sess, aim_bstream_t *bs, aim_userinfo_t *outinfo)
{
	int curtlv, tlvcnt;
	guint8 snlen;

	if (!bs || !outinfo)
		return -EINVAL;

	/* Clear out old data first */
	memset(outinfo, 0x00, sizeof(aim_userinfo_t));

	/*
	 * Screen name.  Stored as an unterminated string prepended with a 
	 * byte containing its length.
	 */
	snlen = aimbs_get8(bs);
	aimbs_getrawbuf(bs, (guint8 *)outinfo->sn, snlen);

	/*
	 * Warning Level.  Stored as an unsigned short.
	 */
	outinfo->warnlevel = aimbs_get16(bs);

	/*
	 * TLV Count. Unsigned short representing the number of 
	 * Type-Length-Value triples that follow.
	 */
	tlvcnt = aimbs_get16(bs);

	/* 
	 * Parse out the Type-Length-Value triples as they're found.
	 */
	for (curtlv = 0; curtlv < tlvcnt; curtlv++) {
		int endpos;
		guint16 type, length;

		type = aimbs_get16(bs);
		length = aimbs_get16(bs);

		endpos = aim_bstream_curpos(bs) + length;

		if (type == 0x0001) {
			/*
			 * Type = 0x0001: User flags
			 * 
			 * Specified as any of the following ORed together:
			 *      0x0001  Trial (user less than 60days)
			 *      0x0002  Unknown bit 2
			 *      0x0004  AOL Main Service user
			 *      0x0008  Unknown bit 4
			 *      0x0010  Free (AIM) user 
			 *      0x0020  Away
			 *      0x0400  ActiveBuddy
			 *
			 */
			outinfo->flags = aimbs_get16(bs);
			outinfo->present |= AIM_USERINFO_PRESENT_FLAGS;

		} else if (type == 0x0002) {
			/*
			 * Type = 0x0002: Member-Since date. 
			 *
			 * The time/date that the user originally registered for
			 * the service, stored in time_t format.
			 */
			outinfo->membersince = aimbs_get32(bs);
			outinfo->present |= AIM_USERINFO_PRESENT_MEMBERSINCE;

		} else if (type == 0x0003) {
			/*
			 * Type = 0x0003: On-Since date.
			 *
			 * The time/date that the user started their current 
			 * session, stored in time_t format.
			 */
			outinfo->onlinesince = aimbs_get32(bs);
			outinfo->present |= AIM_USERINFO_PRESENT_ONLINESINCE;

		} else if (type == 0x0004) {
			/*
			 * Type = 0x0004: Idle time.
			 *
			 * Number of seconds since the user actively used the 
			 * service.
			 *
			 * Note that the client tells the server when to start
			 * counting idle times, so this may or may not be 
			 * related to reality.
			 */
			outinfo->idletime = aimbs_get16(bs);
			outinfo->present |= AIM_USERINFO_PRESENT_IDLE;

		} else if (type == 0x0006) {
			/*
			 * Type = 0x0006: ICQ Online Status
			 *
			 * ICQ's Away/DND/etc "enriched" status. Some decoding 
			 * of values done by Scott <darkagl@pcnet.com>
			 */
			aimbs_get16(bs);
			outinfo->icqinfo.status = aimbs_get16(bs);
			outinfo->present |= AIM_USERINFO_PRESENT_ICQEXTSTATUS;

		} else if (type == 0x000a) {
			/*
			 * Type = 0x000a
			 *
			 * ICQ User IP Address.
			 * Ahh, the joy of ICQ security.
			 */
			outinfo->icqinfo.ipaddr = aimbs_get32(bs);
			outinfo->present |= AIM_USERINFO_PRESENT_ICQIPADDR;

		} else if (type == 0x000c) {
			/* 
			 * Type = 0x000c
			 *
			 * random crap containing the IP address,
			 * apparently a port number, and some Other Stuff.
			 *
			 */
			aimbs_getrawbuf(bs, outinfo->icqinfo.crap, 0x25);
			outinfo->present |= AIM_USERINFO_PRESENT_ICQDATA;

		} else if (type == 0x000d) {
			/*
			 * Type = 0x000d
			 *
			 * Capability information.
			 *
			 */
			outinfo->capabilities = aim_getcap(sess, bs, length);
			outinfo->present |= AIM_USERINFO_PRESENT_CAPABILITIES;

		} else if (type == 0x000e) {
			/*
			 * Type = 0x000e
			 *
			 * Unknown.  Always of zero length, and always only
			 * on AOL users.
			 *
			 * Ignore.
			 *
			 */

		} else if ((type == 0x000f) || (type == 0x0010)) {
			/*
			 * Type = 0x000f: Session Length. (AIM)
			 * Type = 0x0010: Session Length. (AOL)
			 *
			 * The duration, in seconds, of the user's current 
			 * session.
			 *
			 * Which TLV type this comes in depends on the
			 * service the user is using (AIM or AOL).
			 *
			 */
			outinfo->sessionlen = aimbs_get32(bs);
			outinfo->present |= AIM_USERINFO_PRESENT_SESSIONLEN;

		} else {

			/*
			 * Reaching here indicates that either AOL has
			 * added yet another TLV for us to deal with, 
			 * or the parsing has gone Terribly Wrong.
			 *
			 * Either way, inform the owner and attempt
			 * recovery.
			 *
			 */
#ifdef DEBUG
			// imcb_error(sess->aux_data, G_STRLOC);
#endif

		}

		/* Save ourselves. */
		aim_bstream_setpos(bs, endpos);
	}

	return 0;
}

/*
 * Normally contains:
 *   t(0001)  - short containing max profile length (value = 1024)
 *   t(0002)  - short - unknown (value = 16) [max MIME type length?]
 *   t(0003)  - short - unknown (value = 10)
 *   t(0004)  - short - unknown (value = 2048) [ICQ only?]
 */
static int rights(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{
	aim_tlvlist_t *tlvlist;
	aim_rxcallback_t userfunc;
	int ret = 0;
	guint16 maxsiglen = 0;

	tlvlist = aim_readtlvchain(bs);

	if (aim_gettlv(tlvlist, 0x0001, 1))
		maxsiglen = aim_gettlv16(tlvlist, 0x0001, 1);

	if ((userfunc = aim_callhandler(sess, rx->conn, snac->family, snac->subtype)))
		ret = userfunc(sess, rx, maxsiglen);

	aim_freetlvchain(&tlvlist);

	return ret;
}

static int userinfo(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{
	aim_userinfo_t userinfo;
	char *text_encoding = NULL, *text = NULL;
	guint16 text_length = 0;
	aim_rxcallback_t userfunc;
	aim_tlvlist_t *tlvlist;
	aim_tlv_t *tlv;
	aim_snac_t *origsnac = NULL;
	struct aim_priv_inforeq *inforeq;
	int ret = 0;

	origsnac = aim_remsnac(sess, snac->id);

	if (!origsnac || !origsnac->data) {
		imcb_error(sess->aux_data, "major problem: no snac stored!");
		return 0;
	}

	inforeq = (struct aim_priv_inforeq *)origsnac->data;

	if ((inforeq->infotype != AIM_GETINFO_GENERALINFO) &&
			(inforeq->infotype != AIM_GETINFO_AWAYMESSAGE) &&
			(inforeq->infotype != AIM_GETINFO_CAPABILITIES)) {
		imcb_error(sess->aux_data, "unknown infotype in request!");
		return 0;
	}

	aim_extractuserinfo(sess, bs, &userinfo);

	tlvlist = aim_readtlvchain(bs);

	/* 
	 * Depending on what informational text was requested, different
	 * TLVs will appear here.
	 *
	 * Profile will be 1 and 2, away message will be 3 and 4, caps
	 * will be 5.
	 */
	if (inforeq->infotype == AIM_GETINFO_GENERALINFO) {
		text_encoding = aim_gettlv_str(tlvlist, 0x0001, 1);
		if((tlv = aim_gettlv(tlvlist, 0x0002, 1))) {
			text = g_new0(char, tlv->length);
			memcpy(text, tlv->value, tlv->length);
			text_length = tlv->length;
		}
	} else if (inforeq->infotype == AIM_GETINFO_AWAYMESSAGE) {
		text_encoding = aim_gettlv_str(tlvlist, 0x0003, 1);
		if((tlv = aim_gettlv(tlvlist, 0x0004, 1))) {
			text = g_new0(char, tlv->length);
			memcpy(text, tlv->value, tlv->length);
			text_length = tlv->length;
		}
	} else if (inforeq->infotype == AIM_GETINFO_CAPABILITIES) {
		aim_tlv_t *ct;

		if ((ct = aim_gettlv(tlvlist, 0x0005, 1))) {
			aim_bstream_t cbs;

			aim_bstream_init(&cbs, ct->value, ct->length);

			userinfo.capabilities = aim_getcap(sess, &cbs, ct->length);
			userinfo.present = AIM_USERINFO_PRESENT_CAPABILITIES;
		}
	}

	if ((userfunc = aim_callhandler(sess, rx->conn, snac->family, snac->subtype)))
		ret = userfunc(sess, rx, &userinfo, inforeq->infotype, text_encoding, text, text_length);

	g_free(text_encoding);
	g_free(text);

	aim_freetlvchain(&tlvlist);

	if (origsnac)
		g_free(origsnac->data);
	g_free(origsnac);

	return ret;
}

static int snachandler(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{

	if (snac->subtype == 0x0003)
		return rights(sess, mod, rx, snac, bs);
	else if (snac->subtype == 0x0006)
		return userinfo(sess, mod, rx, snac, bs);

	return 0;
}

int locate_modfirst(aim_session_t *sess, aim_module_t *mod)
{

	mod->family = 0x0002;
	mod->version = 0x0001;
	mod->toolid = 0x0110;
	mod->toolversion = 0x0629;
	mod->flags = 0;
	strncpy(mod->name, "locate", sizeof(mod->name));
	mod->snachandler = snachandler;

	return 0;
}
