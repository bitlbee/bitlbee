#include <aim.h>

static void freetlv(aim_tlv_t **oldtlv)
{
	if (!oldtlv || !*oldtlv) {
		return;
	}

	g_free((*oldtlv)->value);
	g_free(*oldtlv);
	*oldtlv = NULL;
}

/**
 * aim_readtlvchain - Read a TLV chain from a buffer.
 * @buf: Input buffer
 * @maxlen: Length of input buffer
 *
 * Reads and parses a series of TLV patterns from a data buffer; the
 * returned structure is manipulatable with the rest of the TLV
 * routines.  When done with a TLV chain, aim_freetlvchain() should
 * be called to free the dynamic substructures.
 *
 * XXX There should be a flag setable here to have the tlvlist contain
 * bstream references, so that at least the ->value portion of each
 * element doesn't need to be malloc/memcpy'd.  This could prove to be
 * just as efficient as the in-place TLV parsing used in a couple places
 * in libfaim.
 *
 */
aim_tlvlist_t *aim_readtlvchain(aim_bstream_t *bs)
{
	aim_tlvlist_t *list = NULL, *cur;
	guint16 type, length;

	while (aim_bstream_empty(bs)) {

		type = aimbs_get16(bs);
		length = aimbs_get16(bs);

		cur = g_new0(aim_tlvlist_t, 1);

		cur->tlv = g_new0(aim_tlv_t, 1);
		cur->tlv->type = type;
		if ((cur->tlv->length = length)) {
			cur->tlv->value = aimbs_getraw(bs, length);
		}

		cur->next = list;
		list = cur;
	}

	return list;
}

/**
 * aim_freetlvchain - Free a TLV chain structure
 * @list: Chain to be freed
 *
 * Walks the list of TLVs in the passed TLV chain and
 * frees each one. Note that any references to this data
 * should be removed before calling this.
 *
 */
void aim_freetlvchain(aim_tlvlist_t **list)
{
	aim_tlvlist_t *cur;

	if (!list || !*list) {
		return;
	}

	for (cur = *list; cur; ) {
		aim_tlvlist_t *tmp;

		freetlv(&cur->tlv);

		tmp = cur->next;
		g_free(cur);
		cur = tmp;
	}

	list = NULL;

	return;
}

/**
 * aim_counttlvchain - Count the number of TLVs in a chain
 * @list: Chain to be counted
 *
 * Returns the number of TLVs stored in the passed chain.
 *
 */
int aim_counttlvchain(aim_tlvlist_t **list)
{
	aim_tlvlist_t *cur;
	int count;

	if (!list || !*list) {
		return 0;
	}

	for (cur = *list, count = 0; cur; cur = cur->next) {
		count++;
	}

	return count;
}

/**
 * aim_sizetlvchain - Count the number of bytes in a TLV chain
 * @list: Chain to be sized
 *
 * Returns the number of bytes that would be needed to
 * write the passed TLV chain to a data buffer.
 *
 */
int aim_sizetlvchain(aim_tlvlist_t **list)
{
	aim_tlvlist_t *cur;
	int size;

	if (!list || !*list) {
		return 0;
	}

	for (cur = *list, size = 0; cur; cur = cur->next) {
		size += (4 + cur->tlv->length);
	}

	return size;
}

/**
 * aim_addtlvtochain_str - Add a string to a TLV chain
 * @list: Designation chain (%NULL pointer if empty)
 * @type: TLV type
 * @str: String to add
 * @len: Length of string to add (not including %NULL)
 *
 * Adds the passed string as a TLV element of the passed type
 * to the TLV chain.
 *
 */
int aim_addtlvtochain_raw(aim_tlvlist_t **list, const guint16 t, const guint16 l, const guint8 *v)
{
	aim_tlvlist_t *newtlv, *cur;

	if (!list) {
		return 0;
	}

	if (!(newtlv = g_new0(aim_tlvlist_t, 1))) {
		return 0;
	}

	if (!(newtlv->tlv = g_new0(aim_tlv_t, 1))) {
		g_free(newtlv);
		return 0;
	}
	newtlv->tlv->type = t;
	if ((newtlv->tlv->length = l)) {
		newtlv->tlv->value = (guint8 *) g_malloc(newtlv->tlv->length);
		memcpy(newtlv->tlv->value, v, newtlv->tlv->length);
	}

	if (!*list) {
		*list = newtlv;
	} else {
		for (cur = *list; cur->next; cur = cur->next) {
			;
		}
		cur->next = newtlv;
	}

	return newtlv->tlv->length;
}

/**
 * aim_addtlvtochain8 - Add a 8bit integer to a TLV chain
 * @list: Destination chain
 * @type: TLV type to add
 * @val: Value to add
 *
 * Adds a one-byte unsigned integer to a TLV chain.
 *
 */
int aim_addtlvtochain8(aim_tlvlist_t **list, const guint16 t, const guint8 v)
{
	guint8 v8[1];

	(void) aimutil_put8(v8, v);

	return aim_addtlvtochain_raw(list, t, 1, v8);
}

/**
 * aim_addtlvtochain16 - Add a 16bit integer to a TLV chain
 * @list: Destination chain
 * @type: TLV type to add
 * @val: Value to add
 *
 * Adds a two-byte unsigned integer to a TLV chain.
 *
 */
int aim_addtlvtochain16(aim_tlvlist_t **list, const guint16 t, const guint16 v)
{
	guint8 v16[2];

	(void) aimutil_put16(v16, v);

	return aim_addtlvtochain_raw(list, t, 2, v16);
}

/**
 * aim_addtlvtochain32 - Add a 32bit integer to a TLV chain
 * @list: Destination chain
 * @type: TLV type to add
 * @val: Value to add
 *
 * Adds a four-byte unsigned integer to a TLV chain.
 *
 */
int aim_addtlvtochain32(aim_tlvlist_t **list, const guint16 t, const guint32 v)
{
	guint8 v32[4];

	(void) aimutil_put32(v32, v);

	return aim_addtlvtochain_raw(list, t, 4, v32);
}

/**
 * aim_addtlvtochain_caps - Add a capability block to a TLV chain
 * @list: Destination chain
 * @type: TLV type to add
 * @caps: Bitfield of capability flags to send
 *
 * Adds a block of capability blocks to a TLV chain. The bitfield
 * passed in should be a bitwise %OR of any of the %AIM_CAPS constants:
 *
 */
int aim_addtlvtochain_caps(aim_tlvlist_t **list, const guint16 t, const guint32 caps)
{
	guint8 buf[16 * 16]; /* XXX icky fixed length buffer */
	aim_bstream_t bs;

	if (!caps) {
		return 0; /* nothing there anyway */

	}
	aim_bstream_init(&bs, buf, sizeof(buf));

	aim_putcap(&bs, caps);

	return aim_addtlvtochain_raw(list, t, aim_bstream_curpos(&bs), buf);
}

/**
 * aim_addtlvtochain_noval - Add a blank TLV to a TLV chain
 * @list: Destination chain
 * @type: TLV type to add
 *
 * Adds a TLV with a zero length to a TLV chain.
 *
 */
int aim_addtlvtochain_noval(aim_tlvlist_t **list, const guint16 t)
{
	return aim_addtlvtochain_raw(list, t, 0, NULL);
}

/*
 * Note that the inner TLV chain will not be modifiable as a tlvchain once
 * it is written using this.  Or rather, it can be, but updates won't be
 * made to this.
 *
 * XXX should probably support sublists for real.
 *
 * This is so neat.
 *
 */
int aim_addtlvtochain_frozentlvlist(aim_tlvlist_t **list, guint16 type, aim_tlvlist_t **tl)
{
	guint8 *buf;
	int buflen;
	aim_bstream_t bs;

	buflen = aim_sizetlvchain(tl);

	if (buflen <= 0) {
		return 0;
	}

	if (!(buf = g_malloc(buflen))) {
		return 0;
	}

	aim_bstream_init(&bs, buf, buflen);

	aim_writetlvchain(&bs, tl);

	aim_addtlvtochain_raw(list, type, aim_bstream_curpos(&bs), buf);

	g_free(buf);

	return buflen;
}

int aim_addtlvtochain_chatroom(aim_tlvlist_t **list, guint16 type, guint16 exchange, const char *roomname,
                               guint16 instance)
{
	guint8 *buf;
	int buflen;
	aim_bstream_t bs;

	buflen = 2 + 1 + strlen(roomname) + 2;

	if (!(buf = g_malloc(buflen))) {
		return 0;
	}

	aim_bstream_init(&bs, buf, buflen);

	aimbs_put16(&bs, exchange);
	aimbs_put8(&bs, strlen(roomname));
	aimbs_putraw(&bs, (guint8 *) roomname, strlen(roomname));
	aimbs_put16(&bs, instance);

	aim_addtlvtochain_raw(list, type, aim_bstream_curpos(&bs), buf);

	g_free(buf);

	return 0;
}

/**
 * aim_writetlvchain - Write a TLV chain into a data buffer.
 * @buf: Destination buffer
 * @buflen: Maximum number of bytes that will be written to buffer
 * @list: Source TLV chain
 *
 * Copies a TLV chain into a raw data buffer, writing only the number
 * of bytes specified. This operation does not free the chain;
 * aim_freetlvchain() must still be called to free up the memory used
 * by the chain structures.
 *
 * XXX clean this up, make better use of bstreams
 */
int aim_writetlvchain(aim_bstream_t *bs, aim_tlvlist_t **list)
{
	int goodbuflen;
	aim_tlvlist_t *cur;

	/* do an initial run to test total length */
	for (cur = *list, goodbuflen = 0; cur; cur = cur->next) {
		goodbuflen += 2 + 2; /* type + len */
		goodbuflen += cur->tlv->length;
	}

	if (goodbuflen > aim_bstream_empty(bs)) {
		return 0; /* not enough buffer */

	}
	/* do the real write-out */
	for (cur = *list; cur; cur = cur->next) {
		aimbs_put16(bs, cur->tlv->type);
		aimbs_put16(bs, cur->tlv->length);
		if (cur->tlv->length) {
			aimbs_putraw(bs, cur->tlv->value, cur->tlv->length);
		}
	}

	return 1; /* XXX this is a nonsensical return */
}


/**
 * aim_gettlv - Grab the Nth TLV of type type in the TLV list list.
 * @list: Source chain
 * @type: Requested TLV type
 * @nth: Index of TLV of type to get
 *
 * Returns a pointer to an aim_tlv_t of the specified type;
 * %NULL on error.  The @nth parameter is specified starting at %1.
 * In most cases, there will be no more than one TLV of any type
 * in a chain.
 *
 */
aim_tlv_t *aim_gettlv(aim_tlvlist_t *list, const guint16 t, const int n)
{
	aim_tlvlist_t *cur;
	int i;

	for (cur = list, i = 0; cur; cur = cur->next) {
		if (cur && cur->tlv) {
			if (cur->tlv->type == t) {
				i++;
			}
			if (i >= n) {
				return cur->tlv;
			}
		}
	}

	return NULL;
}

/**
 * aim_gettlv_str - Retrieve the Nth TLV in chain as a string.
 * @list: Source TLV chain
 * @type: TLV type to search for
 * @nth: Index of TLV to return
 *
 * Same as aim_gettlv(), except that the return value is a %NULL-
 * terminated string instead of an aim_tlv_t.  This is a
 * dynamic buffer and must be freed by the caller.
 *
 */
char *aim_gettlv_str(aim_tlvlist_t *list, const guint16 t, const int n)
{
	aim_tlv_t *tlv;
	char *newstr;

	if (!(tlv = aim_gettlv(list, t, n))) {
		return NULL;
	}

	newstr = (char *) g_malloc(tlv->length + 1);
	memcpy(newstr, tlv->value, tlv->length);
	*(newstr + tlv->length) = '\0';

	return newstr;
}

/**
 * aim_gettlv8 - Retrieve the Nth TLV in chain as a 8bit integer.
 * @list: Source TLV chain
 * @type: TLV type to search for
 * @nth: Index of TLV to return
 *
 * Same as aim_gettlv(), except that the return value is a
 * 8bit integer instead of an aim_tlv_t.
 *
 */
guint8 aim_gettlv8(aim_tlvlist_t *list, const guint16 t, const int n)
{
	aim_tlv_t *tlv;

	if (!(tlv = aim_gettlv(list, t, n))) {
		return 0; /* erm */
	}
	return aimutil_get8(tlv->value);
}

/**
 * aim_gettlv16 - Retrieve the Nth TLV in chain as a 16bit integer.
 * @list: Source TLV chain
 * @type: TLV type to search for
 * @nth: Index of TLV to return
 *
 * Same as aim_gettlv(), except that the return value is a
 * 16bit integer instead of an aim_tlv_t.
 *
 */
guint16 aim_gettlv16(aim_tlvlist_t *list, const guint16 t, const int n)
{
	aim_tlv_t *tlv;

	if (!(tlv = aim_gettlv(list, t, n))) {
		return 0; /* erm */
	}
	return aimutil_get16(tlv->value);
}

/**
 * aim_gettlv32 - Retrieve the Nth TLV in chain as a 32bit integer.
 * @list: Source TLV chain
 * @type: TLV type to search for
 * @nth: Index of TLV to return
 *
 * Same as aim_gettlv(), except that the return value is a
 * 32bit integer instead of an aim_tlv_t.
 *
 */
guint32 aim_gettlv32(aim_tlvlist_t *list, const guint16 t, const int n)
{
	aim_tlv_t *tlv;

	if (!(tlv = aim_gettlv(list, t, n))) {
		return 0; /* erm */
	}
	return aimutil_get32(tlv->value);
}

#if 0
/**
 * aim_puttlv_8 - Write a one-byte TLV.
 * @buf: Destination buffer
 * @t: TLV type
 * @v: Value
 *
 * Writes a TLV with a one-byte integer value portion.
 *
 */
int aim_puttlv_8(guint8 *buf, const guint16 t, const guint8 v)
{
	guint8 v8[1];

	aimutil_put8(v8, v);

	return aim_puttlv_raw(buf, t, 1, v8);
}

/**
 * aim_puttlv_16 - Write a two-byte TLV.
 * @buf: Destination buffer
 * @t: TLV type
 * @v: Value
 *
 * Writes a TLV with a two-byte integer value portion.
 *
 */
int aim_puttlv_16(guint8 *buf, const guint16 t, const guint16 v)
{
	guint8 v16[2];

	aimutil_put16(v16, v);

	return aim_puttlv_raw(buf, t, 2, v16);
}


/**
 * aim_puttlv_32 - Write a four-byte TLV.
 * @buf: Destination buffer
 * @t: TLV type
 * @v: Value
 *
 * Writes a TLV with a four-byte integer value portion.
 *
 */
int aim_puttlv_32(guint8 *buf, const guint16 t, const guint32 v)
{
	guint8 v32[4];

	aimutil_put32(v32, v);

	return aim_puttlv_raw(buf, t, 4, v32);
}

/**
 * aim_puttlv_raw - Write a raw TLV.
 * @buf: Destination buffer
 * @t: TLV type
 * @l: Length of string
 * @v: String to write
 *
 * Writes a TLV with a raw value portion.  (Only the first @l
 * bytes of the passed buffer will be written, which should not
 * include a terminating NULL.)
 *
 */
int aim_puttlv_raw(guint8 *buf, const guint16 t, const guint16 l, const guint8 *v)
{
	int i;

	i = aimutil_put16(buf, t);
	i += aimutil_put16(buf + i, l);
	if (l) {
		memcpy(buf + i, v, l);
	}
	i += l;

	return i;
}
#endif

