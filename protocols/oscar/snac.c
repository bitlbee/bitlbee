/*
 *
 * Various SNAC-related dodads... 
 *
 * outstanding_snacs is a list of aim_snac_t structs.  A SNAC should be added
 * whenever a new SNAC is sent and it should remain in the list until the
 * response for it has been receieved.  
 *
 * cleansnacs() should be called periodically by the client in order
 * to facilitate the aging out of unreplied-to SNACs. This can and does
 * happen, so it should be handled.
 *
 */

#include <aim.h>

/*
 * Called from aim_session_init() to initialize the hash.
 */
void aim_initsnachash(aim_session_t *sess)
{
	int i;

	for (i = 0; i < AIM_SNAC_HASH_SIZE; i++)
		sess->snac_hash[i] = NULL;

	return;
}

aim_snacid_t aim_cachesnac(aim_session_t *sess, const guint16 family, const guint16 type, const guint16 flags, const void *data, const int datalen)
{
	aim_snac_t snac;

	snac.id = sess->snacid_next++;
	snac.family = family;
	snac.type = type;
	snac.flags = flags;

	if (datalen) {
		if (!(snac.data = g_malloc(datalen)))
			return 0; /* er... */
		memcpy(snac.data, data, datalen);
	} else
		snac.data = NULL;

	return aim_newsnac(sess, &snac);
}

/*
 * Clones the passed snac structure and caches it in the
 * list/hash.
 */
aim_snacid_t aim_newsnac(aim_session_t *sess, aim_snac_t *newsnac)
{
	aim_snac_t *snac;
	int index;

	if (!newsnac)
		return 0;

	if (!(snac = g_malloc(sizeof(aim_snac_t))))
		return 0;
	memcpy(snac, newsnac, sizeof(aim_snac_t));
	snac->issuetime = time(NULL);

	index = snac->id % AIM_SNAC_HASH_SIZE;

	snac->next = (aim_snac_t *)sess->snac_hash[index];
	sess->snac_hash[index] = (void *)snac;

	return snac->id;
}

/*
 * Finds a snac structure with the passed SNAC ID, 
 * removes it from the list/hash, and returns a pointer to it.
 *
 * The returned structure must be freed by the caller.
 *
 */
aim_snac_t *aim_remsnac(aim_session_t *sess, aim_snacid_t id) 
{
	aim_snac_t *cur, **prev;
	int index;

	index = id % AIM_SNAC_HASH_SIZE;

	for (prev = (aim_snac_t **)&sess->snac_hash[index]; (cur = *prev); ) {
		if (cur->id == id) {
			*prev = cur->next;
			return cur;
		} else
			prev = &cur->next;
	}

	return cur;
}

/*
 * This is for cleaning up old SNACs that either don't get replies or
 * a reply was never received for.  Garabage collection. Plain and simple.
 *
 * maxage is the _minimum_ age in seconds to keep SNACs.
 *
 */
void aim_cleansnacs(aim_session_t *sess, int maxage)
{
	int i;

	for (i = 0; i < AIM_SNAC_HASH_SIZE; i++) {
		aim_snac_t *cur, **prev;
		time_t curtime;

		if (!sess->snac_hash[i])
			continue;

		curtime = time(NULL); /* done here in case we waited for the lock */

		for (prev = (aim_snac_t **)&sess->snac_hash[i]; (cur = *prev); ) {
			if ((curtime - cur->issuetime) > maxage) {

				*prev = cur->next;

				/* XXX should we have destructors here? */
				g_free(cur->data);
				g_free(cur);

			} else
				prev = &cur->next;
		}
	}

	return;
}

int aim_putsnac(aim_bstream_t *bs, guint16 family, guint16 subtype, guint16 flags, aim_snacid_t snacid)
{

	aimbs_put16(bs, family);
	aimbs_put16(bs, subtype);
	aimbs_put16(bs, flags);
	aimbs_put32(bs, snacid);

	return 10;
}
