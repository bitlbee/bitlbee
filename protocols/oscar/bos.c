#include <aim.h>
#include "bos.h"

/* Request BOS rights (group 9, type 2) */
int aim_bos_reqrights(aim_session_t *sess, aim_conn_t *conn)
{
	return aim_genericreq_n(sess, conn, 0x0009, 0x0002);
}

/* BOS Rights (group 9, type 3) */
static int rights(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{
	aim_rxcallback_t userfunc;
	aim_tlvlist_t *tlvlist;
	guint16 maxpermits = 0, maxdenies = 0;
	int ret = 0;

	/* 
	 * TLVs follow 
	 */
	tlvlist = aim_readtlvchain(bs);

	/*
	 * TLV type 0x0001: Maximum number of buddies on permit list.
	 */
	if (aim_gettlv(tlvlist, 0x0001, 1))
		maxpermits = aim_gettlv16(tlvlist, 0x0001, 1);

	/*
	 * TLV type 0x0002: Maximum number of buddies on deny list.
	 */
	if (aim_gettlv(tlvlist, 0x0002, 1)) 
		maxdenies = aim_gettlv16(tlvlist, 0x0002, 1);

	if ((userfunc = aim_callhandler(sess, rx->conn, snac->family, snac->subtype)))
		ret = userfunc(sess, rx, maxpermits, maxdenies);

	aim_freetlvchain(&tlvlist);

	return ret;  
}

/* 
 * Set group permisson mask (group 9, type 4)
 *
 * Normally 0x1f (all classes).
 *
 * The group permission mask allows you to keep users of a certain
 * class or classes from talking to you.  The mask should be
 * a bitwise OR of all the user classes you want to see you.
 *
 */
int aim_bos_setgroupperm(aim_session_t *sess, aim_conn_t *conn, guint32 mask)
{
	return aim_genericreq_l(sess, conn, 0x0009, 0x0004, &mask);
}

/*
 * Modify permit/deny lists (group 9, types 5, 6, 7, and 8)
 *
 * Changes your visibility depending on changetype:
 *
 *  AIM_VISIBILITYCHANGE_PERMITADD: Lets provided list of names see you
 *  AIM_VISIBILITYCHANGE_PERMIDREMOVE: Removes listed names from permit list
 *  AIM_VISIBILITYCHANGE_DENYADD: Hides you from provided list of names
 *  AIM_VISIBILITYCHANGE_DENYREMOVE: Lets list see you again
 *
 * list should be a list of 
 * screen names in the form "Screen Name One&ScreenNameTwo&" etc.
 *
 * Equivelents to options in WinAIM:
 *   - Allow all users to contact me: Send an AIM_VISIBILITYCHANGE_DENYADD
 *      with only your name on it.
 *   - Allow only users on my Buddy List: Send an 
 *      AIM_VISIBILITYCHANGE_PERMITADD with the list the same as your
 *      buddy list
 *   - Allow only the uesrs below: Send an AIM_VISIBILITYCHANGE_PERMITADD 
 *      with everyone listed that you want to see you.
 *   - Block all users: Send an AIM_VISIBILITYCHANGE_PERMITADD with only 
 *      yourself in the list
 *   - Block the users below: Send an AIM_VISIBILITYCHANGE_DENYADD with
 *      the list of users to be blocked
 *
 * XXX ye gods.
 */
int aim_bos_changevisibility(aim_session_t *sess, aim_conn_t *conn, int changetype, const char *denylist)
{
	aim_frame_t *fr;
	int packlen = 0;
	guint16 subtype;
	char *localcpy = NULL, *tmpptr = NULL;
	int i;
	int listcount;
	aim_snacid_t snacid;

	if (!denylist)
		return -EINVAL;

	if (changetype == AIM_VISIBILITYCHANGE_PERMITADD)
		subtype = 0x05;
	else if (changetype == AIM_VISIBILITYCHANGE_PERMITREMOVE)
		subtype = 0x06;
	else if (changetype == AIM_VISIBILITYCHANGE_DENYADD)
		subtype = 0x07;
	else if (changetype == AIM_VISIBILITYCHANGE_DENYREMOVE)
		subtype = 0x08;
	else
		return -EINVAL;

	localcpy = g_strdup(denylist);

	listcount = aimutil_itemcnt(localcpy, '&');
	packlen = aimutil_tokslen(localcpy, 99, '&') + listcount + 9;

	if (!(fr = aim_tx_new(sess, conn, AIM_FRAMETYPE_FLAP, 0x02, packlen))) {
		g_free(localcpy);
		return -ENOMEM;
	}

	snacid = aim_cachesnac(sess, 0x0009, subtype, 0x0000, NULL, 0);
	aim_putsnac(&fr->data, 0x0009, subtype, 0x00, snacid);

	for (i = 0; (i < (listcount - 1)) && (i < 99); i++) {
		tmpptr = aimutil_itemidx(localcpy, i, '&');

		aimbs_put8(&fr->data, strlen(tmpptr));
		aimbs_putraw(&fr->data, (guint8 *)tmpptr, strlen(tmpptr));

		g_free(tmpptr);
	}
	g_free(localcpy);

	aim_tx_enqueue(sess, fr);

	return 0;
}

static int snachandler(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{

	if (snac->subtype == 0x0003)
		return rights(sess, mod, rx, snac, bs);

	return 0;
}

int bos_modfirst(aim_session_t *sess, aim_module_t *mod)
{

	mod->family = 0x0009;
	mod->version = 0x0001;
	mod->toolid = 0x0110;
	mod->toolversion = 0x0629;
	mod->flags = 0;
	strncpy(mod->name, "bos", sizeof(mod->name));
	mod->snachandler = snachandler;

	return 0;
}


