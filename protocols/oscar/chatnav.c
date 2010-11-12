/*
 * Handle ChatNav.
 *
 * [The ChatNav(igation) service does various things to keep chat
 *  alive.  It provides room information, room searching and creating, 
 *  as well as giving users the right ("permission") to use chat.]
 *
 */

#include <aim.h>
#include "chatnav.h"

/*
 * conn must be a chatnav connection!
 */
int aim_chatnav_reqrights(aim_session_t *sess, aim_conn_t *conn)
{
	return aim_genericreq_n_snacid(sess, conn, 0x000d, 0x0002);
}

int aim_chatnav_createroom(aim_session_t *sess, aim_conn_t *conn, const char *name, guint16 exchange)
{
	static const char ck[] = {"create"};
	static const char lang[] = {"en"};
	static const char charset[] = {"us-ascii"};
	aim_frame_t *fr;
	aim_snacid_t snacid;
	aim_tlvlist_t *tl = NULL;

	if (!(fr = aim_tx_new(sess, conn, AIM_FRAMETYPE_FLAP, 0x02, 1152)))
		return -ENOMEM;

	snacid = aim_cachesnac(sess, 0x000d, 0x0008, 0x0000, NULL, 0);
	aim_putsnac(&fr->data, 0x000d, 0x0008, 0x0000, snacid);

	/* exchange */
	aimbs_put16(&fr->data, exchange);

	/*
	 * This looks to be a big hack.  You'll note that this entire
	 * SNAC is just a room info structure, but the hard room name,
	 * here, is set to "create".  
	 *
	 * Either this goes on the "list of questions concerning
	 * why-the-hell-did-you-do-that", or this value is completly
	 * ignored.  Without experimental evidence, but a good knowledge of
	 * AOL style, I'm going to guess that it is the latter, and that
	 * the value of the room name in create requests is ignored.
	 */
	aimbs_put8(&fr->data, strlen(ck));
	aimbs_putraw(&fr->data, (guint8 *)ck, strlen(ck));

	/* 
	 * instance
	 * 
	 * Setting this to 0xffff apparently assigns the last instance.
	 *
	 */
	aimbs_put16(&fr->data, 0xffff);

	/* detail level */
	aimbs_put8(&fr->data, 0x01);

	aim_addtlvtochain_raw(&tl, 0x00d3, strlen(name), (guint8 *)name);
	aim_addtlvtochain_raw(&tl, 0x00d6, strlen(charset), (guint8 *)charset);
	aim_addtlvtochain_raw(&tl, 0x00d7, strlen(lang), (guint8 *)lang);

	/* tlvcount */
	aimbs_put16(&fr->data, aim_counttlvchain(&tl));
	aim_writetlvchain(&fr->data, &tl);

	aim_freetlvchain(&tl);

	aim_tx_enqueue(sess, fr);

	return 0;
}

static int parseinfo_perms(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs, aim_snac_t *snac2)
{
	aim_rxcallback_t userfunc;
	int ret = 0;
	struct aim_chat_exchangeinfo *exchanges = NULL;
	int curexchange;
	aim_tlv_t *exchangetlv;
	guint8 maxrooms = 0;
	aim_tlvlist_t *tlvlist, *innerlist;

	tlvlist = aim_readtlvchain(bs);

	/* 
	 * Type 0x0002: Maximum concurrent rooms.
	 */ 
	if (aim_gettlv(tlvlist, 0x0002, 1))
		maxrooms = aim_gettlv8(tlvlist, 0x0002, 1);

	/* 
	 * Type 0x0003: Exchange information
	 *
	 * There can be any number of these, each one
	 * representing another exchange.  
	 * 
	 */
	for (curexchange = 0; ((exchangetlv = aim_gettlv(tlvlist, 0x0003, curexchange+1))); ) {
		aim_bstream_t tbs;

		aim_bstream_init(&tbs, exchangetlv->value, exchangetlv->length);

		curexchange++;

		exchanges = g_realloc(exchanges, curexchange * sizeof(struct aim_chat_exchangeinfo));

		/* exchange number */
		exchanges[curexchange-1].number = aimbs_get16(&tbs);
		innerlist = aim_readtlvchain(&tbs);

		/* 
		 * Type 0x000a: Unknown.
		 *
		 * Usually three bytes: 0x0114 (exchange 1) or 0x010f (others).
		 *
		 */
		if (aim_gettlv(innerlist, 0x000a, 1))
			;

		/* 
		 * Type 0x000d: Unknown.
		 */
		if (aim_gettlv(innerlist, 0x000d, 1))
			;

		/* 
		 * Type 0x0004: Unknown
		 */
		if (aim_gettlv(innerlist, 0x0004, 1))
			;

		/* 
		 * Type 0x0002: Unknown
		 */
		if (aim_gettlv(innerlist, 0x0002, 1)) {
			guint16 classperms;

			classperms = aim_gettlv16(innerlist, 0x0002, 1);
			
		}

		/*
		 * Type 0x00c9: Flags
		 *
		 * 1 Evilable
		 * 2 Nav Only
		 * 4 Instancing Allowed
		 * 8 Occupant Peek Allowed
		 *
		 */ 
		if (aim_gettlv(innerlist, 0x00c9, 1))
			exchanges[curexchange-1].flags = aim_gettlv16(innerlist, 0x00c9, 1);
		      
		/*
		 * Type 0x00ca: Creation Date 
		 */
		if (aim_gettlv(innerlist, 0x00ca, 1))
			;
		      
		/*
		 * Type 0x00d0: Mandatory Channels?
		 */
		if (aim_gettlv(innerlist, 0x00d0, 1))
			;

		/*
		 * Type 0x00d1: Maximum Message length
		 */
		if (aim_gettlv(innerlist, 0x00d1, 1))
			;

		/*
		 * Type 0x00d2: Maximum Occupancy?
		 */
		if (aim_gettlv(innerlist, 0x00d2, 1))	
			;

		/*
		 * Type 0x00d3: Exchange Description
		 */
		if (aim_gettlv(innerlist, 0x00d3, 1))	
			exchanges[curexchange-1].name = aim_gettlv_str(innerlist, 0x00d3, 1);
		else
			exchanges[curexchange-1].name = NULL;

		/*
		 * Type 0x00d4: Exchange Description URL
		 */
		if (aim_gettlv(innerlist, 0x00d4, 1))	
			;

		/*
		 * Type 0x00d5: Creation Permissions
		 *
		 * 0  Creation not allowed
		 * 1  Room creation allowed
		 * 2  Exchange creation allowed
		 * 
		 */
		if (aim_gettlv(innerlist, 0x00d5, 1)) {
			guint8 createperms;

			createperms = aim_gettlv8(innerlist, 0x00d5, 1);
		}

		/*
		 * Type 0x00d6: Character Set (First Time)
		 */	      
		if (aim_gettlv(innerlist, 0x00d6, 1))	
			exchanges[curexchange-1].charset1 = aim_gettlv_str(innerlist, 0x00d6, 1);
		else
			exchanges[curexchange-1].charset1 = NULL;
		      
		/*
		 * Type 0x00d7: Language (First Time)
		 */	      
		if (aim_gettlv(innerlist, 0x00d7, 1))	
			exchanges[curexchange-1].lang1 = aim_gettlv_str(innerlist, 0x00d7, 1);
		else
			exchanges[curexchange-1].lang1 = NULL;

		/*
		 * Type 0x00d8: Character Set (Second Time)
		 */	      
		if (aim_gettlv(innerlist, 0x00d8, 1))	
			exchanges[curexchange-1].charset2 = aim_gettlv_str(innerlist, 0x00d8, 1);
		else
			exchanges[curexchange-1].charset2 = NULL;

		/*
		 * Type 0x00d9: Language (Second Time)
		 */	      
		if (aim_gettlv(innerlist, 0x00d9, 1))	
			exchanges[curexchange-1].lang2 = aim_gettlv_str(innerlist, 0x00d9, 1);
		else
			exchanges[curexchange-1].lang2 = NULL;
		      
		/*
		 * Type 0x00da: Unknown
		 */
		if (aim_gettlv(innerlist, 0x00da, 1))	
			;

		aim_freetlvchain(&innerlist);
	}

	/*
	 * Call client.
	 */
	if ((userfunc = aim_callhandler(sess, rx->conn, snac->family, snac->subtype)))
		ret = userfunc(sess, rx, snac2->type, maxrooms, curexchange, exchanges);

	for (curexchange--; curexchange >= 0; curexchange--) {
		g_free(exchanges[curexchange].name);
		g_free(exchanges[curexchange].charset1);
		g_free(exchanges[curexchange].lang1);
		g_free(exchanges[curexchange].charset2);
		g_free(exchanges[curexchange].lang2);
	}
	g_free(exchanges);
	aim_freetlvchain(&tlvlist);

	return ret;
}

static int parseinfo_create(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs, aim_snac_t *snac2)
{
	aim_rxcallback_t userfunc;
	aim_tlvlist_t *tlvlist, *innerlist;
	char *ck = NULL, *fqcn = NULL, *name = NULL;
	guint16 exchange = 0, instance = 0, unknown = 0, flags = 0, maxmsglen = 0, maxoccupancy = 0;
	guint32 createtime = 0;
	guint8 createperms = 0, detaillevel;
	int cklen;
	aim_tlv_t *bigblock;
	int ret = 0;
	aim_bstream_t bbbs;

	tlvlist = aim_readtlvchain(bs);

	if (!(bigblock = aim_gettlv(tlvlist, 0x0004, 1))) {
		imcb_error(sess->aux_data, "no bigblock in top tlv in create room response");
	
		aim_freetlvchain(&tlvlist);
		return 0;
	}

	aim_bstream_init(&bbbs, bigblock->value, bigblock->length);

	exchange = aimbs_get16(&bbbs);
	cklen = aimbs_get8(&bbbs);
	ck = aimbs_getstr(&bbbs, cklen);
	instance = aimbs_get16(&bbbs);
	detaillevel = aimbs_get8(&bbbs);

	if (detaillevel != 0x02) {
		imcb_error(sess->aux_data, "unknown detaillevel in create room response");
		aim_freetlvchain(&tlvlist);
		g_free(ck);
		return 0;
	}

	unknown = aimbs_get16(&bbbs);

	innerlist = aim_readtlvchain(&bbbs);

	if (aim_gettlv(innerlist, 0x006a, 1))
		fqcn = aim_gettlv_str(innerlist, 0x006a, 1);

	if (aim_gettlv(innerlist, 0x00c9, 1))
		flags = aim_gettlv16(innerlist, 0x00c9, 1);

	if (aim_gettlv(innerlist, 0x00ca, 1))
		createtime = aim_gettlv32(innerlist, 0x00ca, 1);

	if (aim_gettlv(innerlist, 0x00d1, 1))
		maxmsglen = aim_gettlv16(innerlist, 0x00d1, 1);

	if (aim_gettlv(innerlist, 0x00d2, 1))
		maxoccupancy = aim_gettlv16(innerlist, 0x00d2, 1);

	if (aim_gettlv(innerlist, 0x00d3, 1))
		name = aim_gettlv_str(innerlist, 0x00d3, 1);

	if (aim_gettlv(innerlist, 0x00d5, 1))
		createperms = aim_gettlv8(innerlist, 0x00d5, 1);

	if ((userfunc = aim_callhandler(sess, rx->conn, snac->family, snac->subtype))) {
		ret = userfunc(sess, rx, snac2->type, fqcn, instance, exchange, flags, createtime, maxmsglen, maxoccupancy, createperms, unknown, name, ck);
	}

	g_free(ck);
	g_free(name);
	g_free(fqcn);
	aim_freetlvchain(&innerlist);
	aim_freetlvchain(&tlvlist);

	return ret;
}

/*
 * Since multiple things can trigger this callback, we must lookup the 
 * snacid to determine the original snac subtype that was called.
 *
 * XXX This isn't really how this works.  But this is:  Every d/9 response
 * has a 16bit value at the beginning. That matches to:
 *    Short Desc = 1
 *    Full Desc = 2
 *    Instance Info = 4
 *    Nav Short Desc = 8
 *    Nav Instance Info = 16
 * And then everything is really asynchronous.  There is no specific 
 * attachment of a response to a create room request, for example.  Creating
 * the room yields no different a response than requesting the room's info.
 *
 */
static int parseinfo(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{
	aim_snac_t *snac2;
	int ret = 0;

	if (!(snac2 = aim_remsnac(sess, snac->id))) {
		imcb_error(sess->aux_data, "received response to unknown request!");
		return 0;
	}

	if (snac2->family != 0x000d) {
		imcb_error(sess->aux_data, "received response that maps to corrupt request!");
		return 0;
	}

	/*
	 * We now know what the original SNAC subtype was.
	 */
	if (snac2->type == 0x0002) /* request chat rights */
		ret = parseinfo_perms(sess, mod, rx, snac, bs, snac2);
	else if (snac2->type == 0x0003) {} /* request exchange info */
	else if (snac2->type == 0x0004) {} /* request room info */
	else if (snac2->type == 0x0005) {} /* request more room info */
	else if (snac2->type == 0x0006) {} /* request occupant list */
	else if (snac2->type == 0x0007) {} /* search for a room */
	else if (snac2->type == 0x0008) /* create room */
		ret = parseinfo_create(sess, mod, rx, snac, bs, snac2);
	else
		imcb_error(sess->aux_data, "unknown request subtype");

	if (snac2)
		g_free(snac2->data);
	g_free(snac2);

	return ret;
}

static int snachandler(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{

	if (snac->subtype == 0x0009)
		return parseinfo(sess, mod, rx, snac, bs);

	return 0;
}

int chatnav_modfirst(aim_session_t *sess, aim_module_t *mod)
{

	mod->family = 0x000d;
	mod->version = 0x0003;
	mod->toolid = 0x0010;
	mod->toolversion = 0x0629;
	mod->flags = 0;
	strncpy(mod->name, "chatnav", sizeof(mod->name));
	mod->snachandler = snachandler;

	return 0;
}
