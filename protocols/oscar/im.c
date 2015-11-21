/*
 *  aim_im.c
 *
 *  The routines for sending/receiving Instant Messages.
 *
 *  Note the term ICBM (Inter-Client Basic Message) which blankets
 *  all types of genericly routed through-server messages.  Within
 *  the ICBM types (family 4), a channel is defined.  Each channel
 *  represents a different type of message.  Channel 1 is used for
 *  what would commonly be called an "instant message".  Channel 2
 *  is used for negotiating "rendezvous".  These transactions end in
 *  something more complex happening, such as a chat invitation, or
 *  a file transfer.
 *
 *  In addition to the channel, every ICBM contains a cookie.  For
 *  standard IMs, these are only used for error messages.  However,
 *  the more complex rendezvous messages make suitably more complex
 *  use of this field.
 *
 */

#include <aim.h>
#include "im.h"
#include "info.h"

/*
 * Send an ICBM (instant message).
 *
 *
 * Possible flags:
 *   AIM_IMFLAGS_AWAY  -- Marks the message as an autoresponse
 *   AIM_IMFLAGS_ACK   -- Requests that the server send an ack
 *                        when the message is received (of type 0x0004/0x000c)
 *   AIM_IMFLAGS_OFFLINE--If destination is offline, store it until they are
 *                        online (probably ICQ only).
 *   AIM_IMFLAGS_UNICODE--Instead of ASCII7, the passed message is
 *                        made up of UNICODE duples.  If you set
 *                        this, you'd better be damn sure you know
 *                        what you're doing.
 *   AIM_IMFLAGS_ISO_8859_1 -- The message contains the ASCII8 subset
 *                        known as ISO-8859-1.
 *
 * Generally, you should use the lowest encoding possible to send
 * your message.  If you only use basic punctuation and the generic
 * Latin alphabet, use ASCII7 (no flags).  If you happen to use non-ASCII7
 * characters, but they are all clearly defined in ISO-8859-1, then
 * use that.  Keep in mind that not all characters in the PC ASCII8
 * character set are defined in the ISO standard. For those cases (most
 * notably when the (r) symbol is used), you must use the full UNICODE
 * encoding for your message.  In UNICODE mode, _all_ characters must
 * occupy 16bits, including ones that are not special.  (Remember that
 * the first 128 UNICODE symbols are equivalent to ASCII7, however they
 * must be prefixed with a zero high order byte.)
 *
 * I strongly discourage the use of UNICODE mode, mainly because none
 * of the clients I use can parse those messages (and besides that,
 * wchars are difficult and non-portable to handle in most UNIX environments).
 * If you really need to include special characters, use the HTML UNICODE
 * entities.  These are of the form &#2026; where 2026 is the hex
 * representation of the UNICODE index (in this case, UNICODE
 * "Horizontal Ellipsis", or 133 in in ASCII8).
 *
 * Implementation note:  Since this is one of the most-used functions
 * in all of libfaim, it is written with performance in mind.  As such,
 * it is not as clear as it could be in respect to how this message is
 * supposed to be laid out. Most obviously, tlvlists should be used
 * instead of writing out the bytes manually.
 *
 * XXX more precise verification that we never send SNACs larger than 8192
 * XXX check SNAC size for multipart
 *
 */
int aim_send_im_ext(aim_session_t *sess, struct aim_sendimext_args *args)
{
	static const guint8 deffeatures[] = {
		0x01, 0x01, 0x01, 0x02
	};
	aim_conn_t *conn;
	int i, msgtlvlen;
	aim_frame_t *fr;
	aim_snacid_t snacid;

	if (!sess || !(conn = aim_conn_findbygroup(sess, 0x0004))) {
		return -EINVAL;
	}

	if (!args) {
		return -EINVAL;
	}

	if (args->flags & AIM_IMFLAGS_MULTIPART) {
		if (args->mpmsg->numparts <= 0) {
			return -EINVAL;
		}
	} else {
		if (!args->msg || (args->msglen <= 0)) {
			return -EINVAL;
		}

		if (args->msglen >= MAXMSGLEN) {
			return -E2BIG;
		}
	}

	/* Painfully calculate the size of the message TLV */
	msgtlvlen = 1 + 1; /* 0501 */

	if (args->flags & AIM_IMFLAGS_CUSTOMFEATURES) {
		msgtlvlen += 2 + args->featureslen;
	} else {
		msgtlvlen += 2 + sizeof(deffeatures);
	}

	if (args->flags & AIM_IMFLAGS_MULTIPART) {
		aim_mpmsg_section_t *sec;

		for (sec = args->mpmsg->parts; sec; sec = sec->next) {
			msgtlvlen += 2 /* 0101 */ + 2 /* block len */;
			msgtlvlen += 4 /* charset */ + sec->datalen;
		}

	} else {
		msgtlvlen += 2 /* 0101 */ + 2 /* block len */;
		msgtlvlen += 4 /* charset */ + args->msglen;
	}


	if (!(fr = aim_tx_new(sess, conn, AIM_FRAMETYPE_FLAP, 0x02, msgtlvlen + 128))) {
		return -ENOMEM;
	}

	/* XXX should be optional */
	snacid = aim_cachesnac(sess, 0x0004, 0x0006, 0x0000, args->destsn, strlen(args->destsn) + 1);
	aim_putsnac(&fr->data, 0x0004, 0x0006, 0x0000, snacid);

	/*
	 * Generate a random message cookie
	 *
	 * We could cache these like we do SNAC IDs.  (In fact, it
	 * might be a good idea.)  In the message error functions,
	 * the 8byte message cookie is returned as well as the
	 * SNAC ID.
	 *
	 */
	for (i = 0; i < 8; i++) {
		aimbs_put8(&fr->data, (guint8) rand());
	}

	/*
	 * Channel ID
	 */
	aimbs_put16(&fr->data, 0x0001);

	/*
	 * Destination SN (prepended with byte length)
	 */
	aimbs_put8(&fr->data, strlen(args->destsn));
	aimbs_putraw(&fr->data, (guint8 *) args->destsn, strlen(args->destsn));

	/*
	 * Message TLV (type 2).
	 */
	aimbs_put16(&fr->data, 0x0002);
	aimbs_put16(&fr->data, msgtlvlen);

	/*
	 * Features
	 *
	 */
	aimbs_put8(&fr->data, 0x05);
	aimbs_put8(&fr->data, 0x01);

	if (args->flags & AIM_IMFLAGS_CUSTOMFEATURES) {
		aimbs_put16(&fr->data, args->featureslen);
		aimbs_putraw(&fr->data, args->features, args->featureslen);
	} else {
		aimbs_put16(&fr->data, sizeof(deffeatures));
		aimbs_putraw(&fr->data, deffeatures, sizeof(deffeatures));
	}

	if (args->flags & AIM_IMFLAGS_MULTIPART) {
		aim_mpmsg_section_t *sec;

		for (sec = args->mpmsg->parts; sec; sec = sec->next) {
			aimbs_put16(&fr->data, 0x0101);
			aimbs_put16(&fr->data, sec->datalen + 4);
			aimbs_put16(&fr->data, sec->charset);
			aimbs_put16(&fr->data, sec->charsubset);
			aimbs_putraw(&fr->data, sec->data, sec->datalen);
		}

	} else {

		aimbs_put16(&fr->data, 0x0101);

		/*
		 * Message block length.
		 */
		aimbs_put16(&fr->data, args->msglen + 0x04);

		/*
		 * Character set.
		 */
		if (args->flags & AIM_IMFLAGS_CUSTOMCHARSET) {

			aimbs_put16(&fr->data, args->charset);
			aimbs_put16(&fr->data, args->charsubset);

		} else {
			if (args->flags & AIM_IMFLAGS_UNICODE) {
				aimbs_put16(&fr->data, 0x0002);
			} else if (args->flags & AIM_IMFLAGS_ISO_8859_1) {
				aimbs_put16(&fr->data, 0x0003);
			} else {
				aimbs_put16(&fr->data, 0x0000);
			}

			aimbs_put16(&fr->data, 0x0000);
		}

		/*
		 * Message.  Not terminated.
		 */
		aimbs_putraw(&fr->data, (guint8 *) args->msg, args->msglen);
	}

	/*
	 * Set the Request Acknowledge flag.
	 */
	if (args->flags & AIM_IMFLAGS_ACK) {
		aimbs_put16(&fr->data, 0x0003);
		aimbs_put16(&fr->data, 0x0000);
	}

	/*
	 * Set the Autoresponse flag.
	 */
	if (args->flags & AIM_IMFLAGS_AWAY) {
		aimbs_put16(&fr->data, 0x0004);
		aimbs_put16(&fr->data, 0x0000);
	}

	if (args->flags & AIM_IMFLAGS_OFFLINE) {
		aimbs_put16(&fr->data, 0x0006);
		aimbs_put16(&fr->data, 0x0000);
	}

	/*
	 * Set the I HAVE A REALLY PURTY ICON flag.
	 */
	if (args->flags & AIM_IMFLAGS_HASICON) {
		aimbs_put16(&fr->data, 0x0008);
		aimbs_put16(&fr->data, 0x000c);
		aimbs_put32(&fr->data, args->iconlen);
		aimbs_put16(&fr->data, 0x0001);
		aimbs_put16(&fr->data, args->iconsum);
		aimbs_put32(&fr->data, args->iconstamp);
	}

	/*
	 * Set the Buddy Icon Requested flag.
	 */
	if (args->flags & AIM_IMFLAGS_BUDDYREQ) {
		aimbs_put16(&fr->data, 0x0009);
		aimbs_put16(&fr->data, 0x0000);
	}

	aim_tx_enqueue(sess, fr);

	if (!(sess->flags & AIM_SESS_FLAGS_DONTTIMEOUTONICBM)) {
		aim_cleansnacs(sess, 60); /* clean out SNACs over 60sec old */

	}
	return 0;
}

/*
 * Simple wrapper for aim_send_im_ext()
 *
 * You cannot use aim_send_im if you need the HASICON flag.  You must
 * use aim_send_im_ext directly for that.
 *
 * aim_send_im also cannot be used if you require UNICODE messages, because
 * that requires an explicit message length.  Use aim_send_im_ext().
 *
 */
int aim_send_im(aim_session_t *sess, const char *destsn, guint16 flags, const char *msg)
{
	struct aim_sendimext_args args;

	args.destsn = destsn;
	args.flags = flags;
	args.msg = msg;
	args.msglen = strlen(msg);

	/* Make these don't get set by accident -- they need aim_send_im_ext */
	args.flags &= ~(AIM_IMFLAGS_CUSTOMFEATURES | AIM_IMFLAGS_HASICON | AIM_IMFLAGS_MULTIPART);

	return aim_send_im_ext(sess, &args);
}

/**
 * answers status message requests
 * @param sess the oscar session
 * @param sender the guy whos asking
 * @param cookie message id which we are answering for
 * @param message away message
 * @param state our current away state the way icq requests it (0xE8 for away, 0xE9 occupied, ...)
 * @return 0 if no error
 */
int aim_send_im_ch2_statusmessage(aim_session_t *sess, const char *sender, const guint8 *cookie,
                                  const char *message, const guint8 state, const guint16 dc)
{
	aim_conn_t *conn;
	aim_frame_t *fr;
	aim_snacid_t snacid;

	if (!sess || !(conn = aim_conn_findbygroup(sess, 0x0004))) {
		return -EINVAL;
	}

	if (!(fr = aim_tx_new(sess, conn, AIM_FRAMETYPE_FLAP, 0x02,
	                      10 + 8 + 2 + 1 + strlen(sender) + 2 + 0x1d + 0x10 + 9 + strlen(message) + 1))) {
		return -ENOMEM;
	}

	snacid = aim_cachesnac(sess, 0x0004, 0x000b, 0x0000, NULL, 0);
	aim_putsnac(&fr->data, 0x0004, 0x000b, 0x0000, snacid);

	aimbs_putraw(&fr->data, cookie, 8);

	aimbs_put16(&fr->data, 0x0002); /* channel */
	aimbs_put8(&fr->data, strlen(sender));
	aimbs_putraw(&fr->data, (guint8 *) sender, strlen(sender));

	aimbs_put16(&fr->data, 0x0003); /* reason: channel specific */

	aimbs_putle16(&fr->data, 0x001b); /* length of data SEQ1 */
	aimbs_putle16(&fr->data, 0x0008); /* protocol version */

	aimbs_putle32(&fr->data, 0x0000); /* no plugin -> 16 times 0x00 */
	aimbs_putle32(&fr->data, 0x0000);
	aimbs_putle32(&fr->data, 0x0000);
	aimbs_putle32(&fr->data, 0x0000);

	aimbs_putle16(&fr->data, 0x0000); /* unknown */
	aimbs_putle32(&fr->data, 0x0003); /* client features */
	aimbs_putle8(&fr->data, 0x00); /* unknown */
	aimbs_putle16(&fr->data, dc); /* Sequence number?  XXX - This should decrement by 1 with each request */
	/* end of SEQ1 */

	aimbs_putle16(&fr->data, 0x000e); /* Length of SEQ2 */
	aimbs_putle16(&fr->data, dc); /* Sequence number? same as above
	                               * XXX - This should decrement by 1 with each request */
	aimbs_putle32(&fr->data, 0x00000000); /* Unknown */
	aimbs_putle32(&fr->data, 0x00000000); /* Unknown */
	aimbs_putle32(&fr->data, 0x00000000); /* Unknown */
	/* end of SEQ2 */

	/* now for the real fun */
	aimbs_putle8(&fr->data, state); /* away state */
	aimbs_putle8(&fr->data, 0x03); /* msg-flag: 03 for states */
	aimbs_putle16(&fr->data, 0x0000); /* status code ? */
	aimbs_putle16(&fr->data, 0x0000); /* priority code */
	aimbs_putle16(&fr->data, strlen(message) + 1); /* message length + termination */
	aimbs_putraw(&fr->data, (guint8 *) message, strlen(message) + 1); /* null terminated string */

	aim_tx_enqueue(sess, fr);


	return 0;
}


static int outgoingim(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{
	int i, ret = 0;
	aim_rxcallback_t userfunc;
	guint16 channel;
	aim_tlvlist_t *tlvlist;
	char *sn;
	int snlen;
	guint16 icbmflags = 0;
	guint8 flag1 = 0, flag2 = 0;
	char *msg = NULL;
	aim_tlv_t *msgblock;

	/* ICBM Cookie. */
	for (i = 0; i < 8; i++) {
		aimbs_get8(bs);
	}

	/* Channel ID */
	channel = aimbs_get16(bs);

	if (channel != 0x01) {
		imcb_error(sess->aux_data, "icbm: ICBM received on unsupported channel.  Ignoring.");
		return 0;
	}

	snlen = aimbs_get8(bs);
	sn = aimbs_getstr(bs, snlen);

	tlvlist = aim_readtlvchain(bs);

	if (aim_gettlv(tlvlist, 0x0003, 1)) {
		icbmflags |= AIM_IMFLAGS_ACK;
	}
	if (aim_gettlv(tlvlist, 0x0004, 1)) {
		icbmflags |= AIM_IMFLAGS_AWAY;
	}

	if ((msgblock = aim_gettlv(tlvlist, 0x0002, 1))) {
		aim_bstream_t mbs;
		int featurelen, msglen;

		aim_bstream_init(&mbs, msgblock->value, msgblock->length);

		aimbs_get8(&mbs);
		aimbs_get8(&mbs);
		for (featurelen = aimbs_get16(&mbs); featurelen; featurelen--) {
			aimbs_get8(&mbs);
		}
		aimbs_get8(&mbs);
		aimbs_get8(&mbs);

		msglen = aimbs_get16(&mbs) - 4; /* final block length */

		flag1 = aimbs_get16(&mbs);
		flag2 = aimbs_get16(&mbs);

		msg = aimbs_getstr(&mbs, msglen);
	}

	if ((userfunc = aim_callhandler(sess, rx->conn, snac->family, snac->subtype))) {
		ret = userfunc(sess, rx, channel, sn, msg, icbmflags, flag1, flag2);
	}

	g_free(sn);
	aim_freetlvchain(&tlvlist);

	return ret;
}

/*
 * Ahh, the joys of nearly ridiculous over-engineering.
 *
 * Not only do AIM ICBM's support multiple channels.  Not only do they
 * support multiple character sets.  But they support multiple character
 * sets / encodings within the same ICBM.
 *
 * These multipart messages allow for complex space savings techniques, which
 * seem utterly unnecessary by today's standards.  In fact, there is only
 * one client still in popular use that still uses this method: AOL for the
 * Macintosh, Version 5.0.  Obscure, yes, I know.
 *
 * In modern (non-"legacy") clients, if the user tries to send a character
 * that is not ISO-8859-1 or ASCII, the client will send the entire message
 * as UNICODE, meaning that every character in the message will occupy the
 * full 16 bit UNICODE field, even if the high order byte would be zero.
 * Multipart messages prevent this wasted space by allowing the client to
 * only send the characters in UNICODE that need to be sent that way, and
 * the rest of the message can be sent in whatever the native character
 * set is (probably ASCII).
 *
 * An important note is that sections will be displayed in the order that
 * they appear in the ICBM.  There is no facility for merging or rearranging
 * sections at run time.  So if you have, say, ASCII then UNICODE then ASCII,
 * you must supply two ASCII sections with a UNICODE in the middle, and incur
 * the associated overhead.
 *
 * Normally I would have laughed and given a firm 'no' to supporting this
 * seldom-used feature, but something is attracting me to it.  In the future,
 * it may be possible to abuse this to send mixed-media messages to other
 * open source clients (like encryption or something) -- see faimtest for
 * examples of how to do this.
 *
 * I would definitely recommend avoiding this feature unless you really
 * know what you are doing, and/or you have something neat to do with it.
 *
 */
int aim_mpmsg_init(aim_session_t *sess, aim_mpmsg_t *mpm)
{

	memset(mpm, 0, sizeof(aim_mpmsg_t));

	return 0;
}

static int mpmsg_addsection(aim_session_t *sess, aim_mpmsg_t *mpm, guint16 charset, guint16 charsubset, guint8 *data,
                            guint16 datalen)
{
	aim_mpmsg_section_t *sec;

	if (!(sec = g_new0(aim_mpmsg_section_t, 1))) {
		return -1;
	}

	sec->charset = charset;
	sec->charsubset = charsubset;
	sec->data = data;
	sec->datalen = datalen;
	sec->next = NULL;

	if (!mpm->parts) {
		mpm->parts = sec;
	} else {
		aim_mpmsg_section_t *cur;

		for (cur = mpm->parts; cur->next; cur = cur->next) {
			;
		}
		cur->next = sec;
	}

	mpm->numparts++;

	return 0;
}

void aim_mpmsg_free(aim_session_t *sess, aim_mpmsg_t *mpm)
{
	aim_mpmsg_section_t *cur;

	for (cur = mpm->parts; cur; ) {
		aim_mpmsg_section_t *tmp;

		tmp = cur->next;
		g_free(cur->data);
		g_free(cur);
		cur = tmp;
	}

	mpm->numparts = 0;
	mpm->parts = NULL;

	return;
}

/*
 * Start by building the multipart structures, then pick the first
 * human-readable section and stuff it into args->msg so no one gets
 * suspicious.
 *
 */
static int incomingim_ch1_parsemsgs(aim_session_t *sess, guint8 *data, int len, struct aim_incomingim_ch1_args *args)
{
	static const guint16 charsetpri[] = {
		0x0000, /* ASCII first */
		0x0003, /* then ISO-8859-1 */
		0x0002, /* UNICODE as last resort */
	};
	static const int charsetpricount = 3;
	int i;
	aim_bstream_t mbs;
	aim_mpmsg_section_t *sec;

	aim_bstream_init(&mbs, data, len);

	while (aim_bstream_empty(&mbs)) {
		guint16 msglen, flag1, flag2;
		char *msgbuf;

		aimbs_get8(&mbs); /* 01 */
		aimbs_get8(&mbs); /* 01 */

		/* Message string length, including character set info. */
		msglen = aimbs_get16(&mbs);

		/* Character set info */
		flag1 = aimbs_get16(&mbs);
		flag2 = aimbs_get16(&mbs);

		/* Message. */
		msglen -= 4;

		/*
		 * For now, we don't care what the encoding is.  Just copy
		 * it into a multipart struct and deal with it later. However,
		 * always pad the ending with a NULL.  This makes it easier
		 * to treat ASCII sections as strings.  It won't matter for
		 * UNICODE or binary data, as you should never read past
		 * the specified data length, which will not include the pad.
		 *
		 * XXX There's an API bug here.  For sending, the UNICODE is
		 * given in host byte order (aim_mpmsg_addunicode), but here
		 * the received messages are given in network byte order.
		 *
		 */
		msgbuf = aimbs_getstr(&mbs, msglen);
		mpmsg_addsection(sess, &args->mpmsg, flag1, flag2, (guint8 *) msgbuf, (guint16) msglen);

	} /* while */

	args->icbmflags |= AIM_IMFLAGS_MULTIPART; /* always set */

	/*
	 * Clients that support multiparts should never use args->msg, as it
	 * will point to an arbitrary section.
	 *
	 * Here, we attempt to provide clients that do not support multipart
	 * messages with something to look at -- hopefully a human-readable
	 * string.  But, failing that, a UNICODE message, or nothing at all.
	 *
	 * Which means that even if args->msg is NULL, it does not mean the
	 * message was blank.
	 *
	 */
	for (i = 0; i < charsetpricount; i++) {
		for (sec = args->mpmsg.parts; sec; sec = sec->next) {

			if (sec->charset != charsetpri[i]) {
				continue;
			}

			/* Great. We found one.  Fill it in. */
			args->charset = sec->charset;
			args->charsubset = sec->charsubset;
			args->icbmflags |= AIM_IMFLAGS_CUSTOMCHARSET;

			/* Set up the simple flags */
			if (args->charset == 0x0000) {
				; /* ASCII */
			} else if (args->charset == 0x0002) {
				args->icbmflags |= AIM_IMFLAGS_UNICODE;
			} else if (args->charset == 0x0003) {
				args->icbmflags |= AIM_IMFLAGS_ISO_8859_1;
			} else if (args->charset == 0xffff) {
				; /* no encoding (yeep!) */

			}
			if (args->charsubset == 0x0000) {
				; /* standard subencoding? */
			} else if (args->charsubset == 0x000b) {
				args->icbmflags |= AIM_IMFLAGS_SUBENC_MACINTOSH;
			} else if (args->charsubset == 0xffff) {
				; /* no subencoding */
			}
#if 0
			/* XXX this isn't really necessary... */
			if (((args.flag1 != 0x0000) &&
			     (args.flag1 != 0x0002) &&
			     (args.flag1 != 0x0003) &&
			     (args.flag1 != 0xffff)) ||
			    ((args.flag2 != 0x0000) &&
			     (args.flag2 != 0x000b) &&
			     (args.flag2 != 0xffff))) {
				faimdprintf(sess, 0, "icbm: **warning: encoding flags are being used! {%04x, %04x}\n",
				            args.flag1, args.flag2);
			}
#endif

			args->msg = (char *) sec->data;
			args->msglen = sec->datalen;

			return 0;
		}
	}

	/* No human-readable sections found.  Oh well. */
	args->charset = args->charsubset = 0xffff;
	args->msg = NULL;
	args->msglen = 0;

	return 0;
}

static int incomingim_ch1(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, guint16 channel,
                          aim_userinfo_t *userinfo, aim_bstream_t *bs, guint8 *cookie)
{
	guint16 type, length;
	aim_rxcallback_t userfunc;
	int ret = 0;
	struct aim_incomingim_ch1_args args;
	int endpos;

	memset(&args, 0, sizeof(args));

	aim_mpmsg_init(sess, &args.mpmsg);

	/*
	 * This used to be done using tlvchains.  For performance reasons,
	 * I've changed it to process the TLVs in-place.  This avoids lots
	 * of per-IM memory allocations.
	 */
	while (aim_bstream_empty(bs)) {

		type = aimbs_get16(bs);
		length = aimbs_get16(bs);

		endpos = aim_bstream_curpos(bs) + length;

		if (type == 0x0002) { /* Message Block */

			/*
			 * This TLV consists of the following:
			 *   - 0501 -- Unknown
			 *   - Features: Don't know how to interpret these
			 *   - 0101 -- Unknown
			 *   - Message
			 *
			 */

			aimbs_get8(bs); /* 05 */
			aimbs_get8(bs); /* 01 */

			args.featureslen = aimbs_get16(bs);
			/* XXX XXX this is all evil! */
			args.features = bs->data + bs->offset;
			aim_bstream_advance(bs, args.featureslen);
			args.icbmflags |= AIM_IMFLAGS_CUSTOMFEATURES;

			/*
			 * The rest of the TLV contains one or more message
			 * blocks...
			 */
			incomingim_ch1_parsemsgs(sess, bs->data + bs->offset /* XXX evil!!! */,
			                         length - 2 - 2 - args.featureslen, &args);

		} else if (type == 0x0003) { /* Server Ack Requested */

			args.icbmflags |= AIM_IMFLAGS_ACK;

		} else if (type == 0x0004) { /* Message is Auto Response */

			args.icbmflags |= AIM_IMFLAGS_AWAY;

		} else if (type == 0x0006) { /* Message was received offline. */

			/* XXX not sure if this actually gets sent. */
			args.icbmflags |= AIM_IMFLAGS_OFFLINE;

		} else if (type == 0x0008) { /* I-HAVE-A-REALLY-PURTY-ICON Flag */

			args.iconlen = aimbs_get32(bs);
			aimbs_get16(bs); /* 0x0001 */
			args.iconsum = aimbs_get16(bs);
			args.iconstamp = aimbs_get32(bs);

			/*
			 * This looks to be a client bug.  MacAIM 4.3 will
			 * send this tag, but with all zero values, in the
			 * first message of a conversation. This makes no
			 * sense whatsoever, so I'm going to say its a bug.
			 *
			 * You really shouldn't advertise a zero-length icon
			 * anyway.
			 *
			 */
			if (args.iconlen) {
				args.icbmflags |= AIM_IMFLAGS_HASICON;
			}

		} else if (type == 0x0009) {

			args.icbmflags |= AIM_IMFLAGS_BUDDYREQ;

		} else if (type == 0x0017) {

			args.extdatalen = length;
			args.extdata = aimbs_getraw(bs, args.extdatalen);

		} else {
			// imcb_error(sess->aux_data, "Unknown TLV encountered");
		}

		/*
		 * This is here to protect ourselves from ourselves.  That
		 * is, if something above doesn't completly parse its value
		 * section, or, worse, overparses it, this will set the
		 * stream where it needs to be in order to land on the next
		 * TLV when the loop continues.
		 *
		 */
		aim_bstream_setpos(bs, endpos);
	}


	if ((userfunc = aim_callhandler(sess, rx->conn, snac->family, snac->subtype))) {
		ret = userfunc(sess, rx, channel, userinfo, &args);
	}

	aim_mpmsg_free(sess, &args.mpmsg);
	g_free(args.extdata);

	return ret;
}


static void incomingim_ch2_chat_free(aim_session_t *sess, struct aim_incomingim_ch2_args *args)
{

	/* XXX aim_chat_roominfo_free() */
	g_free(args->info.chat.roominfo.name);

	return;
}

static void incomingim_ch2_chat(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac,
                                aim_userinfo_t *userinfo, struct aim_incomingim_ch2_args *args, aim_bstream_t *servdata)
{

	/*
	 * Chat room info.
	 */
	if (servdata) {
		aim_chat_readroominfo(servdata, &args->info.chat.roominfo);
	}

	args->destructor = (void *) incomingim_ch2_chat_free;

	return;
}

static void incomingim_ch2_icqserverrelay_free(aim_session_t *sess, struct aim_incomingim_ch2_args *args)
{

	g_free((char *) args->info.rtfmsg.rtfmsg);

	return;
}

/*
 * The relationship between AIM_CAPS_ICQSERVERRELAY and AIM_CAPS_ICQRTF is
 * kind of odd. This sends the client ICQRTF since that is all that I've seen
 * SERVERRELAY used for.
 *
 * Note that this is all little-endian.  Cringe.
 *
 * This cap is used for auto status message replies, too [ft]
 *
 */
static void incomingim_ch2_icqserverrelay(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac,
                                          aim_userinfo_t *userinfo, struct aim_incomingim_ch2_args *args,
                                          aim_bstream_t *servdata)
{
	guint16 hdrlen, msglen, dc;
	guint8 msgtype;
	guint8 *plugin;
	int i = 0, tmp = 0;
	struct im_connection *ic = sess->aux_data;

	/* at the moment we just can deal with requests, not with cancel or accept */
	if (args->status != 0) {
		return;
	}

	hdrlen = aimbs_getle16(servdata);

	aim_bstream_advance(servdata, 0x02); /* protocol version */
	plugin = aimbs_getraw(servdata, 0x10); /* following data is a message or
	                                      something plugin specific */
	/* as there is no plugin handling, just skip the rest */
	aim_bstream_advance(servdata, hdrlen - 0x12);

	hdrlen = aimbs_getle16(servdata);
	dc = aimbs_getle16(servdata); /* save the sequence number */
	aim_bstream_advance(servdata, hdrlen - 0x02);

	/* TODO is it a message or something for a plugin? */
	for (i = 0; i < 0x10; i++) {
		tmp |= plugin[i];
	}

	if (!tmp) { /* message follows */

		msgtype = aimbs_getle8(servdata);
		aimbs_getle8(servdata); /* msgflags */

		aim_bstream_advance(servdata, 0x04); /* status code and priority code */

		msglen = aimbs_getle16(servdata); /* message string length */
		args->info.rtfmsg.rtfmsg = aimbs_getstr(servdata, msglen);

		switch (msgtype) {
		case AIM_MTYPE_PLAIN:

			args->info.rtfmsg.fgcolor = aimbs_getle32(servdata);
			args->info.rtfmsg.bgcolor = aimbs_getle32(servdata);

			hdrlen = aimbs_getle32(servdata);
			aim_bstream_advance(servdata, hdrlen);

			/* XXX This is such a hack. */
			args->reqclass = AIM_CAPS_ICQRTF;
			break;

		case AIM_MTYPE_AUTOAWAY:
		case AIM_MTYPE_AUTOBUSY:
		case AIM_MTYPE_AUTONA:
		case AIM_MTYPE_AUTODND:
		case AIM_MTYPE_AUTOFFC:
		case 0x9c: /* ICQ 5 seems to send this */
			aim_send_im_ch2_statusmessage(sess, userinfo->sn, args->cookie,
			                              ic->away ? ic->away : "", sess->aim_icq_state, dc);
			break;

		}
	} /* message or plugin specific */

	g_free(plugin);
	args->destructor = (void *) incomingim_ch2_icqserverrelay_free;

	return;
}

typedef void (*ch2_args_destructor_t)(aim_session_t *sess, struct aim_incomingim_ch2_args *args);

static int incomingim_ch2(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, guint16 channel,
                          aim_userinfo_t *userinfo, aim_tlvlist_t *tlvlist, guint8 *cookie)
{
	aim_rxcallback_t userfunc;
	aim_tlv_t *block1, *servdatatlv;
	aim_tlvlist_t *list2;
	struct aim_incomingim_ch2_args args;
	aim_bstream_t bbs, sdbs, *sdbsptr = NULL;
	guint8 *cookie2;
	int ret = 0;

	char clientip1[30] = { "" };
	char clientip2[30] = { "" };
	char verifiedip[30] = { "" };

	memset(&args, 0, sizeof(args));

	/*
	 * There's another block of TLVs embedded in the type 5 here.
	 */
	block1 = aim_gettlv(tlvlist, 0x0005, 1);
	aim_bstream_init(&bbs, block1->value, block1->length);

	/*
	 * First two bytes represent the status of the connection.
	 *
	 * 0 is a request, 1 is a deny (?), 2 is an accept
	 */
	args.status = aimbs_get16(&bbs);

	/*
	 * Next comes the cookie.  Should match the ICBM cookie.
	 */
	cookie2 = aimbs_getraw(&bbs, 8);
	if (memcmp(cookie, cookie2, 8) != 0) {
		imcb_error(sess->aux_data, "rend: warning cookies don't match!");
	}
	memcpy(args.cookie, cookie2, 8);
	g_free(cookie2);

	/*
	 * The next 16bytes are a capability block so we can
	 * identify what type of rendezvous this is.
	 */
	args.reqclass = aim_getcap(sess, &bbs, 0x10);

	/*
	 * What follows may be TLVs or nothing, depending on the
	 * purpose of the message.
	 *
	 * Ack packets for instance have nothing more to them.
	 */
	list2 = aim_readtlvchain(&bbs);

	/*
	 * IP address from the perspective of the client.
	 */
	if (aim_gettlv(list2, 0x0002, 1)) {
		aim_tlv_t *iptlv;

		iptlv = aim_gettlv(list2, 0x0002, 1);

		g_snprintf(clientip1, sizeof(clientip1), "%d.%d.%d.%d",
		           aimutil_get8(iptlv->value + 0),
		           aimutil_get8(iptlv->value + 1),
		           aimutil_get8(iptlv->value + 2),
		           aimutil_get8(iptlv->value + 3));
	}

	/*
	 * Secondary IP address from the perspective of the client.
	 */
	if (aim_gettlv(list2, 0x0003, 1)) {
		aim_tlv_t *iptlv;

		iptlv = aim_gettlv(list2, 0x0003, 1);

		g_snprintf(clientip2, sizeof(clientip2), "%d.%d.%d.%d",
		           aimutil_get8(iptlv->value + 0),
		           aimutil_get8(iptlv->value + 1),
		           aimutil_get8(iptlv->value + 2),
		           aimutil_get8(iptlv->value + 3));
	}

	/*
	 * Verified IP address (from the perspective of Oscar).
	 *
	 * This is added by the server.
	 */
	if (aim_gettlv(list2, 0x0004, 1)) {
		aim_tlv_t *iptlv;

		iptlv = aim_gettlv(list2, 0x0004, 1);

		g_snprintf(verifiedip, sizeof(verifiedip), "%d.%d.%d.%d",
		           aimutil_get8(iptlv->value + 0),
		           aimutil_get8(iptlv->value + 1),
		           aimutil_get8(iptlv->value + 2),
		           aimutil_get8(iptlv->value + 3));
	}

	/*
	 * Port number for something.
	 */
	if (aim_gettlv(list2, 0x0005, 1)) {
		args.port = aim_gettlv16(list2, 0x0005, 1);
	}

	/*
	 * Error code.
	 */
	if (aim_gettlv(list2, 0x000b, 1)) {
		args.errorcode = aim_gettlv16(list2, 0x000b, 1);
	}

	/*
	 * Invitation message / chat description.
	 */
	if (aim_gettlv(list2, 0x000c, 1)) {
		args.msg = aim_gettlv_str(list2, 0x000c, 1);
	}

	/*
	 * Character set.
	 */
	if (aim_gettlv(list2, 0x000d, 1)) {
		args.encoding = aim_gettlv_str(list2, 0x000d, 1);
	}

	/*
	 * Language.
	 */
	if (aim_gettlv(list2, 0x000e, 1)) {
		args.language = aim_gettlv_str(list2, 0x000e, 1);
	}

	/* Unknown -- two bytes = 0x0001 */
	if (aim_gettlv(list2, 0x000a, 1)) {
		;
	}

	/* Unknown -- no value */
	if (aim_gettlv(list2, 0x000f, 1)) {
		;
	}

	if (strlen(clientip1)) {
		args.clientip = (char *) clientip1;
	}
	if (strlen(clientip2)) {
		args.clientip2 = (char *) clientip2;
	}
	if (strlen(verifiedip)) {
		args.verifiedip = (char *) verifiedip;
	}

	/*
	 * This is must be present in PROPOSALs, but will probably not
	 * exist in CANCELs and ACCEPTs.
	 *
	 * Service Data blocks are module-specific in format.
	 */
	if ((servdatatlv = aim_gettlv(list2, 0x2711 /* 10001 */, 1))) {

		aim_bstream_init(&sdbs, servdatatlv->value, servdatatlv->length);
		sdbsptr = &sdbs;
	}

	if (args.reqclass & AIM_CAPS_ICQSERVERRELAY) {
		incomingim_ch2_icqserverrelay(sess, mod, rx, snac, userinfo, &args, sdbsptr);
	} else if (args.reqclass & AIM_CAPS_CHAT) {
		incomingim_ch2_chat(sess, mod, rx, snac, userinfo, &args, sdbsptr);
	}


	if ((userfunc = aim_callhandler(sess, rx->conn, snac->family, snac->subtype))) {
		ret = userfunc(sess, rx, channel, userinfo, &args);
	}


	if (args.destructor) {
		((ch2_args_destructor_t) args.destructor)(sess, &args);
	}

	g_free((char *) args.msg);
	g_free((char *) args.encoding);
	g_free((char *) args.language);

	aim_freetlvchain(&list2);

	return ret;
}

static int incomingim_ch4(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, guint16 channel,
                          aim_userinfo_t *userinfo, aim_tlvlist_t *tlvlist, guint8 *cookie)
{
	aim_bstream_t meat;
	aim_rxcallback_t userfunc;
	aim_tlv_t *block;
	struct aim_incomingim_ch4_args args;
	int ret = 0;

	/*
	 * Make a bstream for the meaty part.  Yum.  Meat.
	 */
	if (!(block = aim_gettlv(tlvlist, 0x0005, 1))) {
		return -1;
	}
	aim_bstream_init(&meat, block->value, block->length);

	args.uin = aimbs_getle32(&meat);
	args.type = aimbs_getle16(&meat);
	args.msg = (char *) aimbs_getraw(&meat, aimbs_getle16(&meat));

	if ((userfunc = aim_callhandler(sess, rx->conn, snac->family, snac->subtype))) {
		ret = userfunc(sess, rx, channel, userinfo, &args);
	}

	g_free(args.msg);

	return ret;
}

/*
 * It can easily be said that parsing ICBMs is THE single
 * most difficult thing to do in the in AIM protocol.  In
 * fact, I think I just did say that.
 *
 * Below is the best damned solution I've come up with
 * over the past sixteen months of battling with it. This
 * can parse both away and normal messages from every client
 * I have access to.  Its not fast, its not clean.  But it works.
 *
 */
static int incomingim(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{
	int i, ret = 0;
	guint8 cookie[8];
	guint16 channel;
	aim_userinfo_t userinfo;

	memset(&userinfo, 0x00, sizeof(aim_userinfo_t));

	/*
	 * Read ICBM Cookie.  And throw away.
	 */
	for (i = 0; i < 8; i++) {
		cookie[i] = aimbs_get8(bs);
	}

	/*
	 * Channel ID.
	 *
	 * Channel 0x0001 is the message channel.  There are
	 * other channels for things called "rendezvous"
	 * which represent chat and some of the other new
	 * features of AIM2/3/3.5.
	 *
	 * Channel 0x0002 is the Rendezvous channel, which
	 * is where Chat Invitiations and various client-client
	 * connection negotiations come from.
	 *
	 * Channel 0x0004 is used for ICQ authorization, or
	 * possibly any system notice.
	 *
	 */
	channel = aimbs_get16(bs);

	/*
	 * Extract the standard user info block.
	 *
	 * Note that although this contains TLVs that appear contiguous
	 * with the TLVs read below, they are two different pieces.  The
	 * userinfo block contains the number of TLVs that contain user
	 * information, the rest are not even though there is no separation.
	 * aim_extractuserinfo() returns the number of bytes used by the
	 * userinfo tlvs, so you can start reading the rest of them right
	 * afterward.
	 *
	 * That also means that TLV types can be duplicated between the
	 * userinfo block and the rest of the message, however there should
	 * never be two TLVs of the same type in one block.
	 *
	 */
	aim_extractuserinfo(sess, bs, &userinfo);

	/*
	 * From here on, its depends on what channel we're on.
	 *
	 * Technically all channels have a TLV list have this, however,
	 * for the common channel 1 case, in-place parsing is used for
	 * performance reasons (less memory allocation).
	 */
	if (channel == 1) {

		ret = incomingim_ch1(sess, mod, rx, snac, channel, &userinfo, bs, cookie);

	} else if (channel == 2) {
		aim_tlvlist_t *tlvlist;

		/*
		 * Read block of TLVs (not including the userinfo data).  All
		 * further data is derived from what is parsed here.
		 */
		tlvlist = aim_readtlvchain(bs);

		ret = incomingim_ch2(sess, mod, rx, snac, channel, &userinfo, tlvlist, cookie);

		aim_freetlvchain(&tlvlist);

	} else if (channel == 4) {
		aim_tlvlist_t *tlvlist;

		tlvlist = aim_readtlvchain(bs);
		ret = incomingim_ch4(sess, mod, rx, snac, channel, &userinfo, tlvlist, cookie);
		aim_freetlvchain(&tlvlist);

	} else {

		imcb_error(sess->aux_data, "ICBM received on an unsupported channel.  Ignoring.");

		return 0;
	}

	return ret;
}

/*
 * aim_reqicbmparaminfo()
 *
 * Request ICBM parameter information.
 *
 */
int aim_reqicbmparams(aim_session_t *sess)
{
	aim_conn_t *conn;

	if (!sess || !(conn = aim_conn_findbygroup(sess, 0x0004))) {
		return -EINVAL;
	}

	return aim_genericreq_n(sess, conn, 0x0004, 0x0004);
}

/*
 *
 * I definitely recommend sending this.  If you don't, you'll be stuck
 * with the rather unreasonable defaults.  You don't want those.  Send this.
 *
 */
int aim_seticbmparam(aim_session_t *sess, struct aim_icbmparameters *params)
{
	aim_conn_t *conn;
	aim_frame_t *fr;
	aim_snacid_t snacid;

	if (!sess || !(conn = aim_conn_findbygroup(sess, 0x0004))) {
		return -EINVAL;
	}

	if (!params) {
		return -EINVAL;
	}

	if (!(fr = aim_tx_new(sess, conn, AIM_FRAMETYPE_FLAP, 0x02, 10 + 16))) {
		return -ENOMEM;
	}

	snacid = aim_cachesnac(sess, 0x0004, 0x0002, 0x0000, NULL, 0);
	aim_putsnac(&fr->data, 0x0004, 0x0002, 0x0000, snacid);

	/* This is read-only (see Parameter Reply). Must be set to zero here. */
	aimbs_put16(&fr->data, 0x0000);

	/* These are all read-write */
	aimbs_put32(&fr->data, params->flags);
	aimbs_put16(&fr->data, params->maxmsglen);
	aimbs_put16(&fr->data, params->maxsenderwarn);
	aimbs_put16(&fr->data, params->maxrecverwarn);
	aimbs_put32(&fr->data, params->minmsginterval);

	aim_tx_enqueue(sess, fr);

	return 0;
}

static int paraminfo(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{
	struct aim_icbmparameters params;
	aim_rxcallback_t userfunc;

	params.maxchan = aimbs_get16(bs);
	params.flags = aimbs_get32(bs);
	params.maxmsglen = aimbs_get16(bs);
	params.maxsenderwarn = aimbs_get16(bs);
	params.maxrecverwarn = aimbs_get16(bs);
	params.minmsginterval = aimbs_get32(bs);

	if ((userfunc = aim_callhandler(sess, rx->conn, snac->family, snac->subtype))) {
		return userfunc(sess, rx, &params);
	}

	return 0;
}

static int missedcall(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{
	int ret = 0;
	aim_rxcallback_t userfunc;
	guint16 channel, nummissed, reason;
	aim_userinfo_t userinfo;

	while (aim_bstream_empty(bs)) {

		channel = aimbs_get16(bs);
		aim_extractuserinfo(sess, bs, &userinfo);
		nummissed = aimbs_get16(bs);
		reason = aimbs_get16(bs);

		if ((userfunc = aim_callhandler(sess, rx->conn, snac->family, snac->subtype))) {
			ret = userfunc(sess, rx, channel, &userinfo, nummissed, reason);
		}
	}

	return ret;
}

/*
 * Receive the response from an ICQ status message request.  This contains the
 * ICQ status message.  Go figure.
 */
static int clientautoresp(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac,
                          aim_bstream_t *bs)
{
	int ret = 0;
	aim_rxcallback_t userfunc;
	guint16 channel, reason;
	char *sn;
	guint8 *ck, snlen;

	ck = aimbs_getraw(bs, 8);
	channel = aimbs_get16(bs);
	snlen = aimbs_get8(bs);
	sn = aimbs_getstr(bs, snlen);
	reason = aimbs_get16(bs);

	switch (reason) {
	case 0x0003: {         /* ICQ status message.  Maybe other stuff too, you never know with these people. */
		guint8 statusmsgtype, *msg;
		guint16 len;
		guint32 state;

		len = aimbs_getle16(bs);         /* Should be 0x001b */
		aim_bstream_advance(bs, len);         /* Unknown */

		len = aimbs_getle16(bs);         /* Should be 0x000e */
		aim_bstream_advance(bs, len);         /* Unknown */

		statusmsgtype = aimbs_getle8(bs);
		switch (statusmsgtype) {
		case 0xe8:
			state = AIM_ICQ_STATE_AWAY;
			break;
		case 0xe9:
			state = AIM_ICQ_STATE_AWAY | AIM_ICQ_STATE_BUSY;
			break;
		case 0xea:
			state = AIM_ICQ_STATE_AWAY | AIM_ICQ_STATE_OUT;
			break;
		case 0xeb:
			state = AIM_ICQ_STATE_AWAY | AIM_ICQ_STATE_DND | AIM_ICQ_STATE_BUSY;
			break;
		case 0xec:
			state = AIM_ICQ_STATE_CHAT;
			break;
		default:
			state = 0;
			break;
		}

		aimbs_getle8(bs);         /* Unknown - 0x03 Maybe this means this is an auto-reply */
		aimbs_getle16(bs);         /* Unknown - 0x0000 */
		aimbs_getle16(bs);         /* Unknown - 0x0000 */

		len = aimbs_getle16(bs);
		msg = aimbs_getraw(bs, len);

		if ((userfunc = aim_callhandler(sess, rx->conn, snac->family, snac->subtype))) {
			ret = userfunc(sess, rx, channel, sn, reason, state, msg);
		}

		g_free(msg);
	} break;

	default: {
		if ((userfunc = aim_callhandler(sess, rx->conn, snac->family, snac->subtype))) {
			ret = userfunc(sess, rx, channel, sn, reason);
		}
	} break;
	} /* end switch */

	g_free(ck);
	g_free(sn);

	return ret;
}

static int msgack(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{
	aim_rxcallback_t userfunc;
	guint16 type;
	guint8 snlen, *ck;
	char *sn;
	int ret = 0;

	ck = aimbs_getraw(bs, 8);
	type = aimbs_get16(bs);
	snlen = aimbs_get8(bs);
	sn = aimbs_getstr(bs, snlen);

	if ((userfunc = aim_callhandler(sess, rx->conn, snac->family, snac->subtype))) {
		ret = userfunc(sess, rx, type, sn);
	}

	g_free(sn);
	g_free(ck);

	return ret;
}

/*
 * Subtype 0x0014 - Send a mini typing notification (mtn) packet.
 *
 * This is supported by winaim5 and newer, MacAIM bleh and newer, iChat bleh and newer,
 * and Gaim 0.60 and newer.
 *
 */
int aim_im_sendmtn(aim_session_t *sess, guint16 type1, const char *sn, guint16 type2)
{
	aim_conn_t *conn;
	aim_frame_t *fr;
	aim_snacid_t snacid;

	if (!sess || !(conn = aim_conn_findbygroup(sess, 0x0002))) {
		return -EINVAL;
	}

	if (!sn) {
		return -EINVAL;
	}

	if (!(fr = aim_tx_new(sess, conn, AIM_FRAMETYPE_FLAP, 0x02, 10 + 11 + strlen(sn) + 2))) {
		return -ENOMEM;
	}

	snacid = aim_cachesnac(sess, 0x0004, 0x0014, 0x0000, NULL, 0);
	aim_putsnac(&fr->data, 0x0004, 0x0014, 0x0000, snacid);

	/*
	 * 8 days of light
	 * Er, that is to say, 8 bytes of 0's
	 */
	aimbs_put16(&fr->data, 0x0000);
	aimbs_put16(&fr->data, 0x0000);
	aimbs_put16(&fr->data, 0x0000);
	aimbs_put16(&fr->data, 0x0000);

	/*
	 * Type 1 (should be 0x0001 for mtn)
	 */
	aimbs_put16(&fr->data, type1);

	/*
	 * Dest sn
	 */
	aimbs_put8(&fr->data, strlen(sn));
	aimbs_putraw(&fr->data, (const guint8 *) sn, strlen(sn));

	/*
	 * Type 2 (should be 0x0000, 0x0001, or 0x0002 for mtn)
	 */
	aimbs_put16(&fr->data, type2);

	aim_tx_enqueue(sess, fr);

	return 0;
}

/*
 * Subtype 0x0014 - Receive a mini typing notification (mtn) packet.
 *
 * This is supported by winaim5 and newer, MacAIM bleh and newer, iChat bleh and newer,
 * and Gaim 0.60 and newer.
 *
 */
static int mtn_receive(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{
	int ret = 0;
	aim_rxcallback_t userfunc;
	char *sn;
	guint8 snlen;
	guint16 type1, type2;

	aim_bstream_advance(bs, 8); /* Unknown - All 0's */
	type1 = aimbs_get16(bs);
	snlen = aimbs_get8(bs);
	sn = aimbs_getstr(bs, snlen);
	type2 = aimbs_get16(bs);

	if ((userfunc = aim_callhandler(sess, rx->conn, snac->family, snac->subtype))) {
		ret = userfunc(sess, rx, type1, sn, type2);
	}

	g_free(sn);

	return ret;
}

static int snachandler(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{

	if (snac->subtype == 0x0005) {
		return paraminfo(sess, mod, rx, snac, bs);
	} else if (snac->subtype == 0x0006) {
		return outgoingim(sess, mod, rx, snac, bs);
	} else if (snac->subtype == 0x0007) {
		return incomingim(sess, mod, rx, snac, bs);
	} else if (snac->subtype == 0x000a) {
		return missedcall(sess, mod, rx, snac, bs);
	} else if (snac->subtype == 0x000b) {
		return clientautoresp(sess, mod, rx, snac, bs);
	} else if (snac->subtype == 0x000c) {
		return msgack(sess, mod, rx, snac, bs);
	} else if (snac->subtype == 0x0014) {
		return mtn_receive(sess, mod, rx, snac, bs);
	}

	return 0;
}

int msg_modfirst(aim_session_t *sess, aim_module_t *mod)
{

	mod->family = 0x0004;
	mod->version = 0x0001;
	mod->toolid = 0x0110;
	mod->toolversion = 0x0629;
	mod->flags = 0;
	strncpy(mod->name, "messaging", sizeof(mod->name));
	mod->snachandler = snachandler;

	return 0;
}
