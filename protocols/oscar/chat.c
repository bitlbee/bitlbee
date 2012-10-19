/*
 * aim_chat.c
 *
 * Routines for the Chat service.
 *
 */

#include <aim.h> 
#include <glib.h>
#include "info.h"

/* Stored in the ->priv of chat connections */
struct chatconnpriv {
	guint16 exchange;
	char *name;
	guint16 instance;
};

void aim_conn_kill_chat(aim_session_t *sess, aim_conn_t *conn)
{
	struct chatconnpriv *ccp = (struct chatconnpriv *)conn->priv;

	if (ccp)
		g_free(ccp->name);
	g_free(ccp);

	return;
}

/*
 * Send a Chat Message.
 *
 * Possible flags:
 *   AIM_CHATFLAGS_NOREFLECT   --  Unset the flag that requests messages
 *                                 should be sent to their sender.
 *   AIM_CHATFLAGS_AWAY        --  Mark the message as an autoresponse
 *                                 (Note that WinAIM does not honor this,
 *                                 and displays the message as normal.)
 *
 * XXX convert this to use tlvchains 
 */
int aim_chat_send_im(aim_session_t *sess, aim_conn_t *conn, guint16 flags, const char *msg, int msglen)
{   
	int i;
	aim_frame_t *fr;
	aim_msgcookie_t *cookie;
	aim_snacid_t snacid;
	guint8 ckstr[8];
	aim_tlvlist_t *otl = NULL, *itl = NULL;

	if (!sess || !conn || !msg || (msglen <= 0))
		return 0;

	if (!(fr = aim_tx_new(sess, conn, AIM_FRAMETYPE_FLAP, 0x02, 1152)))
		return -ENOMEM;

	snacid = aim_cachesnac(sess, 0x000e, 0x0005, 0x0000, NULL, 0);
	aim_putsnac(&fr->data, 0x000e, 0x0005, 0x0000, snacid);


	/* 
	 * Generate a random message cookie.
	 *
	 * XXX mkcookie should generate the cookie and cache it in one
	 * operation to preserve uniqueness.
	 *
	 */
	for (i = 0; i < sizeof(ckstr); i++)
		aimutil_put8(ckstr+i, (guint8) rand());

	cookie = aim_mkcookie(ckstr, AIM_COOKIETYPE_CHAT, NULL);
	cookie->data = NULL; /* XXX store something useful here */

	aim_cachecookie(sess, cookie);

	for (i = 0; i < sizeof(ckstr); i++)
		aimbs_put8(&fr->data, ckstr[i]);


	/*
	 * Channel ID. 
	 */
	aimbs_put16(&fr->data, 0x0003);


	/*
	 * Type 1: Flag meaning this message is destined to the room.
	 */
	aim_addtlvtochain_noval(&otl, 0x0001);

	/*
	 * Type 6: Reflect
	 */
	if (!(flags & AIM_CHATFLAGS_NOREFLECT))
		aim_addtlvtochain_noval(&otl, 0x0006);

	/*
	 * Type 7: Autoresponse
	 */
	if (flags & AIM_CHATFLAGS_AWAY)
		aim_addtlvtochain_noval(&otl, 0x0007);
	
	/* [WvG] This wasn't there originally, but we really should send
	         the right charset flags, as we also do with normal
	         messages. Hope this will work. :-) */
	/*
	if (flags & AIM_CHATFLAGS_UNICODE)
		aimbs_put16(&fr->data, 0x0002);
	else if (flags & AIM_CHATFLAGS_ISO_8859_1)
		aimbs_put16(&fr->data, 0x0003);
	else
		aimbs_put16(&fr->data, 0x0000);
	
	aimbs_put16(&fr->data, 0x0000);
	*/
	
	/*
	 * SubTLV: Type 1: Message
	 */
	aim_addtlvtochain_raw(&itl, 0x0001, strlen(msg), (guint8 *)msg);

	/*
	 * Type 5: Message block.  Contains more TLVs.
	 *
	 * This could include other information... We just
	 * put in a message TLV however.  
	 * 
	 */
	aim_addtlvtochain_frozentlvlist(&otl, 0x0005, &itl);

	aim_writetlvchain(&fr->data, &otl);
	
	aim_freetlvchain(&itl);
	aim_freetlvchain(&otl);
	
	aim_tx_enqueue(sess, fr);

	return 0;
}

/*
 * Join a room of name roomname.  This is the first step to joining an 
 * already created room.  It's basically a Service Request for 
 * family 0x000e, with a little added on to specify the exchange and room 
 * name.
 */
int aim_chat_join(aim_session_t *sess, aim_conn_t *conn, guint16 exchange, const char *roomname, guint16 instance)
{
	aim_frame_t *fr;
	aim_snacid_t snacid;
	aim_tlvlist_t *tl = NULL;
	struct chatsnacinfo csi;
	
	if (!sess || !conn || !roomname || !strlen(roomname))
		return -EINVAL;

	if (!(fr = aim_tx_new(sess, conn, AIM_FRAMETYPE_FLAP, 0x02, 512)))
		return -ENOMEM;

	memset(&csi, 0, sizeof(csi));
	csi.exchange = exchange;
	strncpy(csi.name, roomname, sizeof(csi.name));
	csi.instance = instance;

	snacid = aim_cachesnac(sess, 0x0001, 0x0004, 0x0000, &csi, sizeof(csi));
	aim_putsnac(&fr->data, 0x0001, 0x0004, 0x0000, snacid);

	/*
	 * Requesting service chat (0x000e)
	 */
	aimbs_put16(&fr->data, 0x000e);

	aim_addtlvtochain_chatroom(&tl, 0x0001, exchange, roomname, instance);
	aim_writetlvchain(&fr->data, &tl);
	aim_freetlvchain(&tl);

	aim_tx_enqueue(sess, fr);

	return 0; 
}

int aim_chat_readroominfo(aim_bstream_t *bs, struct aim_chat_roominfo *outinfo)
{
	int namelen;

	if (!bs || !outinfo)
		return 0;

	outinfo->exchange = aimbs_get16(bs);
	namelen = aimbs_get8(bs);
	outinfo->name = aimbs_getstr(bs, namelen);
	outinfo->instance = aimbs_get16(bs);

	return 0;
}

/*
 * conn must be a BOS connection!
 */
int aim_chat_invite(aim_session_t *sess, aim_conn_t *conn, const char *sn, const char *msg, guint16 exchange, const char *roomname, guint16 instance)
{
	int i;
	aim_frame_t *fr;
	aim_msgcookie_t *cookie;
	struct aim_invite_priv *priv;
	guint8 ckstr[8];
	aim_snacid_t snacid;
	aim_tlvlist_t *otl = NULL, *itl = NULL;
	guint8 *hdr;
	int hdrlen;
	aim_bstream_t hdrbs;
	
	if (!sess || !conn || !sn || !msg || !roomname)
		return -EINVAL;

	if (conn->type != AIM_CONN_TYPE_BOS)
		return -EINVAL;

	if (!(fr = aim_tx_new(sess, conn, AIM_FRAMETYPE_FLAP, 0x02, 1152+strlen(sn)+strlen(roomname)+strlen(msg))))
		return -ENOMEM;

	snacid = aim_cachesnac(sess, 0x0004, 0x0006, 0x0000, sn, strlen(sn)+1);
	aim_putsnac(&fr->data, 0x0004, 0x0006, 0x0000, snacid);


	/*
	 * Cookie
	 */
	for (i = 0; i < sizeof(ckstr); i++)
		aimutil_put8(ckstr, (guint8) rand());

	/* XXX should be uncached by an unwritten 'invite accept' handler */
	if ((priv = g_malloc(sizeof(struct aim_invite_priv)))) {
		priv->sn = g_strdup(sn);
		priv->roomname = g_strdup(roomname);
		priv->exchange = exchange;
		priv->instance = instance;
	}

	if ((cookie = aim_mkcookie(ckstr, AIM_COOKIETYPE_INVITE, priv)))
		aim_cachecookie(sess, cookie);
	else
		g_free(priv);

	for (i = 0; i < sizeof(ckstr); i++)
		aimbs_put8(&fr->data, ckstr[i]);


	/*
	 * Channel (2)
	 */
	aimbs_put16(&fr->data, 0x0002);

	/*
	 * Dest sn
	 */
	aimbs_put8(&fr->data, strlen(sn));
	aimbs_putraw(&fr->data, (guint8 *)sn, strlen(sn));

	/*
	 * TLV t(0005)
	 *
	 * Everything else is inside this TLV.
	 *
	 * Sigh.  AOL was rather inconsistent right here.  So we have
	 * to play some minor tricks.  Right inside the type 5 is some
	 * raw data, followed by a series of TLVs.  
	 *
	 */
	hdrlen = 2+8+16+6+4+4+strlen(msg)+4+2+1+strlen(roomname)+2;
	hdr = g_malloc(hdrlen);
	aim_bstream_init(&hdrbs, hdr, hdrlen);
	
	aimbs_put16(&hdrbs, 0x0000); /* Unknown! */
	aimbs_putraw(&hdrbs, ckstr, sizeof(ckstr)); /* I think... */
	aim_putcap(&hdrbs, AIM_CAPS_CHAT);

	aim_addtlvtochain16(&itl, 0x000a, 0x0001);
	aim_addtlvtochain_noval(&itl, 0x000f);
	aim_addtlvtochain_raw(&itl, 0x000c, strlen(msg), (guint8 *)msg);
	aim_addtlvtochain_chatroom(&itl, 0x2711, exchange, roomname, instance);
	aim_writetlvchain(&hdrbs, &itl);
	
	aim_addtlvtochain_raw(&otl, 0x0005, aim_bstream_curpos(&hdrbs), hdr);

	aim_writetlvchain(&fr->data, &otl);

	g_free(hdr);
	aim_freetlvchain(&itl);
	aim_freetlvchain(&otl);
	
	aim_tx_enqueue(sess, fr);

	return 0;
}

/*
 * General room information.  Lots of stuff.
 *
 * Values I know are in here but I havent attached
 * them to any of the 'Unknown's:
 *	- Language (English)
 *
 * SNAC 000e/0002
 */
static int infoupdate(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{
	aim_userinfo_t *userinfo = NULL;
	aim_rxcallback_t userfunc;
	int ret = 0;
	int usercount = 0;
	guint8 detaillevel = 0;
	char *roomname = NULL;
	struct aim_chat_roominfo roominfo;
	aim_tlvlist_t *tlvlist;
	char *roomdesc = NULL;
	guint16 flags = 0;
	guint32 creationtime = 0;
	guint16 maxmsglen = 0, maxvisiblemsglen = 0;
	guint16 unknown_d2 = 0, unknown_d5 = 0;

	aim_chat_readroominfo(bs, &roominfo);

	detaillevel = aimbs_get8(bs);

	if (detaillevel != 0x02) {
		imcb_error(sess->aux_data, "Only detaillevel 0x2 is support at the moment");
		return 1;
	}

	aimbs_get16(bs); /* tlv count */

	/*
	 * Everything else are TLVs.
	 */ 
	tlvlist = aim_readtlvchain(bs);

	/*
	 * TLV type 0x006a is the room name in Human Readable Form.
	 */
	if (aim_gettlv(tlvlist, 0x006a, 1))
		roomname = aim_gettlv_str(tlvlist, 0x006a, 1);

	/*
	 * Type 0x006f: Number of occupants.
	 */
	if (aim_gettlv(tlvlist, 0x006f, 1))
		usercount = aim_gettlv16(tlvlist, 0x006f, 1);

	/*
	 * Type 0x0073:  Occupant list.
	 */
	if (aim_gettlv(tlvlist, 0x0073, 1)) {	
		int curoccupant = 0;
		aim_tlv_t *tmptlv;
		aim_bstream_t occbs;

		tmptlv = aim_gettlv(tlvlist, 0x0073, 1);

		/* Allocate enough userinfo structs for all occupants */
		userinfo = g_new0(aim_userinfo_t, usercount);

		aim_bstream_init(&occbs, tmptlv->value, tmptlv->length);

		while (curoccupant < usercount)
			aim_extractuserinfo(sess, &occbs, &userinfo[curoccupant++]);
	}

	/* 
	 * Type 0x00c9: Flags. (AIM_CHATROOM_FLAG)
	 */
	if (aim_gettlv(tlvlist, 0x00c9, 1))
		flags = aim_gettlv16(tlvlist, 0x00c9, 1);

	/* 
	 * Type 0x00ca: Creation time (4 bytes)
	 */
	if (aim_gettlv(tlvlist, 0x00ca, 1))
		creationtime = aim_gettlv32(tlvlist, 0x00ca, 1);

	/* 
	 * Type 0x00d1: Maximum Message Length
	 */
	if (aim_gettlv(tlvlist, 0x00d1, 1))
		maxmsglen = aim_gettlv16(tlvlist, 0x00d1, 1);

	/* 
	 * Type 0x00d2: Unknown. (2 bytes)
	 */
	if (aim_gettlv(tlvlist, 0x00d2, 1))
		unknown_d2 = aim_gettlv16(tlvlist, 0x00d2, 1);

	/* 
	 * Type 0x00d3: Room Description
	 */
	if (aim_gettlv(tlvlist, 0x00d3, 1))
		roomdesc = aim_gettlv_str(tlvlist, 0x00d3, 1);

	/*
	 * Type 0x000d4: Unknown (flag only)
	 */
	if (aim_gettlv(tlvlist, 0x000d4, 1))
		;

	/* 
	 * Type 0x00d5: Unknown. (1 byte)
	 */
	if (aim_gettlv(tlvlist, 0x00d5, 1))
		unknown_d5 = aim_gettlv8(tlvlist, 0x00d5, 1);


	/*
	 * Type 0x00d6: Encoding 1 ("us-ascii")
	 */
	if (aim_gettlv(tlvlist, 0x000d6, 1))
		;
	
	/*
	 * Type 0x00d7: Language 1 ("en")
	 */
	if (aim_gettlv(tlvlist, 0x000d7, 1))
		;

	/*
	 * Type 0x00d8: Encoding 2 ("us-ascii")
	 */
	if (aim_gettlv(tlvlist, 0x000d8, 1))
		;
	
	/*
	 * Type 0x00d9: Language 2 ("en")
	 */
	if (aim_gettlv(tlvlist, 0x000d9, 1))
		;

	/*
	 * Type 0x00da: Maximum visible message length
	 */
	if (aim_gettlv(tlvlist, 0x000da, 1))
		maxvisiblemsglen = aim_gettlv16(tlvlist, 0x00da, 1);

	if ((userfunc = aim_callhandler(sess, rx->conn, snac->family, snac->subtype))) {
		ret = userfunc(sess,
				rx, 
				&roominfo,
				roomname,
				usercount,
				userinfo,	
				roomdesc,
				flags,
				creationtime,
				maxmsglen,
				unknown_d2,
				unknown_d5,
				maxvisiblemsglen);
	}

	g_free(roominfo.name);
	g_free(userinfo);
	g_free(roomname);
	g_free(roomdesc);
	aim_freetlvchain(&tlvlist);

	return ret;
}

static int userlistchange(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{
	aim_userinfo_t *userinfo = NULL;
	aim_rxcallback_t userfunc;
	int curcount = 0, ret = 0;

	while (aim_bstream_empty(bs)) {
		curcount++;
		userinfo = g_realloc(userinfo, curcount * sizeof(aim_userinfo_t));
		aim_extractuserinfo(sess, bs, &userinfo[curcount-1]);
	}

	if ((userfunc = aim_callhandler(sess, rx->conn, snac->family, snac->subtype)))
		ret = userfunc(sess, rx, curcount, userinfo);

	g_free(userinfo);

	return ret;
}

/*
 * We could probably include this in the normal ICBM parsing 
 * code as channel 0x0003, however, since only the start
 * would be the same, we might as well do it here.
 *
 * General outline of this SNAC:
 *   snac
 *   cookie
 *   channel id
 *   tlvlist
 *     unknown
 *     source user info
 *       name
 *       evility
 *       userinfo tlvs
 *         online time
 *         etc
 *     message metatlv
 *       message tlv
 *         message string
 *       possibly others
 *  
 */
static int incomingmsg(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{
	aim_userinfo_t userinfo;
	aim_rxcallback_t userfunc;	
	int ret = 0;
	guint8 *cookie;
	guint16 channel;
	aim_tlvlist_t *otl;
	char *msg = NULL;
	aim_msgcookie_t *ck;

	memset(&userinfo, 0, sizeof(aim_userinfo_t));

	/*
	 * ICBM Cookie.  Uncache it.
	 */
	cookie = aimbs_getraw(bs, 8);

	if ((ck = aim_uncachecookie(sess, cookie, AIM_COOKIETYPE_CHAT))) {
		g_free(ck->data);
		g_free(ck);
	}

	/*
	 * Channel ID
	 *
	 * Channels 1 and 2 are implemented in the normal ICBM
	 * parser.
	 *
	 * We only do channel 3 here.
	 *
	 */
	channel = aimbs_get16(bs);

	if (channel != 0x0003) {
		imcb_error(sess->aux_data, "unknown channel!");
		return 0;
	}

	/*
	 * Start parsing TLVs right away. 
	 */
	otl = aim_readtlvchain(bs);

	/*
	 * Type 0x0003: Source User Information
	 */
	if (aim_gettlv(otl, 0x0003, 1)) {
		aim_tlv_t *userinfotlv;
		aim_bstream_t tbs;

		userinfotlv = aim_gettlv(otl, 0x0003, 1);

		aim_bstream_init(&tbs, userinfotlv->value, userinfotlv->length);
		aim_extractuserinfo(sess, &tbs, &userinfo);
	}

	/*
	 * Type 0x0001: If present, it means it was a message to the 
	 * room (as opposed to a whisper).
	 */
	if (aim_gettlv(otl, 0x0001, 1))
		;

	/*
	 * Type 0x0005: Message Block.  Conains more TLVs.
	 */
	if (aim_gettlv(otl, 0x0005, 1)) {
		aim_tlvlist_t *itl;
		aim_tlv_t *msgblock;
		aim_bstream_t tbs;

		msgblock = aim_gettlv(otl, 0x0005, 1);
		aim_bstream_init(&tbs, msgblock->value, msgblock->length);
		itl = aim_readtlvchain(&tbs);

		/* 
		 * Type 0x0001: Message.
		 */	
		if (aim_gettlv(itl, 0x0001, 1))
			msg = aim_gettlv_str(itl, 0x0001, 1);

		aim_freetlvchain(&itl); 
	}

	if ((userfunc = aim_callhandler(sess, rx->conn, snac->family, snac->subtype)))
		ret = userfunc(sess, rx, &userinfo, msg);

	g_free(cookie);
	g_free(msg);
	aim_freetlvchain(&otl);

	return ret;
}

static int snachandler(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{

	if (snac->subtype == 0x0002)
		return infoupdate(sess, mod, rx, snac, bs);
	else if ((snac->subtype == 0x0003) || (snac->subtype == 0x0004))
		return userlistchange(sess, mod, rx, snac, bs);
	else if (snac->subtype == 0x0006)
		return incomingmsg(sess, mod, rx, snac, bs);

	return 0;
}

int chat_modfirst(aim_session_t *sess, aim_module_t *mod)
{

	mod->family = 0x000e;
	mod->version = 0x0001;
	mod->toolid = 0x0010;
	mod->toolversion = 0x0629;
	mod->flags = 0;
	strncpy(mod->name, "chat", sizeof(mod->name));
	mod->snachandler = snachandler;

	return 0;
}
