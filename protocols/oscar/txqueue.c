/*
 *  aim_txqueue.c
 *
 * Herein lies all the mangement routines for the transmit (Tx) queue.
 *
 */

#include <aim.h>
#include "im.h"

#include <sys/socket.h>

/*
 * Allocate a new tx frame.
 *
 * This is more for looks than anything else.
 *
 * Right now, that is.  If/when we implement a pool of transmit
 * frames, this will become the request-an-unused-frame part.
 *
 * framing = AIM_FRAMETYPE_OFT/FLAP
 * chan = channel for FLAP, hdrtype for OFT
 *
 */
aim_frame_t *aim_tx_new(aim_session_t *sess, aim_conn_t *conn, guint8 framing, guint8 chan, int datalen)
{
	aim_frame_t *fr;

	if (!conn) {
		imcb_error(sess->aux_data, "no connection specified");
		return NULL;
	}

	if (!(fr = (aim_frame_t *) g_new0(aim_frame_t, 1))) {
		return NULL;
	}

	fr->conn = conn;

	fr->hdrtype = framing;

	if (fr->hdrtype == AIM_FRAMETYPE_FLAP) {

		fr->hdr.flap.type = chan;

	} else {
		imcb_error(sess->aux_data, "unknown framing");
	}

	if (datalen > 0) {
		guint8 *data;

		if (!(data = (unsigned char *) g_malloc(datalen))) {
			aim_frame_destroy(fr);
			return NULL;
		}

		aim_bstream_init(&fr->data, data, datalen);
	}

	return fr;
}

/*
 * aim_tx_enqeue__queuebased()
 *
 * The overall purpose here is to enqueue the passed in command struct
 * into the outgoing (tx) queue.  Basically...
 *   1) Make a scope-irrelevant copy of the struct
 *   3) Mark as not-sent-yet
 *   4) Enqueue the struct into the list
 *   6) Return
 *
 * Note that this is only used when doing queue-based transmitting;
 * that is, when sess->tx_enqueue is set to &aim_tx_enqueue__queuebased.
 *
 */
static int aim_tx_enqueue__queuebased(aim_session_t *sess, aim_frame_t *fr)
{

	if (!fr->conn) {
		imcb_error(sess->aux_data, "Warning: enqueueing packet with no connection");
		fr->conn = aim_getconn_type(sess, AIM_CONN_TYPE_BOS);
	}

	if (fr->hdrtype == AIM_FRAMETYPE_FLAP) {
		/* assign seqnum -- XXX should really not assign until hardxmit */
		fr->hdr.flap.seqnum = aim_get_next_txseqnum(fr->conn);
	}

	fr->handled = 0; /* not sent yet */

	/* see overhead note in aim_rxqueue counterpart */
	if (!sess->queue_outgoing) {
		sess->queue_outgoing = fr;
	} else {
		aim_frame_t *cur;

		for (cur = sess->queue_outgoing; cur->next; cur = cur->next) {
			;
		}
		cur->next = fr;
	}

	return 0;
}

/*
 * aim_tx_enqueue__immediate()
 *
 * Parallel to aim_tx_enqueue__queuebased, however, this bypasses
 * the whole queue mess when you want immediate writes to happen.
 *
 * Basically the same as its __queuebased couterpart, however
 * instead of doing a list append, it just calls aim_tx_sendframe()
 * right here.
 *
 */
static int aim_tx_enqueue__immediate(aim_session_t *sess, aim_frame_t *fr)
{

	if (!fr->conn) {
		imcb_error(sess->aux_data, "packet has no connection");
		aim_frame_destroy(fr);
		return 0;
	}

	if (fr->hdrtype == AIM_FRAMETYPE_FLAP) {
		fr->hdr.flap.seqnum = aim_get_next_txseqnum(fr->conn);
	}

	fr->handled = 0; /* not sent yet */

	aim_tx_sendframe(sess, fr);

	aim_frame_destroy(fr);

	return 0;
}

int aim_tx_setenqueue(aim_session_t *sess, int what, int (*func)(aim_session_t *, aim_frame_t *))
{

	if (what == AIM_TX_QUEUED) {
		sess->tx_enqueue = &aim_tx_enqueue__queuebased;
	} else if (what == AIM_TX_IMMEDIATE) {
		sess->tx_enqueue = &aim_tx_enqueue__immediate;
	} else if (what == AIM_TX_USER) {
		if (!func) {
			return -EINVAL;
		}
		sess->tx_enqueue = func;
	} else {
		return -EINVAL; /* unknown action */

	}
	return 0;
}

int aim_tx_enqueue(aim_session_t *sess, aim_frame_t *fr)
{

	/*
	 * If we want to send a connection thats inprogress, we have to force
	 * them to use the queue based version. Otherwise, use whatever they
	 * want.
	 */
	if (fr && fr->conn &&
	    (fr->conn->status & AIM_CONN_STATUS_INPROGRESS)) {
		return aim_tx_enqueue__queuebased(sess, fr);
	}

	return (*sess->tx_enqueue)(sess, fr);
}

/*
 *  aim_get_next_txseqnum()
 *
 *   This increments the tx command count, and returns the seqnum
 *   that should be stamped on the next FLAP packet sent.  This is
 *   normally called during the final step of packet preparation
 *   before enqueuement (in aim_tx_enqueue()).
 *
 */
flap_seqnum_t aim_get_next_txseqnum(aim_conn_t *conn)
{
	flap_seqnum_t ret;

	ret = ++conn->seqnum;

	return ret;
}

static int aim_send(int fd, const void *buf, size_t count)
{
	int left, cur;

	for (cur = 0, left = count; left; ) {
		int ret;

		ret = send(fd, ((unsigned char *) buf) + cur, left, 0);
		if (ret == -1) {
			return -1;
		} else if (ret == 0) {
			return cur;
		}

		cur += ret;
		left -= ret;
	}

	return cur;
}

static int aim_bstream_send(aim_bstream_t *bs, aim_conn_t *conn, size_t count)
{
	int wrote = 0;

	if (!bs || !conn || (count < 0)) {
		return -EINVAL;
	}

	if (count > aim_bstream_empty(bs)) {
		count = aim_bstream_empty(bs); /* truncate to remaining space */

	}
	if (count) {
		if (count - wrote) {
			wrote = wrote + aim_send(conn->fd, bs->data + bs->offset + wrote, count - wrote);
		}

	}

	bs->offset += wrote;

	return wrote;
}

static int sendframe_flap(aim_session_t *sess, aim_frame_t *fr)
{
	aim_bstream_t obs;
	guint8 *obs_raw;
	int payloadlen, err = 0, obslen;

	payloadlen = aim_bstream_curpos(&fr->data);

	if (!(obs_raw = g_malloc(6 + payloadlen))) {
		return -ENOMEM;
	}

	aim_bstream_init(&obs, obs_raw, 6 + payloadlen);

	/* FLAP header */
	aimbs_put8(&obs, 0x2a);
	aimbs_put8(&obs, fr->hdr.flap.type);
	aimbs_put16(&obs, fr->hdr.flap.seqnum);
	aimbs_put16(&obs, payloadlen);

	/* payload */
	aim_bstream_rewind(&fr->data);
	aimbs_putbs(&obs, &fr->data, payloadlen);

	obslen = aim_bstream_curpos(&obs);
	aim_bstream_rewind(&obs);
	if (aim_bstream_send(&obs, fr->conn, obslen) != obslen) {
		err = -errno;
	}

	g_free(obs_raw); /* XXX aim_bstream_free */

	fr->handled = 1;
	fr->conn->lastactivity = time(NULL);

	return err;
}

int aim_tx_sendframe(aim_session_t *sess, aim_frame_t *fr)
{
	if (fr->hdrtype == AIM_FRAMETYPE_FLAP) {
		return sendframe_flap(sess, fr);
	}
	return -1;
}

int aim_tx_flushqueue(aim_session_t *sess)
{
	aim_frame_t *cur;

	for (cur = sess->queue_outgoing; cur; cur = cur->next) {

		if (cur->handled) {
			continue; /* already been sent */

		}
		if (cur->conn && (cur->conn->status & AIM_CONN_STATUS_INPROGRESS)) {
			continue;
		}

		/*
		 * And now for the meager attempt to force transmit
		 * latency and avoid missed messages.
		 */
		if ((cur->conn->lastactivity + cur->conn->forcedlatency) >= time(NULL)) {
			/*
			 * XXX should be a break! we dont want to block the
			 * upper layers
			 *
			 * XXX or better, just do this right.
			 *
			 */
			sleep((cur->conn->lastactivity + cur->conn->forcedlatency) - time(NULL));
		}

		/* XXX this should call the custom "queuing" function!! */
		aim_tx_sendframe(sess, cur);
	}

	/* purge sent commands from queue */
	aim_tx_purgequeue(sess);

	return 0;
}

/*
 *  aim_tx_purgequeue()
 *
 *  This is responsable for removing sent commands from the transmit
 *  queue. This is not a required operation, but it of course helps
 *  reduce memory footprint at run time!
 *
 */
void aim_tx_purgequeue(aim_session_t *sess)
{
	aim_frame_t *cur, **prev;

	for (prev = &sess->queue_outgoing; (cur = *prev); ) {

		if (cur->handled) {
			*prev = cur->next;

			aim_frame_destroy(cur);

		} else {
			prev = &cur->next;
		}
	}

	return;
}

/**
 * aim_tx_cleanqueue - get rid of packets waiting for tx on a dying conn
 * @sess: session
 * @conn: connection that's dying
 *
 * for now this simply marks all packets as sent and lets them
 * disappear without warning.
 *
 */
void aim_tx_cleanqueue(aim_session_t *sess, aim_conn_t *conn)
{
	aim_frame_t *cur;

	for (cur = sess->queue_outgoing; cur; cur = cur->next) {
		if (cur->conn == conn) {
			cur->handled = 1;
		}
	}

	return;
}


