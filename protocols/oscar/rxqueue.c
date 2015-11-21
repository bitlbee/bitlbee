/*
 *  aim_rxqueue.c
 *
 * This file contains the management routines for the receive
 * (incoming packet) queue.  The actual packet handlers are in
 * aim_rxhandlers.c.
 */

#include <aim.h>

#include <sys/socket.h>

/*
 *
 */
int aim_recv(int fd, void *buf, size_t count)
{
	int left, cur;

	for (cur = 0, left = count; left; ) {
		int ret;

		ret = recv(fd, ((unsigned char *) buf) + cur, left, 0);

		/* Of course EOF is an error, only morons disagree with that. */
		if (ret <= 0) {
			return -1;
		}

		cur += ret;
		left -= ret;
	}

	return cur;
}

/*
 * Read into a byte stream.  Will not read more than count, but may read
 * less if there is not enough room in the stream buffer.
 */
static int aim_bstream_recv(aim_bstream_t *bs, int fd, size_t count)
{
	int red = 0;

	if (!bs || (fd < 0) || (count < 0)) {
		return -1;
	}

	if (count > (bs->len - bs->offset)) {
		count = bs->len - bs->offset; /* truncate to remaining space */

	}
	if (count) {

		red = aim_recv(fd, bs->data + bs->offset, count);

		if (red <= 0) {
			return -1;
		}
	}

	bs->offset += red;

	return red;
}

int aim_bstream_init(aim_bstream_t *bs, guint8 *data, int len)
{

	if (!bs) {
		return -1;
	}

	bs->data = data;
	bs->len = len;
	bs->offset = 0;

	return 0;
}

int aim_bstream_empty(aim_bstream_t *bs)
{
	return bs->len - bs->offset;
}

int aim_bstream_curpos(aim_bstream_t *bs)
{
	return bs->offset;
}

int aim_bstream_setpos(aim_bstream_t *bs, int off)
{

	if (off > bs->len) {
		return -1;
	}

	bs->offset = off;

	return off;
}

void aim_bstream_rewind(aim_bstream_t *bs)
{

	aim_bstream_setpos(bs, 0);

	return;
}

int aim_bstream_advance(aim_bstream_t *bs, int n)
{

	if (aim_bstream_empty(bs) < n) {
		return 0; /* XXX throw an exception */

	}
	bs->offset += n;

	return n;
}

guint8 aimbs_get8(aim_bstream_t *bs)
{

	if (aim_bstream_empty(bs) < 1) {
		return 0; /* XXX throw an exception */

	}
	bs->offset++;

	return aimutil_get8(bs->data + bs->offset - 1);
}

guint16 aimbs_get16(aim_bstream_t *bs)
{

	if (aim_bstream_empty(bs) < 2) {
		return 0; /* XXX throw an exception */

	}
	bs->offset += 2;

	return aimutil_get16(bs->data + bs->offset - 2);
}

guint32 aimbs_get32(aim_bstream_t *bs)
{

	if (aim_bstream_empty(bs) < 4) {
		return 0; /* XXX throw an exception */

	}
	bs->offset += 4;

	return aimutil_get32(bs->data + bs->offset - 4);
}

guint8 aimbs_getle8(aim_bstream_t *bs)
{

	if (aim_bstream_empty(bs) < 1) {
		return 0; /* XXX throw an exception */

	}
	bs->offset++;

	return aimutil_getle8(bs->data + bs->offset - 1);
}

guint16 aimbs_getle16(aim_bstream_t *bs)
{

	if (aim_bstream_empty(bs) < 2) {
		return 0; /* XXX throw an exception */

	}
	bs->offset += 2;

	return aimutil_getle16(bs->data + bs->offset - 2);
}

guint32 aimbs_getle32(aim_bstream_t *bs)
{

	if (aim_bstream_empty(bs) < 4) {
		return 0; /* XXX throw an exception */

	}
	bs->offset += 4;

	return aimutil_getle32(bs->data + bs->offset - 4);
}

int aimbs_put8(aim_bstream_t *bs, guint8 v)
{

	if (aim_bstream_empty(bs) < 1) {
		return 0; /* XXX throw an exception */

	}
	bs->offset += aimutil_put8(bs->data + bs->offset, v);

	return 1;
}

int aimbs_put16(aim_bstream_t *bs, guint16 v)
{

	if (aim_bstream_empty(bs) < 2) {
		return 0; /* XXX throw an exception */

	}
	bs->offset += aimutil_put16(bs->data + bs->offset, v);

	return 2;
}

int aimbs_put32(aim_bstream_t *bs, guint32 v)
{

	if (aim_bstream_empty(bs) < 4) {
		return 0; /* XXX throw an exception */

	}
	bs->offset += aimutil_put32(bs->data + bs->offset, v);

	return 1;
}

int aimbs_putle8(aim_bstream_t *bs, guint8 v)
{

	if (aim_bstream_empty(bs) < 1) {
		return 0; /* XXX throw an exception */

	}
	bs->offset += aimutil_putle8(bs->data + bs->offset, v);

	return 1;
}

int aimbs_putle16(aim_bstream_t *bs, guint16 v)
{

	if (aim_bstream_empty(bs) < 2) {
		return 0; /* XXX throw an exception */

	}
	bs->offset += aimutil_putle16(bs->data + bs->offset, v);

	return 2;
}

int aimbs_putle32(aim_bstream_t *bs, guint32 v)
{

	if (aim_bstream_empty(bs) < 4) {
		return 0; /* XXX throw an exception */

	}
	bs->offset += aimutil_putle32(bs->data + bs->offset, v);

	return 1;
}

int aimbs_getrawbuf(aim_bstream_t *bs, guint8 *buf, int len)
{

	if (aim_bstream_empty(bs) < len) {
		return 0;
	}

	memcpy(buf, bs->data + bs->offset, len);
	bs->offset += len;

	return len;
}

guint8 *aimbs_getraw(aim_bstream_t *bs, int len)
{
	guint8 *ob;

	if (!(ob = g_malloc(len))) {
		return NULL;
	}

	if (aimbs_getrawbuf(bs, ob, len) < len) {
		g_free(ob);
		return NULL;
	}

	return ob;
}

char *aimbs_getstr(aim_bstream_t *bs, int len)
{
	guint8 *ob;

	if (!(ob = g_malloc(len + 1))) {
		return NULL;
	}

	if (aimbs_getrawbuf(bs, ob, len) < len) {
		g_free(ob);
		return NULL;
	}

	ob[len] = '\0';

	return (char *) ob;
}

int aimbs_putraw(aim_bstream_t *bs, const guint8 *v, int len)
{

	if (aim_bstream_empty(bs) < len) {
		return 0; /* XXX throw an exception */

	}
	memcpy(bs->data + bs->offset, v, len);
	bs->offset += len;

	return len;
}

int aimbs_putbs(aim_bstream_t *bs, aim_bstream_t *srcbs, int len)
{

	if (aim_bstream_empty(srcbs) < len) {
		return 0; /* XXX throw exception (underrun) */

	}
	if (aim_bstream_empty(bs) < len) {
		return 0; /* XXX throw exception (overflow) */

	}
	memcpy(bs->data + bs->offset, srcbs->data + srcbs->offset, len);
	bs->offset += len;
	srcbs->offset += len;

	return len;
}

/**
 * aim_frame_destroy - free aim_frame_t
 * @frame: the frame to free
 *
 * returns -1 on error; 0 on success.
 *
 */
void aim_frame_destroy(aim_frame_t *frame)
{

	g_free(frame->data.data); /* XXX aim_bstream_free */

	g_free(frame);
}


/*
 * Grab a single command sequence off the socket, and enqueue
 * it in the incoming event queue in a separate struct.
 */
int aim_get_command(aim_session_t *sess, aim_conn_t *conn)
{
	guint8 flaphdr_raw[6];
	aim_bstream_t flaphdr;
	aim_frame_t *newrx;
	guint16 payloadlen;

	if (!sess || !conn) {
		return 0;
	}

	if (conn->fd == -1) {
		return -1; /* its a aim_conn_close()'d connection */

	}
	/* KIDS, THIS IS WHAT HAPPENS IF YOU USE CODE WRITTEN FOR GUIS IN A DAEMON!

	   And wouldn't it make sense to return something that prevents this function
	   from being called again IMMEDIATELY (and making the program suck up all
	   CPU time)?...

	if (conn->fd < 3)
	        return 0;
	*/

	if (conn->status & AIM_CONN_STATUS_INPROGRESS) {
		return aim_conn_completeconnect(sess, conn);
	}

	aim_bstream_init(&flaphdr, flaphdr_raw, sizeof(flaphdr_raw));

	/*
	 * Read FLAP header.  Six bytes:
	 *
	 *   0 char  -- Always 0x2a
	 *   1 char  -- Channel ID.  Usually 2 -- 1 and 4 are used during login.
	 *   2 short -- Sequence number
	 *   4 short -- Number of data bytes that follow.
	 */
	if (aim_bstream_recv(&flaphdr, conn->fd, 6) < 6) {
		aim_conn_close(conn);
		return -1;
	}

	aim_bstream_rewind(&flaphdr);

	/*
	 * This shouldn't happen unless the socket breaks, the server breaks,
	 * or we break.  We must handle it just in case.
	 */
	if (aimbs_get8(&flaphdr) != 0x2a) {
		aim_bstream_rewind(&flaphdr);
		aimbs_get8(&flaphdr);
		imcb_error(sess->aux_data, "FLAP framing disrupted");
		aim_conn_close(conn);
		return -1;
	}

	/* allocate a new struct */
	if (!(newrx = (aim_frame_t *) g_new0(aim_frame_t, 1))) {
		return -1;
	}

	/* we're doing FLAP if we're here */
	newrx->hdrtype = AIM_FRAMETYPE_FLAP;

	newrx->hdr.flap.type = aimbs_get8(&flaphdr);
	newrx->hdr.flap.seqnum = aimbs_get16(&flaphdr);
	payloadlen = aimbs_get16(&flaphdr);

	newrx->nofree = 0; /* free by default */

	if (payloadlen) {
		guint8 *payload = NULL;

		if (!(payload = (guint8 *) g_malloc(payloadlen))) {
			aim_frame_destroy(newrx);
			return -1;
		}

		aim_bstream_init(&newrx->data, payload, payloadlen);

		/* read the payload */
		if (aim_bstream_recv(&newrx->data, conn->fd, payloadlen) < payloadlen) {
			aim_frame_destroy(newrx); /* free's payload */
			aim_conn_close(conn);
			return -1;
		}
	} else {
		aim_bstream_init(&newrx->data, NULL, 0);
	}


	aim_bstream_rewind(&newrx->data);

	newrx->conn = conn;

	newrx->next = NULL;  /* this will always be at the bottom */

	if (!sess->queue_incoming) {
		sess->queue_incoming = newrx;
	} else {
		aim_frame_t *cur;

		for (cur = sess->queue_incoming; cur->next; cur = cur->next) {
			;
		}
		cur->next = newrx;
	}

	newrx->conn->lastactivity = time(NULL);

	return 0;
}

/*
 * Purge receive queue of all handled commands (->handled==1).  Also
 * allows for selective freeing using ->nofree so that the client can
 * keep the data for various purposes.
 *
 * If ->nofree is nonzero, the frame will be delinked from the global list,
 * but will not be free'ed.  The client _must_ keep a pointer to the
 * data -- libfaim will not!  If the client marks ->nofree but
 * does not keep a pointer, it's lost forever.
 *
 */
void aim_purge_rxqueue(aim_session_t *sess)
{
	aim_frame_t *cur, **prev;

	for (prev = &sess->queue_incoming; (cur = *prev); ) {
		if (cur->handled) {

			*prev = cur->next;

			if (!cur->nofree) {
				aim_frame_destroy(cur);
			}

		} else {
			prev = &cur->next;
		}
	}

	return;
}

/*
 * Since aim_get_command will aim_conn_kill dead connections, we need
 * to clean up the rxqueue of unprocessed connections on that socket.
 *
 * XXX: this is something that was handled better in the old connection
 * handling method, but eh.
 */
void aim_rxqueue_cleanbyconn(aim_session_t *sess, aim_conn_t *conn)
{
	aim_frame_t *currx;

	for (currx = sess->queue_incoming; currx; currx = currx->next) {
		if ((!currx->handled) && (currx->conn == conn)) {
			currx->handled = 1;
		}
	}
	return;
}

