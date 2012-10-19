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


