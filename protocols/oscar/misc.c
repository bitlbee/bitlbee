
/*
 * aim_misc.c
 *
 * TODO: Seperate a lot of this into an aim_bos.c.
 *
 * Other things...
 *
 *   - Idle setting 
 * 
 *
 */

#include <aim.h> 

/*
 * aim_bos_setbuddylist(buddylist)
 *
 * This just builds the "set buddy list" command then queues it.
 *
 * buddy_list = "Screen Name One&ScreenNameTwo&";
 *
 * TODO: Clean this up.  
 *
 * XXX: I can't stress the TODO enough.
 *
 */
int aim_bos_setbuddylist(aim_session_t *sess, aim_conn_t *conn, const char *buddy_list)
{
	aim_frame_t *fr;
	aim_snacid_t snacid;
	int len = 0;
	char *localcpy = NULL;
	char *tmpptr = NULL;

	if (!buddy_list || !(localcpy = g_strdup(buddy_list))) 
		return -EINVAL;

	for (tmpptr = strtok(localcpy, "&"); tmpptr; ) {
		len += 1 + strlen(tmpptr);
		tmpptr = strtok(NULL, "&");
	}

	if (!(fr = aim_tx_new(sess, conn, AIM_FRAMETYPE_FLAP, 0x02, 10+len)))
		return -ENOMEM;

	snacid = aim_cachesnac(sess, 0x0003, 0x0004, 0x0000, NULL, 0);
	aim_putsnac(&fr->data, 0x0003, 0x0004, 0x0000, snacid);

	strncpy(localcpy, buddy_list, strlen(buddy_list) + 1);

	for (tmpptr = strtok(localcpy, "&"); tmpptr; ) {

		aimbs_put8(&fr->data, strlen(tmpptr));
		aimbs_putraw(&fr->data, (guint8 *)tmpptr, strlen(tmpptr));
		tmpptr = strtok(NULL, "&");
	}

	aim_tx_enqueue(sess, fr);

	g_free(localcpy);

	return 0;
}

/* 
 * aim_bos_setprofile(profile)
 *
 * Gives BOS your profile.
 * 
 */
int aim_bos_setprofile(aim_session_t *sess, aim_conn_t *conn, const char *profile, const char *awaymsg, guint32 caps)
{
	static const char defencoding[] = {"text/aolrtf; charset=\"utf-8\""};
	aim_frame_t *fr;
	aim_tlvlist_t *tl = NULL;
	aim_snacid_t snacid;

	/* Build to packet first to get real length */
	if (profile) {
		aim_addtlvtochain_raw(&tl, 0x0001, strlen(defencoding), (guint8 *)defencoding);
		aim_addtlvtochain_raw(&tl, 0x0002, strlen(profile), (guint8 *)profile);
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
			aim_addtlvtochain_raw(&tl, 0x0003, strlen(defencoding), (guint8 *)defencoding);
			aim_addtlvtochain_raw(&tl, 0x0004, strlen(awaymsg), (guint8 *)awaymsg);
		} else
			aim_addtlvtochain_noval(&tl, 0x0004);
	}

	aim_addtlvtochain_caps(&tl, 0x0005, caps);

	if (!(fr = aim_tx_new(sess, conn, AIM_FRAMETYPE_FLAP, 0x02, 10 + aim_sizetlvchain(&tl))))
		return -ENOMEM;

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
 * Send a warning to destsn.
 * 
 * Flags:
 *  AIM_WARN_ANON  Send as an anonymous (doesn't count as much)
 *
 * returns -1 on error (couldn't alloc packet), 0 on success. 
 *
 */
int aim_send_warning(aim_session_t *sess, aim_conn_t *conn, const char *destsn, guint32 flags)
{
	aim_frame_t *fr;
	aim_snacid_t snacid;
	guint16 outflags = 0x0000;

	if (!(fr = aim_tx_new(sess, conn, AIM_FRAMETYPE_FLAP, 0x02, strlen(destsn)+13)))
		return -ENOMEM;

	snacid = aim_cachesnac(sess, 0x0004, 0x0008, 0x0000, destsn, strlen(destsn)+1);

	aim_putsnac(&fr->data, 0x0004, 0x0008, 0x0000, snacid);

	if (flags & AIM_WARN_ANON)
		outflags |= 0x0001;

	aimbs_put16(&fr->data, outflags); 
	aimbs_put8(&fr->data, strlen(destsn));
	aimbs_putraw(&fr->data, (guint8 *)destsn, strlen(destsn));

	aim_tx_enqueue(sess, fr);

	return 0;
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

	if (!(fr = aim_tx_new(sess, conn, AIM_FRAMETYPE_FLAP, 0x02, 10)))
		return -ENOMEM;

	aim_putsnac(&fr->data, family, subtype, 0x0000, snacid);

	aim_tx_enqueue(sess, fr);

	return 0;
}

int aim_genericreq_n_snacid(aim_session_t *sess, aim_conn_t *conn, guint16 family, guint16 subtype)
{
	aim_frame_t *fr;
	aim_snacid_t snacid;

	if (!(fr = aim_tx_new(sess, conn, AIM_FRAMETYPE_FLAP, 0x02, 10)))
		return -ENOMEM;

	snacid = aim_cachesnac(sess, family, subtype, 0x0000, NULL, 0);
	aim_putsnac(&fr->data, family, subtype, 0x0000, snacid);

	aim_tx_enqueue(sess, fr);

	return 0;
}

int aim_genericreq_l(aim_session_t *sess, aim_conn_t *conn, guint16 family, guint16 subtype, guint32 *longdata)
{
	aim_frame_t *fr;
	aim_snacid_t snacid;

	if (!longdata)
		return aim_genericreq_n(sess, conn, family, subtype);

	if (!(fr = aim_tx_new(sess, conn, AIM_FRAMETYPE_FLAP, 0x02, 10+4)))
		return -ENOMEM; 

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

	if (!shortdata)
		return aim_genericreq_n(sess, conn, family, subtype);

	if (!(fr = aim_tx_new(sess, conn, AIM_FRAMETYPE_FLAP, 0x02, 10+2)))
		return -ENOMEM; 

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
 * Set directory profile data (not the same as aim_bos_setprofile!)
 *
 * privacy: 1 to allow searching, 0 to disallow.
 */
int aim_setdirectoryinfo(aim_session_t *sess, aim_conn_t *conn, const char *first, const char *middle, const char *last, const char *maiden, const char *nickname, const char *street, const char *city, const char *state, const char *zip, int country, guint16 privacy) 
{
	aim_frame_t *fr;
	aim_snacid_t snacid;
	aim_tlvlist_t *tl = NULL;


	aim_addtlvtochain16(&tl, 0x000a, privacy);

	if (first)
		aim_addtlvtochain_raw(&tl, 0x0001, strlen(first), (guint8 *)first);
	if (last)
		aim_addtlvtochain_raw(&tl, 0x0002, strlen(last), (guint8 *)last);
	if (middle)
		aim_addtlvtochain_raw(&tl, 0x0003, strlen(middle), (guint8 *)middle);
	if (maiden)
		aim_addtlvtochain_raw(&tl, 0x0004, strlen(maiden), (guint8 *)maiden);

	if (state)
		aim_addtlvtochain_raw(&tl, 0x0007, strlen(state), (guint8 *)state);
	if (city)
		aim_addtlvtochain_raw(&tl, 0x0008, strlen(city), (guint8 *)city);

	if (nickname)
		aim_addtlvtochain_raw(&tl, 0x000c, strlen(nickname), (guint8 *)nickname);
	if (zip)
		aim_addtlvtochain_raw(&tl, 0x000d, strlen(zip), (guint8 *)zip);

	if (street)
		aim_addtlvtochain_raw(&tl, 0x0021, strlen(street), (guint8 *)street);

	if (!(fr = aim_tx_new(sess, conn, AIM_FRAMETYPE_FLAP, 0x02, 10+aim_sizetlvchain(&tl))))
		return -ENOMEM;

	snacid = aim_cachesnac(sess, 0x0002, 0x0009, 0x0000, NULL, 0);
	
	aim_putsnac(&fr->data, 0x0002, 0x0009, 0x0000, snacid);
	aim_writetlvchain(&fr->data, &tl);
	aim_freetlvchain(&tl);

	aim_tx_enqueue(sess, fr);

	return 0;
}

/* XXX pass these in better */
int aim_setuserinterests(aim_session_t *sess, aim_conn_t *conn, const char *interest1, const char *interest2, const char *interest3, const char *interest4, const char *interest5, guint16 privacy)
{
	aim_frame_t *fr;
	aim_tlvlist_t *tl = NULL;

	/* ?? privacy ?? */
	aim_addtlvtochain16(&tl, 0x000a, privacy);

	if (interest1)
		aim_addtlvtochain_raw(&tl, 0x0000b, strlen(interest1), (guint8 *)interest1);
	if (interest2)
		aim_addtlvtochain_raw(&tl, 0x0000b, strlen(interest2), (guint8 *)interest2);
	if (interest3)
		aim_addtlvtochain_raw(&tl, 0x0000b, strlen(interest3), (guint8 *)interest3);
	if (interest4)
		aim_addtlvtochain_raw(&tl, 0x0000b, strlen(interest4), (guint8 *)interest4);
	if (interest5)
		aim_addtlvtochain_raw(&tl, 0x0000b, strlen(interest5), (guint8 *)interest5);

	if (!(fr = aim_tx_new(sess, conn, AIM_FRAMETYPE_FLAP, 0x02, 10+aim_sizetlvchain(&tl))))
		return -ENOMEM;

	aim_cachesnac(sess, 0x0002, 0x000f, 0x0000, NULL, 0);

	aim_putsnac(&fr->data, 0x0002, 0x000f, 0x0000, 0);
	aim_writetlvchain(&fr->data, &tl);
	aim_freetlvchain(&tl);

	aim_tx_enqueue(sess, fr);

	return 0;
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

	if (aim_bstream_empty(bs))
		error = aimbs_get16(bs);

	if ((userfunc = aim_callhandler(sess, rx->conn, snac->family, snac->subtype)))
		ret = userfunc(sess, rx, error, snac2 ? snac2->data : NULL);

	if (snac2)
		g_free(snac2->data);
	g_free(snac2);

	return ret;
}

static int snachandler(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{

	if (snac->subtype == 0x0001)
		return generror(sess, mod, rx, snac, bs);
	else if ((snac->family == 0xffff) && (snac->subtype == 0xffff)) {
		aim_rxcallback_t userfunc;

		if ((userfunc = aim_callhandler(sess, rx->conn, snac->family, snac->subtype)))
			return userfunc(sess, rx);
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


