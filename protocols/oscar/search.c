
/*
 * aim_search.c
 *
 * TODO: Add aim_usersearch_name()
 *
 */

#include <aim.h>

/* XXX can this be integrated with the rest of the error handling? */
static int error(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{
	int ret = 0;
	aim_rxcallback_t userfunc;
	aim_snac_t *snac2;

	/* XXX the modules interface should have already retrieved this for us */
	if (!(snac2 = aim_remsnac(sess, snac->id))) {
		imcb_error(sess->aux_data, "couldn't get snac");
		return 0;
	}

	if ((userfunc = aim_callhandler(sess, rx->conn, snac->family, snac->subtype)))
		ret = userfunc(sess, rx, snac2->data /* address */);

	/* XXX freesnac()? */
	if (snac2)
		g_free(snac2->data);
	g_free(snac2);

	return ret;
}

static int reply(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{
	int j = 0, m, ret = 0;
	aim_tlvlist_t *tlvlist;
	char *cur = NULL, *buf = NULL;
	aim_rxcallback_t userfunc;
	aim_snac_t *snac2;
	char *searchaddr = NULL;

	if ((snac2 = aim_remsnac(sess, snac->id)))
		searchaddr = (char *)snac2->data;

	tlvlist = aim_readtlvchain(bs);
	m = aim_counttlvchain(&tlvlist);

	/* XXX uhm. */
	while ((cur = aim_gettlv_str(tlvlist, 0x0001, j+1)) && j < m) {
		buf = g_realloc(buf, (j+1) * (MAXSNLEN+1));

		strncpy(&buf[j * (MAXSNLEN+1)], cur, MAXSNLEN);
		g_free(cur);

		j++; 
	}

	aim_freetlvchain(&tlvlist);

	if ((userfunc = aim_callhandler(sess, rx->conn, snac->family, snac->subtype)))
		ret = userfunc(sess, rx, searchaddr, j, buf);

	/* XXX freesnac()? */
	if (snac2)
		g_free(snac2->data);
	g_free(snac2);

	g_free(buf);

	return ret;
}

static int snachandler(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{

	if (snac->subtype == 0x0001)
		return error(sess, mod, rx, snac, bs);
	else if (snac->subtype == 0x0003)
		return reply(sess, mod, rx, snac, bs);

	return 0;
}

int search_modfirst(aim_session_t *sess, aim_module_t *mod)
{

	mod->family = 0x000a;
	mod->version = 0x0001;
	mod->toolid = 0x0110;
	mod->toolversion = 0x0629;
	mod->flags = 0;
	strncpy(mod->name, "search", sizeof(mod->name));
	mod->snachandler = snachandler;

	return 0;
}


