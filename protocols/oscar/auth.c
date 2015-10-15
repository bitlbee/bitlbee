/*
 * Deals with the authorizer (group 0x0017=23, and old-style non-SNAC login).
 *
 */

#include <aim.h>

#include "md5.h"

static int aim_encode_password(const char *password, unsigned char *encoded);

/*
 * This just pushes the passed cookie onto the passed connection, without
 * the SNAC header or any of that.
 *
 * Very commonly used, as every connection except auth will require this to
 * be the first thing you send.
 *
 */
int aim_sendcookie(aim_session_t *sess, aim_conn_t *conn, const guint8 *chipsahoy)
{
	aim_frame_t *fr;
	aim_tlvlist_t *tl = NULL;

	if (!(fr = aim_tx_new(sess, conn, AIM_FRAMETYPE_FLAP, 0x0001, 4 + 2 + 2 + AIM_COOKIELEN))) {
		return -ENOMEM;
	}

	aimbs_put32(&fr->data, 0x00000001);
	aim_addtlvtochain_raw(&tl, 0x0006, AIM_COOKIELEN, chipsahoy);
	aim_writetlvchain(&fr->data, &tl);
	aim_freetlvchain(&tl);

	aim_tx_enqueue(sess, fr);

	return 0;
}

/*
 * Normally the FLAP version is sent as the first few bytes of the cookie,
 * meaning you generally never call this.
 *
 * But there are times when something might want it separate. Specifically,
 * libfaim sends this internally when doing SNAC login.
 *
 */
int aim_sendflapver(aim_session_t *sess, aim_conn_t *conn)
{
	aim_frame_t *fr;

	if (!(fr = aim_tx_new(sess, conn, AIM_FRAMETYPE_FLAP, 0x01, 4))) {
		return -ENOMEM;
	}

	aimbs_put32(&fr->data, 0x00000001);

	aim_tx_enqueue(sess, fr);

	return 0;
}

/*
 * This is a bit confusing.
 *
 * Normal SNAC login goes like this:
 *   - connect
 *   - server sends flap version
 *   - client sends flap version
 *   - client sends screen name (17/6)
 *   - server sends hash key (17/7)
 *   - client sends auth request (17/2 -- aim_send_login)
 *   - server yells
 *
 * XOR login (for ICQ) goes like this:
 *   - connect
 *   - server sends flap version
 *   - client sends auth request which contains flap version (aim_send_login)
 *   - server yells
 *
 * For the client API, we make them implement the most complicated version,
 * and for the simpler version, we fake it and make it look like the more
 * complicated process.
 *
 * This is done by giving the client a faked key, just so we can convince
 * them to call aim_send_login right away, which will detect the session
 * flag that says this is XOR login and ignore the key, sending an ICQ
 * login request instead of the normal SNAC one.
 *
 * As soon as AOL makes ICQ log in the same way as AIM, this is /gone/.
 *
 * XXX This may cause problems if the client relies on callbacks only
 * being called from the context of aim_rxdispatch()...
 *
 */
static int goddamnicq(aim_session_t *sess, aim_conn_t *conn, const char *sn)
{
	aim_frame_t fr;
	aim_rxcallback_t userfunc;

	sess->flags &= ~AIM_SESS_FLAGS_SNACLOGIN;
	sess->flags |= AIM_SESS_FLAGS_XORLOGIN;

	fr.conn = conn;

	if ((userfunc = aim_callhandler(sess, conn, 0x0017, 0x0007))) {
		userfunc(sess, &fr, "");
	}

	return 0;
}

/*
 * In AIM 3.5 protocol, the first stage of login is to request login from the
 * Authorizer, passing it the screen name for verification.  If the name is
 * invalid, a 0017/0003 is spit back, with the standard error contents.  If
 * valid, a 0017/0007 comes back, which is the signal to send it the main
 * login command (0017/0002).
 *
 */
int aim_request_login(aim_session_t *sess, aim_conn_t *conn, const char *sn)
{
	aim_frame_t *fr;
	aim_snacid_t snacid;
	aim_tlvlist_t *tl = NULL;
	struct im_connection *ic = sess->aux_data;

	if (!sess || !conn || !sn) {
		return -EINVAL;
	}

	if (g_ascii_isdigit(sn[0]) && set_getbool(&ic->acc->set, "old_icq_auth")) {
		return goddamnicq(sess, conn, sn);
	}

	sess->flags |= AIM_SESS_FLAGS_SNACLOGIN;

	aim_sendflapver(sess, conn);

	if (!(fr = aim_tx_new(sess, conn, AIM_FRAMETYPE_FLAP, 0x02, 10 + 2 + 2 + strlen(sn)))) {
		return -ENOMEM;
	}

	snacid = aim_cachesnac(sess, 0x0017, 0x0006, 0x0000, NULL, 0);
	aim_putsnac(&fr->data, 0x0017, 0x0006, 0x0000, snacid);

	aim_addtlvtochain_raw(&tl, 0x0001, strlen(sn), (guint8 *) sn);
	aim_writetlvchain(&fr->data, &tl);
	aim_freetlvchain(&tl);

	aim_tx_enqueue(sess, fr);

	return 0;
}

/*
 * Part two of the ICQ hack.  Note the ignoring of the key and clientinfo.
 */
static int goddamnicq2(aim_session_t *sess, aim_conn_t *conn, const char *sn, const char *password)
{
	static const char clientstr[] = { "ICQ Inc. - Product of ICQ (TM) 2001b.5.17.1.3642.85" };
	static const char lang[] = { "en" };
	static const char country[] = { "us" };
	aim_frame_t *fr;
	aim_tlvlist_t *tl = NULL;
	guint8 *password_encoded;

	if (!(password_encoded = (guint8 *) g_malloc(strlen(password)))) {
		return -ENOMEM;
	}

	if (!(fr = aim_tx_new(sess, conn, AIM_FRAMETYPE_FLAP, 0x01, 1152))) {
		g_free(password_encoded);
		return -ENOMEM;
	}

	aim_encode_password(password, password_encoded);

	aimbs_put32(&fr->data, 0x00000001);
	aim_addtlvtochain_raw(&tl, 0x0001, strlen(sn), (guint8 *) sn);
	aim_addtlvtochain_raw(&tl, 0x0002, strlen(password), password_encoded);
	aim_addtlvtochain_raw(&tl, 0x0003, strlen(clientstr), (guint8 *) clientstr);
	aim_addtlvtochain16(&tl, 0x0016, 0x010a); /* cliend ID */
	aim_addtlvtochain16(&tl, 0x0017, 0x0005); /* major version */
	aim_addtlvtochain16(&tl, 0x0018, 0x0011); /* minor version */
	aim_addtlvtochain16(&tl, 0x0019, 0x0001); /* point version */
	aim_addtlvtochain16(&tl, 0x001a, 0x0e3a); /* build */
	aim_addtlvtochain32(&tl, 0x0014, 0x00000055); /* distribution chan */
	aim_addtlvtochain_raw(&tl, 0x000f, strlen(lang), (guint8 *) lang);
	aim_addtlvtochain_raw(&tl, 0x000e, strlen(country), (guint8 *) country);

	aim_writetlvchain(&fr->data, &tl);

	g_free(password_encoded);
	aim_freetlvchain(&tl);

	aim_tx_enqueue(sess, fr);

	return 0;
}

/*
 * send_login(int socket, char *sn, char *password)
 *
 * This is the initial login request packet.
 *
 * NOTE!! If you want/need to make use of the aim_sendmemblock() function,
 * then the client information you send here must exactly match the
 * executable that you're pulling the data from.
 *
 * WinAIM 4.8.2540
 *   clientstring = "AOL Instant Messenger (SM), version 4.8.2540/WIN32"
 *   clientid = 0x0109
 *   major = 0x0004
 *   minor = 0x0008
 *   point = 0x0000
 *   build = 0x09ec
 *   t(0x0014) = 0x000000af
 *   t(0x004a) = 0x01
 *
 * WinAIM 4.3.2188:
 *   clientstring = "AOL Instant Messenger (SM), version 4.3.2188/WIN32"
 *   clientid = 0x0109
 *   major = 0x0400
 *   minor = 0x0003
 *   point = 0x0000
 *   build = 0x088c
 *   unknown = 0x00000086
 *   lang = "en"
 *   country = "us"
 *   unknown4a = 0x01
 *
 * Latest WinAIM that libfaim can emulate without server-side buddylists:
 *   clientstring = "AOL Instant Messenger (SM), version 4.1.2010/WIN32"
 *   clientid = 0x0004
 *   major  = 0x0004
 *   minor  = 0x0001
 *   point = 0x0000
 *   build  = 0x07da
 *   unknown= 0x0000004b
 *
 * WinAIM 3.5.1670:
 *   clientstring = "AOL Instant Messenger (SM), version 3.5.1670/WIN32"
 *   clientid = 0x0004
 *   major =  0x0003
 *   minor =  0x0005
 *   point = 0x0000
 *   build =  0x0686
 *   unknown =0x0000002a
 *
 * Java AIM 1.1.19:
 *   clientstring = "AOL Instant Messenger (TM) version 1.1.19 for Java built 03/24/98, freeMem 215871 totalMem 1048567, i686, Linus, #2 SMP Sun Feb 11 03:41:17 UTC 2001 2.4.1-ac9, IBM Corporation, 1.1.8, 45.3, Tue Mar 27 12:09:17 PST 2001"
 *   clientid = 0x0001
 *   major  = 0x0001
 *   minor  = 0x0001
 *   point = (not sent)
 *   build  = 0x0013
 *   unknown= (not sent)
 *
 * AIM for Linux 1.1.112:
 *   clientstring = "AOL Instant Messenger (SM)"
 *   clientid = 0x1d09
 *   major  = 0x0001
 *   minor  = 0x0001
 *   point = 0x0001
 *   build  = 0x0070
 *   unknown= 0x0000008b
 *   serverstore = 0x01
 *
 */
int aim_send_login(aim_session_t *sess, aim_conn_t *conn, const char *sn, const char *password,
                   struct client_info_s *ci, const char *key)
{
	aim_frame_t *fr;
	aim_tlvlist_t *tl = NULL;
	guint8 digest[16];
	aim_snacid_t snacid;

	if (!ci || !sn || !password) {
		return -EINVAL;
	}

	/*
	 * What the XORLOGIN flag _really_ means is that its an ICQ login,
	 * which is really stupid and painful, so its not done here.
	 *
	 */
	if (sess->flags & AIM_SESS_FLAGS_XORLOGIN) {
		return goddamnicq2(sess, conn, sn, password);
	}


	if (!(fr = aim_tx_new(sess, conn, AIM_FRAMETYPE_FLAP, 0x02, 1152))) {
		return -ENOMEM;
	}

	snacid = aim_cachesnac(sess, 0x0017, 0x0002, 0x0000, NULL, 0);
	aim_putsnac(&fr->data, 0x0017, 0x0002, 0x0000, snacid);

	aim_addtlvtochain_raw(&tl, 0x0001, strlen(sn), (guint8 *) sn);

	aim_encode_password_md5(password, key, digest);
	aim_addtlvtochain_raw(&tl, 0x0025, 16, digest);

	/*
	 * Newer versions of winaim have an empty type x004c TLV here.
	 */

	if (ci->clientstring) {
		aim_addtlvtochain_raw(&tl, 0x0003, strlen(ci->clientstring), (guint8 *) ci->clientstring);
	}
	aim_addtlvtochain16(&tl, 0x0016, (guint16) ci->clientid);
	aim_addtlvtochain16(&tl, 0x0017, (guint16) ci->major);
	aim_addtlvtochain16(&tl, 0x0018, (guint16) ci->minor);
	aim_addtlvtochain16(&tl, 0x0019, (guint16) ci->point);
	aim_addtlvtochain16(&tl, 0x001a, (guint16) ci->build);
	aim_addtlvtochain_raw(&tl, 0x000e, strlen(ci->country), (guint8 *) ci->country);
	aim_addtlvtochain_raw(&tl, 0x000f, strlen(ci->lang), (guint8 *) ci->lang);

	/*
	 * If set, old-fashioned buddy lists will not work. You will need
	 * to use SSI.
	 */
	aim_addtlvtochain8(&tl, 0x004a, 0x01);

	aim_writetlvchain(&fr->data, &tl);

	aim_freetlvchain(&tl);

	aim_tx_enqueue(sess, fr);

	return 0;
}

int aim_encode_password_md5(const char *password, const char *key, guint8 *digest)
{
	md5_state_t state;

	md5_init(&state);
	md5_append(&state, (const md5_byte_t *) key, strlen(key));
	md5_append(&state, (const md5_byte_t *) password, strlen(password));
	md5_append(&state, (const md5_byte_t *) AIM_MD5_STRING, strlen(AIM_MD5_STRING));
	md5_finish(&state, (md5_byte_t *) digest);

	return 0;
}

/**
 * aim_encode_password - Encode a password using old XOR method
 * @password: incoming password
 * @encoded: buffer to put encoded password
 *
 * This takes a const pointer to a (null terminated) string
 * containing the unencoded password.  It also gets passed
 * an already allocated buffer to store the encoded password.
 * This buffer should be the exact length of the password without
 * the null.  The encoded password buffer /is not %NULL terminated/.
 *
 * The encoding_table seems to be a fixed set of values.  We'll
 * hope it doesn't change over time!
 *
 * This is only used for the XOR method, not the better MD5 method.
 *
 */
static int aim_encode_password(const char *password, guint8 *encoded)
{
	guint8 encoding_table[] = {
		/* v2.1 table, also works for ICQ */
		0xf3, 0x26, 0x81, 0xc4,
		0x39, 0x86, 0xdb, 0x92,
		0x71, 0xa3, 0xb9, 0xe6,
		0x53, 0x7a, 0x95, 0x7c
	};
	int i;

	for (i = 0; i < strlen(password); i++) {
		encoded[i] = (password[i] ^ encoding_table[i]);
	}

	return 0;
}

/*
 * This is sent back as a general response to the login command.
 * It can be either an error or a success, depending on the
 * precense of certain TLVs.
 *
 * The client should check the value passed as errorcode. If
 * its nonzero, there was an error.
 *
 */
static int parse(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{
	aim_tlvlist_t *tlvlist;
	aim_rxcallback_t userfunc;
	struct aim_authresp_info info;
	int ret = 0;

	memset(&info, 0, sizeof(info));

	/*
	 * Read block of TLVs.  All further data is derived
	 * from what is parsed here.
	 */
	tlvlist = aim_readtlvchain(bs);

	/*
	 * No matter what, we should have a screen name.
	 */
	memset(sess->sn, 0, sizeof(sess->sn));
	if (aim_gettlv(tlvlist, 0x0001, 1)) {
		info.sn = aim_gettlv_str(tlvlist, 0x0001, 1);
		strncpy(sess->sn, info.sn, sizeof(sess->sn));
	}

	/*
	 * Check for an error code.  If so, we should also
	 * have an error url.
	 */
	if (aim_gettlv(tlvlist, 0x0008, 1)) {
		info.errorcode = aim_gettlv16(tlvlist, 0x0008, 1);
	}
	if (aim_gettlv(tlvlist, 0x0004, 1)) {
		info.errorurl = aim_gettlv_str(tlvlist, 0x0004, 1);
	}

	/*
	 * BOS server address.
	 */
	if (aim_gettlv(tlvlist, 0x0005, 1)) {
		info.bosip = aim_gettlv_str(tlvlist, 0x0005, 1);
	}

	/*
	 * Authorization cookie.
	 */
	if (aim_gettlv(tlvlist, 0x0006, 1)) {
		aim_tlv_t *tmptlv;

		tmptlv = aim_gettlv(tlvlist, 0x0006, 1);

		info.cookie = tmptlv->value;
	}

	/*
	 * The email address attached to this account
	 *   Not available for ICQ logins.
	 */
	if (aim_gettlv(tlvlist, 0x0011, 1)) {
		info.email = aim_gettlv_str(tlvlist, 0x0011, 1);
	}

	/*
	 * The registration status.  (Not real sure what it means.)
	 *   Not available for ICQ logins.
	 *
	 *   1 = No disclosure
	 *   2 = Limited disclosure
	 *   3 = Full disclosure
	 *
	 * This has to do with whether your email address is available
	 * to other users or not.  AFAIK, this feature is no longer used.
	 *
	 */
	if (aim_gettlv(tlvlist, 0x0013, 1)) {
		info.regstatus = aim_gettlv16(tlvlist, 0x0013, 1);
	}

	if (aim_gettlv(tlvlist, 0x0040, 1)) {
		info.latestbeta.build = aim_gettlv32(tlvlist, 0x0040, 1);
	}
	if (aim_gettlv(tlvlist, 0x0041, 1)) {
		info.latestbeta.url = aim_gettlv_str(tlvlist, 0x0041, 1);
	}
	if (aim_gettlv(tlvlist, 0x0042, 1)) {
		info.latestbeta.info = aim_gettlv_str(tlvlist, 0x0042, 1);
	}
	if (aim_gettlv(tlvlist, 0x0043, 1)) {
		info.latestbeta.name = aim_gettlv_str(tlvlist, 0x0043, 1);
	}
	if (aim_gettlv(tlvlist, 0x0048, 1)) {
		; /* no idea what this is */

	}
	if (aim_gettlv(tlvlist, 0x0044, 1)) {
		info.latestrelease.build = aim_gettlv32(tlvlist, 0x0044, 1);
	}
	if (aim_gettlv(tlvlist, 0x0045, 1)) {
		info.latestrelease.url = aim_gettlv_str(tlvlist, 0x0045, 1);
	}
	if (aim_gettlv(tlvlist, 0x0046, 1)) {
		info.latestrelease.info = aim_gettlv_str(tlvlist, 0x0046, 1);
	}
	if (aim_gettlv(tlvlist, 0x0047, 1)) {
		info.latestrelease.name = aim_gettlv_str(tlvlist, 0x0047, 1);
	}
	if (aim_gettlv(tlvlist, 0x0049, 1)) {
		; /* no idea what this is */


	}
	if ((userfunc = aim_callhandler(sess, rx->conn, snac ? snac->family : 0x0017, snac ? snac->subtype : 0x0003))) {
		ret = userfunc(sess, rx, &info);
	}

	g_free(info.sn);
	g_free(info.bosip);
	g_free(info.errorurl);
	g_free(info.email);
	g_free(info.latestrelease.name);
	g_free(info.latestrelease.url);
	g_free(info.latestrelease.info);
	g_free(info.latestbeta.name);
	g_free(info.latestbeta.url);
	g_free(info.latestbeta.info);

	aim_freetlvchain(&tlvlist);

	return ret;
}

/*
 * Middle handler for 0017/0007 SNACs.  Contains the auth key prefixed
 * by only its length in a two byte word.
 *
 * Calls the client, which should then use the value to call aim_send_login.
 *
 */
static int keyparse(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{
	int keylen, ret = 1;
	aim_rxcallback_t userfunc;
	char *keystr;

	keylen = aimbs_get16(bs);
	keystr = aimbs_getstr(bs, keylen);

	if ((userfunc = aim_callhandler(sess, rx->conn, snac->family, snac->subtype))) {
		ret = userfunc(sess, rx, keystr);
	}

	g_free(keystr);

	return ret;
}

static int snachandler(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{

	if (snac->subtype == 0x0003) {
		return parse(sess, mod, rx, snac, bs);
	} else if (snac->subtype == 0x0007) {
		return keyparse(sess, mod, rx, snac, bs);
	}

	return 0;
}

int auth_modfirst(aim_session_t *sess, aim_module_t *mod)
{

	mod->family = 0x0017;
	mod->version = 0x0000;
	mod->flags = 0;
	strncpy(mod->name, "auth", sizeof(mod->name));
	mod->snachandler = snachandler;

	return 0;
}

