
/*
 * aim_misc.c
 *
 * TODO: Separate a lot of this into an aim_bos.c.
 *
 * Other things...
 *
 *   - Idle setting
 *
 *
 */

#include <aim.h>

/*
 * aim_bos_setprofile(profile)
 *
 * Gives BOS your profile.
 *
 */
int aim_bos_setprofile(aim_session_t *sess, aim_conn_t *conn, const char *profile, const char *awaymsg, guint32 caps)
{
	static const char defencoding[] = { "text/aolrtf; charset=\"utf-8\"" };
	aim_frame_t *fr;
	aim_tlvlist_t *tl = NULL;
	aim_snacid_t snacid;

	/* Build to packet first to get real length */
	if (profile) {
		aim_addtlvtochain_raw(&tl, 0x0001, strlen(defencoding), (guint8 *) defencoding);
		aim_addtlvtochain_raw(&tl, 0x0002, strlen(profile), (guint8 *) profile);
	}

	/*
	 * So here's how this works:
	 *   - You are away when you have a non-zero-length type 4 TLV stored.
	 *   - You become unaway when you clear the TLV with a zero-length
	 *       type 4 TLV.
	 *   - If you do not send the type 4 TLV, your status does not change
	 *       (that is, if you were away, you'll remain away).
	 */
	if (awaymsg) {
		if (strlen(awaymsg)) {
			aim_addtlvtochain_raw(&tl, 0x0003, strlen(defencoding), (guint8 *) defencoding);
			aim_addtlvtochain_raw(&tl, 0x0004, strlen(awaymsg), (guint8 *) awaymsg);
		} else {
			aim_addtlvtochain_noval(&tl, 0x0004);
		}
	}

	aim_addtlvtochain_caps(&tl, 0x0005, caps);

	if (!(fr = aim_tx_new(sess, conn, AIM_FRAMETYPE_FLAP, 0x02, 10 + aim_sizetlvchain(&tl)))) {
		return -ENOMEM;
	}

	snacid = aim_cachesnac(sess, 0x0002, 0x0004, 0x0000, NULL, 0);

	aim_putsnac(&fr->data, 0x0002, 0x004, 0x0000, snacid);
	aim_writetlvchain(&fr->data, &tl);
	aim_freetlvchain(&tl);

	aim_tx_enqueue(sess, fr);

	return 0;
}

/*
 * aim_bos_reqbuddyrights()
 *
 * Request Buddy List rights.
 *
 */
int aim_bos_reqbuddyrights(aim_session_t *sess, aim_conn_t *conn)
{
	return aim_genericreq_n(sess, conn, 0x0003, 0x0002);
}

/*
 * Generic routine for sending commands.
 *
 *
 * I know I can do this in a smarter way...but I'm not thinking straight
 * right now...
 *
 * I had one big function that handled all three cases, but then it broke
 * and I split it up into three.  But then I fixed it.  I just never went
 * back to the single.  I don't see any advantage to doing it either way.
 *
 */
int aim_genericreq_n(aim_session_t *sess, aim_conn_t *conn, guint16 family, guint16 subtype)
{
	aim_frame_t *fr;
	aim_snacid_t snacid = 0x00000000;

	if (!(fr = aim_tx_new(sess, conn, AIM_FRAMETYPE_FLAP, 0x02, 10))) {
		return -ENOMEM;
	}

	aim_putsnac(&fr->data, family, subtype, 0x0000, snacid);

	aim_tx_enqueue(sess, fr);

	return 0;
}

int aim_genericreq_n_snacid(aim_session_t *sess, aim_conn_t *conn, guint16 family, guint16 subtype)
{
	aim_frame_t *fr;
	aim_snacid_t snacid;

	if (!(fr = aim_tx_new(sess, conn, AIM_FRAMETYPE_FLAP, 0x02, 10))) {
		return -ENOMEM;
	}

	snacid = aim_cachesnac(sess, family, subtype, 0x0000, NULL, 0);
	aim_putsnac(&fr->data, family, subtype, 0x0000, snacid);

	aim_tx_enqueue(sess, fr);

	return 0;
}

int aim_genericreq_l(aim_session_t *sess, aim_conn_t *conn, guint16 family, guint16 subtype, guint32 *longdata)
{
	aim_frame_t *fr;
	aim_snacid_t snacid;

	if (!longdata) {
		return aim_genericreq_n(sess, conn, family, subtype);
	}

	if (!(fr = aim_tx_new(sess, conn, AIM_FRAMETYPE_FLAP, 0x02, 10 + 4))) {
		return -ENOMEM;
	}

	snacid = aim_cachesnac(sess, family, subtype, 0x0000, NULL, 0);

	aim_putsnac(&fr->data, family, subtype, 0x0000, snacid);
	aimbs_put32(&fr->data, *longdata);

	aim_tx_enqueue(sess, fr);

	return 0;
}

int aim_genericreq_s(aim_session_t *sess, aim_conn_t *conn, guint16 family, guint16 subtype, guint16 *shortdata)
{
	aim_frame_t *fr;
	aim_snacid_t snacid;

	if (!shortdata) {
		return aim_genericreq_n(sess, conn, family, subtype);
	}

	if (!(fr = aim_tx_new(sess, conn, AIM_FRAMETYPE_FLAP, 0x02, 10 + 2))) {
		return -ENOMEM;
	}

	snacid = aim_cachesnac(sess, family, subtype, 0x0000, NULL, 0);

	aim_putsnac(&fr->data, family, subtype, 0x0000, snacid);
	aimbs_put16(&fr->data, *shortdata);

	aim_tx_enqueue(sess, fr);

	return 0;
}

/*
 * aim_bos_reqlocaterights()
 *
 * Request Location services rights.
 *
 */
int aim_bos_reqlocaterights(aim_session_t *sess, aim_conn_t *conn)
{
	return aim_genericreq_n(sess, conn, 0x0002, 0x0002);
}

/*
 * Should be generic enough to handle the errors for all groups.
 *
 */
static int generror(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{
	int ret = 0;
	int error = 0;
	aim_rxcallback_t userfunc;
	aim_snac_t *snac2;

	snac2 = aim_remsnac(sess, snac->id);

	if (aim_bstream_empty(bs)) {
		error = aimbs_get16(bs);
	}

	if ((userfunc = aim_callhandler(sess, rx->conn, snac->family, snac->subtype))) {
		ret = userfunc(sess, rx, error, snac2 ? snac2->data : NULL);
	}

	if (snac2) {
		g_free(snac2->data);
	}
	g_free(snac2);

	return ret;
}

static int snachandler(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{

	if (snac->subtype == 0x0001) {
		return generror(sess, mod, rx, snac, bs);
	} else if ((snac->family == 0xffff) && (snac->subtype == 0xffff)) {
		aim_rxcallback_t userfunc;

		if ((userfunc = aim_callhandler(sess, rx->conn, snac->family, snac->subtype))) {
			return userfunc(sess, rx);
		}
	}

	return 0;
}

int misc_modfirst(aim_session_t *sess, aim_module_t *mod)
{

	mod->family = 0xffff;
	mod->version = 0x0000;
	mod->flags = AIM_MODFLAG_MULTIFAMILY;
	strncpy(mod->name, "misc", sizeof(mod->name));
	mod->snachandler = snachandler;

	return 0;
}


