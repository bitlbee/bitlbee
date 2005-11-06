
#include <aim.h>

static int reportinterval(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{
	guint16 interval;
	aim_rxcallback_t userfunc;

	interval = aimbs_get16(bs);

	if ((userfunc = aim_callhandler(sess, rx->conn, snac->family, snac->subtype)))
		return userfunc(sess, rx, interval);

	return 0;
}

static int snachandler(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{

	if (snac->subtype == 0x0002)
		return reportinterval(sess, mod, rx, snac, bs);

	return 0;
}

int stats_modfirst(aim_session_t *sess, aim_module_t *mod)
{

	mod->family = 0x000b;
	mod->version = 0x0001;
	mod->toolid = 0x0104;
	mod->toolversion = 0x0001;
	mod->flags = 0;
	strncpy(mod->name, "stats", sizeof(mod->name));
	mod->snachandler = snachandler;

	return 0;
}
