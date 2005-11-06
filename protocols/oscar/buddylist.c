#include <aim.h>
#include "buddylist.h"

/*
 * Oncoming Buddy notifications contain a subset of the
 * user information structure.  Its close enough to run
 * through aim_extractuserinfo() however.
 *
 * Although the offgoing notification contains no information,
 * it is still in a format parsable by extractuserinfo.
 *
 */
static int buddychange(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{
	aim_userinfo_t userinfo;
	aim_rxcallback_t userfunc;

	aim_extractuserinfo(sess, bs, &userinfo);

	if ((userfunc = aim_callhandler(sess, rx->conn, snac->family, snac->subtype)))
		return userfunc(sess, rx, &userinfo);

	return 0;
}

static int rights(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{
	aim_rxcallback_t userfunc;
	aim_tlvlist_t *tlvlist;
	guint16 maxbuddies = 0, maxwatchers = 0;
	int ret = 0;

	/* 
	 * TLVs follow 
	 */
	tlvlist = aim_readtlvchain(bs);

	/*
	 * TLV type 0x0001: Maximum number of buddies.
	 */
	if (aim_gettlv(tlvlist, 0x0001, 1))
		maxbuddies = aim_gettlv16(tlvlist, 0x0001, 1);

	/*
	 * TLV type 0x0002: Maximum number of watchers.
	 *
	 * Watchers are other users who have you on their buddy
	 * list.  (This is called the "reverse list" by a certain
	 * other IM protocol.)
	 * 
	 */
	if (aim_gettlv(tlvlist, 0x0002, 1))
		maxwatchers = aim_gettlv16(tlvlist, 0x0002, 1);

	/*
	 * TLV type 0x0003: Unknown.
	 *
	 * ICQ only?
	 */

	if ((userfunc = aim_callhandler(sess, rx->conn, snac->family, snac->subtype)))
		ret = userfunc(sess, rx, maxbuddies, maxwatchers);

	aim_freetlvchain(&tlvlist);

	return ret;  
}

static int snachandler(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{

	if (snac->subtype == 0x0003)
		return rights(sess, mod, rx, snac, bs);
	else if ((snac->subtype == 0x000b) || (snac->subtype == 0x000c))
		return buddychange(sess, mod, rx, snac, bs);

	return 0;
}

int buddylist_modfirst(aim_session_t *sess, aim_module_t *mod)
{

	mod->family = 0x0003;
	mod->version = 0x0001;
	mod->toolid = 0x0110;
	mod->toolversion = 0x0629;
	mod->flags = 0;
	strncpy(mod->name, "buddylist", sizeof(mod->name));
	mod->snachandler = snachandler;

	return 0;
}

/*
 * aim_add_buddy()
 *
 * Adds a single buddy to your buddy list after login.
 *
 * XXX this should just be an extension of setbuddylist()
 *
 */
int aim_add_buddy(aim_session_t *sess, aim_conn_t *conn, const char *sn)
{
	aim_frame_t *fr;
	aim_snacid_t snacid;

	if (!sn || !strlen(sn))
		return -EINVAL;

	if (!(fr = aim_tx_new(sess, conn, AIM_FRAMETYPE_FLAP, 0x02, 10+1+strlen(sn))))
		return -ENOMEM;

	snacid = aim_cachesnac(sess, 0x0003, 0x0004, 0x0000, sn, strlen(sn)+1);
	aim_putsnac(&fr->data, 0x0003, 0x0004, 0x0000, snacid);

	aimbs_put8(&fr->data, strlen(sn));
	aimbs_putraw(&fr->data, (guint8 *)sn, strlen(sn));

	aim_tx_enqueue(sess, fr);

	return 0;
}

/*
 * XXX generalise to support removing multiple buddies (basically, its
 * the same as setbuddylist() but with a different snac subtype).
 *
 */
int aim_remove_buddy(aim_session_t *sess, aim_conn_t *conn, const char *sn)
{
	aim_frame_t *fr;
	aim_snacid_t snacid;

	if (!sn || !strlen(sn))
		return -EINVAL;

	if (!(fr = aim_tx_new(sess, conn, AIM_FRAMETYPE_FLAP, 0x02, 10+1+strlen(sn))))
		return -ENOMEM;

	snacid = aim_cachesnac(sess, 0x0003, 0x0005, 0x0000, sn, strlen(sn)+1);
	aim_putsnac(&fr->data, 0x0003, 0x0005, 0x0000, snacid);

	aimbs_put8(&fr->data, strlen(sn));
	aimbs_putraw(&fr->data, (guint8 *)sn, strlen(sn));

	aim_tx_enqueue(sess, fr);

	return 0;
}

