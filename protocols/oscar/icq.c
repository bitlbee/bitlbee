/*
 * Encapsulated ICQ.
 *
 */

#include <aim.h>
#include "icq.h"

int aim_icq_reqofflinemsgs(aim_session_t *sess)
{
	aim_conn_t *conn;
	aim_frame_t *fr;
	aim_snacid_t snacid;
	int bslen;

	if (!sess || !(conn = aim_conn_findbygroup(sess, 0x0015)))
		return -EINVAL;

	bslen = 2 + 4 + 2 + 2;

	if (!(fr = aim_tx_new(sess, conn, AIM_FRAMETYPE_FLAP, 0x02, 10 + 4 + bslen)))
		return -ENOMEM;

	snacid = aim_cachesnac(sess, 0x0015, 0x0002, 0x0000, NULL, 0);
	aim_putsnac(&fr->data, 0x0015, 0x0002, 0x0000, snacid);

	/* For simplicity, don't bother using a tlvlist */
	aimbs_put16(&fr->data, 0x0001);
	aimbs_put16(&fr->data, bslen);

	aimbs_putle16(&fr->data, bslen - 2);
	aimbs_putle32(&fr->data, atoi(sess->sn));
	aimbs_putle16(&fr->data, 0x003c); /* I command thee. */
	aimbs_putle16(&fr->data, snacid); /* eh. */

	aim_tx_enqueue(sess, fr);

	return 0;
}

int aim_icq_ackofflinemsgs(aim_session_t *sess)
{
	aim_conn_t *conn;
	aim_frame_t *fr;
	aim_snacid_t snacid;
	int bslen;

	if (!sess || !(conn = aim_conn_findbygroup(sess, 0x0015)))
		return -EINVAL;

	bslen = 2 + 4 + 2 + 2;

	if (!(fr = aim_tx_new(sess, conn, AIM_FRAMETYPE_FLAP, 0x02, 10 + 4 + bslen)))
		return -ENOMEM;

	snacid = aim_cachesnac(sess, 0x0015, 0x0002, 0x0000, NULL, 0);
	aim_putsnac(&fr->data, 0x0015, 0x0002, 0x0000, snacid);

	/* For simplicity, don't bother using a tlvlist */
	aimbs_put16(&fr->data, 0x0001);
	aimbs_put16(&fr->data, bslen);

	aimbs_putle16(&fr->data, bslen - 2);
	aimbs_putle32(&fr->data, atoi(sess->sn));
	aimbs_putle16(&fr->data, 0x003e); /* I command thee. */
	aimbs_putle16(&fr->data, snacid); /* eh. */

	aim_tx_enqueue(sess, fr);

	return 0;
}

int aim_icq_getallinfo(aim_session_t *sess, const char *uin)
{
        aim_conn_t *conn;
        aim_frame_t *fr;
        aim_snacid_t snacid;
        int bslen;
        struct aim_icq_info *info;

        if (!uin || uin[0] < '0' || uin[0] > '9')
                return -EINVAL;

        if (!sess || !(conn = aim_conn_findbygroup(sess, 0x0015)))
                return -EINVAL;

        bslen = 2 + 4 + 2 + 2 + 2 + 4;

        if (!(fr = aim_tx_new(sess, conn, AIM_FRAMETYPE_FLAP, 0x02, 10 + 4 + bslen)))
                return -ENOMEM;

        snacid = aim_cachesnac(sess, 0x0015, 0x0002, 0x0000, NULL, 0);
        aim_putsnac(&fr->data, 0x0015, 0x0002, 0x0000, snacid);

        /* For simplicity, don't bother using a tlvlist */
        aimbs_put16(&fr->data, 0x0001);
        aimbs_put16(&fr->data, bslen);

        aimbs_putle16(&fr->data, bslen - 2);
        aimbs_putle32(&fr->data, atoi(sess->sn));
        aimbs_putle16(&fr->data, 0x07d0); /* I command thee. */
        aimbs_putle16(&fr->data, snacid); /* eh. */
        aimbs_putle16(&fr->data, 0x04b2); /* shrug. */
        aimbs_putle32(&fr->data, atoi(uin));

        aim_tx_enqueue(sess, fr);

        /* Keep track of this request and the ICQ number and request ID */
        info = g_new0(struct aim_icq_info, 1);
        info->reqid = snacid;
        info->uin = atoi(uin);
        info->next = sess->icq_info;
        sess->icq_info = info;

        return 0;
}

static void aim_icq_freeinfo(struct aim_icq_info *info) {
        int i;

        if (!info)
                return;
        g_free(info->nick);
        g_free(info->first);
        g_free(info->last);
        g_free(info->email);
        g_free(info->homecity);
        g_free(info->homestate);
        g_free(info->homephone);
        g_free(info->homefax);
        g_free(info->homeaddr);
        g_free(info->mobile);
        g_free(info->homezip);
        g_free(info->personalwebpage);
        if (info->email2)
                for (i = 0; i < info->numaddresses; i++)
                        g_free(info->email2[i]);
        g_free(info->email2);
        g_free(info->workcity);
        g_free(info->workstate);
        g_free(info->workphone);
        g_free(info->workfax);
        g_free(info->workaddr);
        g_free(info->workzip);
        g_free(info->workcompany);
        g_free(info->workdivision);
        g_free(info->workposition);
        g_free(info->workwebpage);
        g_free(info->info);
        g_free(info);
}

/**
 * Subtype 0x0003 - Response to 0x0015/0x002, contains an ICQesque packet.
 */
static int icqresponse(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{
	int ret = 0;
	aim_tlvlist_t *tl;
	aim_tlv_t *datatlv;
	aim_bstream_t qbs;
	guint16 cmd, reqid;

	if (!(tl = aim_readtlvchain(bs)) || !(datatlv = aim_gettlv(tl, 0x0001, 1))) {
		aim_freetlvchain(&tl);
		imcb_error(sess->aux_data, "corrupt ICQ response\n");
		return 0;
	}

	aim_bstream_init(&qbs, datatlv->value, datatlv->length);

	aimbs_getle16(&qbs); /* cmdlen */
	aimbs_getle32(&qbs); /* ouruin */
	cmd = aimbs_getle16(&qbs);
	reqid = aimbs_getle16(&qbs);

	if (cmd == 0x0041) { /* offline message */
		guint16 msglen;
		struct aim_icq_offlinemsg msg;
		aim_rxcallback_t userfunc;

		memset(&msg, 0, sizeof(msg));

		msg.sender = aimbs_getle32(&qbs);
		msg.year = aimbs_getle16(&qbs);
		msg.month = aimbs_getle8(&qbs);
		msg.day = aimbs_getle8(&qbs);
		msg.hour = aimbs_getle8(&qbs);
		msg.minute = aimbs_getle8(&qbs);
		msg.type = aimbs_getle16(&qbs);
		msglen = aimbs_getle16(&qbs);
		msg.msg = aimbs_getstr(&qbs, msglen);

		if ((userfunc = aim_callhandler(sess, rx->conn, AIM_CB_FAM_ICQ, AIM_CB_ICQ_OFFLINEMSG)))
			ret = userfunc(sess, rx, &msg);

		g_free(msg.msg);

	} else if (cmd == 0x0042) {
		aim_rxcallback_t userfunc;

		if ((userfunc = aim_callhandler(sess, rx->conn, AIM_CB_FAM_ICQ, AIM_CB_ICQ_OFFLINEMSGCOMPLETE)))
			ret = userfunc(sess, rx);
	} else if (cmd == 0x07da) { /* information */
		guint16 subtype;
		struct aim_icq_info *info;
		aim_rxcallback_t userfunc;

		subtype = aimbs_getle16(&qbs);
		aim_bstream_advance(&qbs, 1); /* 0x0a */

		/* find another data from the same request */
		for (info = sess->icq_info; info && (info->reqid != reqid); info = info->next);

		if (!info) {
			info = g_new0(struct aim_icq_info, 1);
			info->reqid = reqid;
			info->next = sess->icq_info;
			sess->icq_info = info;
		}

		switch (subtype) {
			case 0x00a0: { /* hide ip status */
							 /* nothing */
						 } break;
			case 0x00aa: { /* password change status */
							 /* nothing */
						 } break;
			case 0x00c8: { /* general and "home" information */
							 info->nick = aimbs_getstr(&qbs, aimbs_getle16(&qbs));
							 info->first = aimbs_getstr(&qbs, aimbs_getle16(&qbs));
							 info->last = aimbs_getstr(&qbs, aimbs_getle16(&qbs));
							 info->email = aimbs_getstr(&qbs, aimbs_getle16(&qbs));
							 info->homecity = aimbs_getstr(&qbs, aimbs_getle16(&qbs));
							 info->homestate = aimbs_getstr(&qbs, aimbs_getle16(&qbs));
							 info->homephone = aimbs_getstr(&qbs, aimbs_getle16(&qbs));
							 info->homefax = aimbs_getstr(&qbs, aimbs_getle16(&qbs));
							 info->homeaddr = aimbs_getstr(&qbs, aimbs_getle16(&qbs));
							 info->mobile = aimbs_getstr(&qbs, aimbs_getle16(&qbs));
							 info->homezip = aimbs_getstr(&qbs, aimbs_getle16(&qbs));
							 info->homecountry = aimbs_getle16(&qbs);
							 /* 0x0a 00 02 00 */
							 /* 1 byte timezone? */
							 /* 1 byte hide email flag? */
						 } break;
			case 0x00dc: { /* personal information */
							 info->age = aimbs_getle8(&qbs);
							 info->unknown = aimbs_getle8(&qbs);
							 info->gender = aimbs_getle8(&qbs);
							 info->personalwebpage = aimbs_getstr(&qbs, aimbs_getle16(&qbs));
							 info->birthyear = aimbs_getle16(&qbs);
							 info->birthmonth = aimbs_getle8(&qbs);
							 info->birthday = aimbs_getle8(&qbs);
							 info->language1 = aimbs_getle8(&qbs);
							 info->language2 = aimbs_getle8(&qbs);
							 info->language3 = aimbs_getle8(&qbs);
							 /* 0x00 00 01 00 00 01 00 00 00 00 00 */
						 } break;
			case 0x00d2: { /* work information */
							 info->workcity = aimbs_getstr(&qbs, aimbs_getle16(&qbs));
							 info->workstate = aimbs_getstr(&qbs, aimbs_getle16(&qbs));
							 info->workphone = aimbs_getstr(&qbs, aimbs_getle16(&qbs));
							 info->workfax = aimbs_getstr(&qbs, aimbs_getle16(&qbs));
							 info->workaddr = aimbs_getstr(&qbs, aimbs_getle16(&qbs));
							 info->workzip = aimbs_getstr(&qbs, aimbs_getle16(&qbs));
							 info->workcountry = aimbs_getle16(&qbs);
							 info->workcompany = aimbs_getstr(&qbs, aimbs_getle16(&qbs));
							 info->workdivision = aimbs_getstr(&qbs, aimbs_getle16(&qbs));
							 info->workposition = aimbs_getstr(&qbs, aimbs_getle16(&qbs));
							 aim_bstream_advance(&qbs, 2); /* 0x01 00 */
							 info->workwebpage = aimbs_getstr(&qbs, aimbs_getle16(&qbs));
						 } break;
			case 0x00e6: { /* additional personal information */
							 info->info = aimbs_getstr(&qbs, aimbs_getle16(&qbs)-1);
						 } break;
			case 0x00eb: { /* email address(es) */
							 int i;
							 info->numaddresses = aimbs_getle16(&qbs);
							 info->email2 = g_new0(char *, info->numaddresses);
							 for (i = 0; i < info->numaddresses; i++) {
								 info->email2[i] = aimbs_getstr(&qbs, aimbs_getle16(&qbs));
								 if (i+1 != info->numaddresses)
									 aim_bstream_advance(&qbs, 1); /* 0x00 */
							 }
						 } break;
			case 0x00f0: { /* personal interests */
						 } break;
			case 0x00fa: { /* past background and current organizations */
						 } break;
			case 0x0104: { /* alias info */
							 info->nick = aimbs_getstr(&qbs, aimbs_getle16(&qbs));
							 info->first = aimbs_getstr(&qbs, aimbs_getle16(&qbs));
							 info->last = aimbs_getstr(&qbs, aimbs_getle16(&qbs));
							 aim_bstream_advance(&qbs, aimbs_getle16(&qbs));
							 /* email address? */
							 /* Then 0x00 02 00 */
						 } break;
			case 0x010e: { /* unknown */
							 /* 0x00 00 */
						 } break;

			case 0x019a: { /* simple info */
							 aim_bstream_advance(&qbs, 2);
							 info->uin = aimbs_getle32(&qbs);
							 info->nick = aimbs_getstr(&qbs, aimbs_getle16(&qbs));
							 info->first = aimbs_getstr(&qbs, aimbs_getle16(&qbs));
							 info->last = aimbs_getstr(&qbs, aimbs_getle16(&qbs));
							 info->email = aimbs_getstr(&qbs, aimbs_getle16(&qbs));
							 /* Then 0x00 02 00 00 00 00 00 */
						 } break;
		} /* End switch statement */


		if (!(snac->flags & 0x0001)) {
			if (subtype != 0x0104)
				if ((userfunc = aim_callhandler(sess, rx->conn, AIM_CB_FAM_ICQ, AIM_CB_ICQ_INFO)))
					ret = userfunc(sess, rx, info);

			/* Bitlbee - not supported, yet 
			if (info->uin && info->nick)
				if ((userfunc = aim_callhandler(sess, rx->conn, AIM_CB_FAM_ICQ, AIM_CB_ICQ_ALIAS)))
					ret = userfunc(sess, rx, info);
			*/

			if (sess->icq_info == info) {
				sess->icq_info = info->next;
			} else {
				struct aim_icq_info *cur;
				for (cur=sess->icq_info; (cur->next && (cur->next!=info)); cur=cur->next);
				if (cur->next)
					cur->next = cur->next->next;
			}
			aim_icq_freeinfo(info);
		}
	}

	aim_freetlvchain(&tl);

	return ret;
}

static int snachandler(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{

	if (snac->subtype == 0x0003)
		return icqresponse(sess, mod, rx, snac, bs);

	return 0;
}

int icq_modfirst(aim_session_t *sess, aim_module_t *mod)
{

	mod->family = 0x0015;
	mod->version = 0x0001;
	mod->toolid = 0x0110;
	mod->toolversion = 0x047c;
	mod->flags = 0;
	strncpy(mod->name, "icq", sizeof(mod->name));
	mod->snachandler = snachandler;

	return 0;
}


