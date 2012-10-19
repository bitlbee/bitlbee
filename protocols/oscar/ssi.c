/*
 * Server-Side/Stored Information.
 *
 * Relatively new facility that allows storing of certain types of information,
 * such as a users buddy list, permit/deny list, and permit/deny preferences, 
 * to be stored on the server, so that they can be accessed from any client.
 *
 * We keep a copy of the ssi data in sess->ssi, because the data needs to be 
 * accessed for various reasons.  So all the "aim_ssi_itemlist_bleh" functions 
 * near the top just manage the local data.
 *
 * The SNAC sending and receiving functions are lower down in the file, and 
 * they're simpler.  They are in the order of the subtypes they deal with, 
 * starting with the request rights function (subtype 0x0002), then parse 
 * rights (subtype 0x0003), then--well, you get the idea.
 *
 * This is entirely too complicated.
 * You don't know the half of it.
 *
 * XXX - Test for memory leaks
 * XXX - Better parsing of rights, and use the rights info to limit adds
 *
 */

#include <aim.h>
#include "ssi.h"

/**
 * Locally add a new item to the given item list.
 *
 * @param list A pointer to a pointer to the current list of items.
 * @param parent A pointer to the parent group, or NULL if the item should have no 
 *        parent group (ie. the group ID# should be 0).
 * @param name A null terminated string of the name of the new item, or NULL if the 
 *        item should have no name.
 * @param type The type of the item, 0x0001 for a contact, 0x0002 for a group, etc.
 * @return The newly created item.
 */
static struct aim_ssi_item *aim_ssi_itemlist_add(struct aim_ssi_item **list, struct aim_ssi_item *parent, char *name, guint16 type)
{
	int i;
	struct aim_ssi_item *cur, *newitem;

	if (!(newitem = g_new0(struct aim_ssi_item, 1)))
		return NULL;

	/* Set the name */
	if (name) {
		if (!(newitem->name = (char *)g_malloc((strlen(name)+1)*sizeof(char)))) {
			g_free(newitem);
			return NULL;
		}
		strcpy(newitem->name, name);
	} else
		newitem->name = NULL;

	/* Set the group ID# and the buddy ID# */
	newitem->gid = 0x0000;
	newitem->bid = 0x0000;
	if (type == AIM_SSI_TYPE_GROUP) {
		if (name)
			do {
				newitem->gid += 0x0001;
				for (cur=*list, i=0; ((cur) && (!i)); cur=cur->next)
					if ((cur->gid == newitem->gid) && (cur->gid == newitem->gid))
						i=1;
			} while (i);
	} else {
		if (parent)
			newitem->gid = parent->gid;
		do {
			newitem->bid += 0x0001;
			for (cur=*list, i=0; ((cur) && (!i)); cur=cur->next)
				if ((cur->bid == newitem->bid) && (cur->gid == newitem->gid))
					i=1;
		} while (i);
	}

	/* Set the rest */
	newitem->type = type;
	newitem->data = NULL;
	newitem->next = *list;
	*list = newitem;

	return newitem;
}

/**
 * Locally rebuild the 0x00c8 TLV in the additional data of the given group.
 *
 * @param list A pointer to a pointer to the current list of items.
 * @param parentgroup A pointer to the group who's additional data you want to rebuild.
 * @return Return 0 if no errors, otherwise return the error number.
 */
static int aim_ssi_itemlist_rebuildgroup(struct aim_ssi_item **list, struct aim_ssi_item *parentgroup)
{
	int newlen; //, i;
	struct aim_ssi_item *cur;

	/* Free the old additional data */
	if (parentgroup->data) {
		aim_freetlvchain((aim_tlvlist_t **)&parentgroup->data);
		parentgroup->data = NULL;
	}

	/* Find the length for the new additional data */
	newlen = 0;
	if (parentgroup->gid == 0x0000) {
		for (cur=*list; cur; cur=cur->next)
			if ((cur->gid != 0x0000) && (cur->type == AIM_SSI_TYPE_GROUP))
				newlen += 2;
	} else {
		for (cur=*list; cur; cur=cur->next)
			if ((cur->gid == parentgroup->gid) && (cur->type == AIM_SSI_TYPE_BUDDY))
				newlen += 2;
	}

	/* Rebuild the additional data */
	if (newlen>0) {
		guint8 *newdata;

		if (!(newdata = (guint8 *)g_malloc((newlen)*sizeof(guint8))))
			return -ENOMEM;
		newlen = 0;
		if (parentgroup->gid == 0x0000) {
			for (cur=*list; cur; cur=cur->next)
				if ((cur->gid != 0x0000) && (cur->type == AIM_SSI_TYPE_GROUP))
						newlen += aimutil_put16(newdata+newlen, cur->gid);
		} else {
			for (cur=*list; cur; cur=cur->next)
				if ((cur->gid == parentgroup->gid) && (cur->type == AIM_SSI_TYPE_BUDDY))
						newlen += aimutil_put16(newdata+newlen, cur->bid);
		}
		aim_addtlvtochain_raw((aim_tlvlist_t **)&(parentgroup->data), 0x00c8, newlen, newdata);

		g_free(newdata);
	}

	return 0;
}

/**
 * Locally free all of the stored buddy list information.
 *
 * @param sess The oscar session.
 * @return Return 0 if no errors, otherwise return the error number.
 */
static int aim_ssi_freelist(aim_session_t *sess)
{
	struct aim_ssi_item *cur, *delitem;

	cur = sess->ssi.items;
	while (cur) {
		if (cur->name)  g_free(cur->name);
		if (cur->data)  aim_freetlvchain((aim_tlvlist_t **)&cur->data);
		delitem = cur;
		cur = cur->next;
		g_free(delitem);
	}

	sess->ssi.items = NULL;
	sess->ssi.revision = 0;
	sess->ssi.timestamp = (time_t)0;

	return 0;
}

/**
 * Locally find an item given a group ID# and a buddy ID#.
 *
 * @param list A pointer to the current list of items.
 * @param gid The group ID# of the desired item.
 * @param bid The buddy ID# of the desired item.
 * @return Return a pointer to the item if found, else return NULL;
 */
struct aim_ssi_item *aim_ssi_itemlist_find(struct aim_ssi_item *list, guint16 gid, guint16 bid)
{
	struct aim_ssi_item *cur;
	for (cur=list; cur; cur=cur->next)
		if ((cur->gid == gid) && (cur->bid == bid))
			return cur;
	return NULL;
}

/**
 * Locally find an item given a group name, screen name, and type.  If group name 
 * and screen name are null, then just return the first item of the given type.
 *
 * @param list A pointer to the current list of items.
 * @param gn The group name of the desired item.
 * @param bn The buddy name of the desired item.
 * @param type The type of the desired item.
 * @return Return a pointer to the item if found, else return NULL;
 */
struct aim_ssi_item *aim_ssi_itemlist_finditem(struct aim_ssi_item *list, char *gn, char *sn, guint16 type)
{
	struct aim_ssi_item *cur;
	if (!list)
		return NULL;

	if (gn && sn) { /* For finding buddies in groups */
		for (cur=list; cur; cur=cur->next)
			if ((cur->type == type) && (cur->name) && !(aim_sncmp(cur->name, sn))) {
				struct aim_ssi_item *curg;
				for (curg=list; curg; curg=curg->next)
					if ((curg->type == AIM_SSI_TYPE_GROUP) && (curg->gid == cur->gid) && (curg->name) && !(aim_sncmp(curg->name, gn)))
						return cur;
			}

	} else if (sn) { /* For finding groups, permits, denies, and ignores */
		for (cur=list; cur; cur=cur->next)
			if ((cur->type == type) && (cur->name) && !(aim_sncmp(cur->name, sn)))
				return cur;

	/* For stuff without names--permit deny setting, visibility mask, etc. */
	} else for (cur=list; cur; cur=cur->next) {
		if (cur->type == type)
			return cur;
	}

	return NULL;
}

/**
 * Locally find the parent item of the given buddy name.
 *
 * @param list A pointer to the current list of items.
 * @param bn The buddy name of the desired item.
 * @return Return a pointer to the item if found, else return NULL;
 */
struct aim_ssi_item *aim_ssi_itemlist_findparent(struct aim_ssi_item *list, char *sn)
{
	struct aim_ssi_item *cur, *curg;
	if (!list || !sn)
		return NULL;
	if (!(cur = aim_ssi_itemlist_finditem(list, NULL, sn, AIM_SSI_TYPE_BUDDY)))
		return NULL;
	for (curg=list; curg; curg=curg->next)
		if ((curg->type == AIM_SSI_TYPE_GROUP) && (curg->gid == cur->gid))
			return curg;
	return NULL;
}

/**
 * Locally find the permit/deny setting item, and return the setting.
 *
 * @param list A pointer to the current list of items.
 * @return Return the current SSI permit deny setting, or 0 if no setting was found.
 */
int aim_ssi_getpermdeny(struct aim_ssi_item *list)
{
	struct aim_ssi_item *cur = aim_ssi_itemlist_finditem(list, NULL, NULL, AIM_SSI_TYPE_PDINFO);
	if (cur) {
		aim_tlvlist_t *tlvlist = cur->data;
		if (tlvlist) {
			aim_tlv_t *tlv = aim_gettlv(tlvlist, 0x00ca, 1);
			if (tlv && tlv->value)
				return aimutil_get8(tlv->value);
		}
	}
	return 0;
}

/**
 * Add the given packet to the holding queue.  We totally need to send SSI SNACs one at 
 * a time, so we have a local queue where packets get put before they are sent, and 
 * then we send stuff one at a time, nice and orderly-like.
 *
 * @param sess The oscar session.
 * @param conn The bos connection for this session.
 * @param fr The newly created SNAC that you want to send.
 * @return Return 0 if no errors, otherwise return the error number.
 */
static int aim_ssi_enqueue(aim_session_t *sess, aim_conn_t *conn, aim_frame_t *fr)
{
	aim_frame_t *cur;

	if (!sess || !conn || !fr)
		return -EINVAL;

	fr->next = NULL;
	if (sess->ssi.holding_queue == NULL) {
		sess->ssi.holding_queue = fr;
		if (!sess->ssi.waiting_for_ack)
			aim_ssi_modbegin(sess, conn);
	} else {
		for (cur = sess->ssi.holding_queue; cur->next; cur = cur->next) ;
		cur->next = fr;
	}

	return 0;
}

/**
 * Send the next SNAC from the holding queue.  This is called 
 * automatically when an ack from an add, mod, or del is received.  
 * If the queue is empty, it sends the modend SNAC.
 *
 * @param sess The oscar session.
 * @param conn The bos connection for this session.
 * @return Return 0 if no errors, otherwise return the error number.
 */
static int aim_ssi_dispatch(aim_session_t *sess, aim_conn_t *conn)
{
	aim_frame_t *cur;

	if (!sess || !conn)
		return -EINVAL;

	if (!sess->ssi.waiting_for_ack) {
		if (sess->ssi.holding_queue) {
			sess->ssi.waiting_for_ack = 1;
			cur = sess->ssi.holding_queue->next;
			sess->ssi.holding_queue->next = NULL;
			aim_tx_enqueue(sess, sess->ssi.holding_queue);
			sess->ssi.holding_queue = cur;
		} else
			aim_ssi_modend(sess, conn);
	}

	return 0;
}

/**
 * Add an array of screen names to the given group.
 *
 * @param sess The oscar session.
 * @param conn The bos connection for this session.
 * @param gn The name of the group to which you want to add these names.
 * @param sn An array of null terminated strings of the names you want to add.
 * @param num The number of screen names you are adding (size of the sn array).
 * @param flags 1 - Add with TLV(0x66)
 * @return Return 0 if no errors, otherwise return the error number.
 */
int aim_ssi_addbuddies(aim_session_t *sess, aim_conn_t *conn, char *gn, char **sn, unsigned int num, unsigned int flags)
{
	struct aim_ssi_item *parentgroup, **newitems;
	guint16 i;

	if (!sess || !conn || !gn || !sn || !num)
		return -EINVAL;

	/* Look up the parent group */
	if (!(parentgroup = aim_ssi_itemlist_finditem(sess->ssi.items, NULL, gn, AIM_SSI_TYPE_GROUP))) {
		aim_ssi_addgroups(sess, conn, &gn, 1);
		if (!(parentgroup = aim_ssi_itemlist_finditem(sess->ssi.items, NULL, gn, AIM_SSI_TYPE_GROUP)))
			return -ENOMEM;
	}

	/* Allocate an array of pointers to each of the new items */
	if (!(newitems = g_new0(struct aim_ssi_item *, num)))
		return -ENOMEM;

	/* Add items to the local list, and index them in the array */
	for (i=0; i<num; i++)
		if (!(newitems[i] = aim_ssi_itemlist_add(&sess->ssi.items, parentgroup, sn[i], AIM_SSI_TYPE_BUDDY))) {
			g_free(newitems);
			return -ENOMEM;
		} else if (flags & 1) {
			aim_tlvlist_t *tl = NULL;
			aim_addtlvtochain_noval(&tl, 0x66);
			newitems[i]->data = tl;
		}

	/* Send the add item SNAC */
	if ((i = aim_ssi_addmoddel(sess, conn, newitems, num, AIM_CB_SSI_ADD))) {
		g_free(newitems);
		return -i;
	}

	/* Free the array of pointers to each of the new items */
	g_free(newitems);

	/* Rebuild the additional data in the parent group */
	if ((i = aim_ssi_itemlist_rebuildgroup(&sess->ssi.items, parentgroup)))
		return i;

	/* Send the mod item SNAC */
	if ((i = aim_ssi_addmoddel(sess, conn, &parentgroup, 1, AIM_CB_SSI_MOD )))
		return i;

	/* Begin sending SSI SNACs */
	if (!(i = aim_ssi_dispatch(sess, conn)))
		return i;

	return 0;
}

/**
 * Add the master group (the group containing all groups).  This is called by 
 * aim_ssi_addgroups, if necessary.
 *
 * @param sess The oscar session.
 * @param conn The bos connection for this session.
 * @return Return 0 if no errors, otherwise return the error number.
 */
int aim_ssi_addmastergroup(aim_session_t *sess, aim_conn_t *conn)
{
	struct aim_ssi_item *newitem;

	if (!sess || !conn)
		return -EINVAL;

	/* Add the item to the local list, and keep a pointer to it */
	if (!(newitem = aim_ssi_itemlist_add(&sess->ssi.items, NULL, NULL, AIM_SSI_TYPE_GROUP)))
		return -ENOMEM;

	/* If there are any existing groups (technically there shouldn't be, but */
	/* just in case) then add their group ID#'s to the additional data */
	aim_ssi_itemlist_rebuildgroup(&sess->ssi.items, newitem);

	/* Send the add item SNAC */
	aim_ssi_addmoddel(sess, conn, &newitem, 1, AIM_CB_SSI_ADD);

	/* Begin sending SSI SNACs */
	aim_ssi_dispatch(sess, conn);

	return 0;
}

/**
 * Add an array of groups to the list.
 *
 * @param sess The oscar session.
 * @param conn The bos connection for this session.
 * @param gn An array of null terminated strings of the names you want to add.
 * @param num The number of groups names you are adding (size of the sn array).
 * @return Return 0 if no errors, otherwise return the error number.
 */
int aim_ssi_addgroups(aim_session_t *sess, aim_conn_t *conn, char **gn, unsigned int num)
{
	struct aim_ssi_item *parentgroup, **newitems;
	guint16 i;

	if (!sess || !conn || !gn || !num)
		return -EINVAL;

	/* Look up the parent group */
	if (!(parentgroup = aim_ssi_itemlist_find(sess->ssi.items, 0, 0))) {
		aim_ssi_addmastergroup(sess, conn);
		if (!(parentgroup = aim_ssi_itemlist_find(sess->ssi.items, 0, 0)))
			return -ENOMEM;
	}

	/* Allocate an array of pointers to each of the new items */
	if (!(newitems = g_new0(struct aim_ssi_item *, num)))
		return -ENOMEM;

	/* Add items to the local list, and index them in the array */
	for (i=0; i<num; i++)
		if (!(newitems[i] = aim_ssi_itemlist_add(&sess->ssi.items, parentgroup, gn[i], AIM_SSI_TYPE_GROUP))) {
			g_free(newitems);
			return -ENOMEM;
		}

	/* Send the add item SNAC */
	if ((i = aim_ssi_addmoddel(sess, conn, newitems, num, AIM_CB_SSI_ADD))) {
		g_free(newitems);
		return -i;
	}

	/* Free the array of pointers to each of the new items */
	g_free(newitems);

	/* Rebuild the additional data in the parent group */
	if ((i = aim_ssi_itemlist_rebuildgroup(&sess->ssi.items, parentgroup)))
		return i;

	/* Send the mod item SNAC */
	if ((i = aim_ssi_addmoddel(sess, conn, &parentgroup, 1, AIM_CB_SSI_MOD)))
		return i;

	/* Begin sending SSI SNACs */
	if (!(i = aim_ssi_dispatch(sess, conn)))
		return i;

	return 0;
}

/**
 * Add an array of a certain type of item to the list.  This can be used for 
 * permit buddies, deny buddies, ICQ's ignore buddies, and probably other 
 * types, also.
 *
 * @param sess The oscar session.
 * @param conn The bos connection for this session.
 * @param sn An array of null terminated strings of the names you want to add.
 * @param num The number of groups names you are adding (size of the sn array).
 * @param type The type of item you want to add.  See the AIM_SSI_TYPE_BLEH 
 *        #defines in aim.h.
 * @return Return 0 if no errors, otherwise return the error number.
 */
int aim_ssi_addpord(aim_session_t *sess, aim_conn_t *conn, char **sn, unsigned int num, guint16 type)
{
	struct aim_ssi_item **newitems;
	guint16 i;

	if (!sess || !conn || !sn || !num)
		return -EINVAL;

	/* Allocate an array of pointers to each of the new items */
	if (!(newitems = g_new0(struct aim_ssi_item *, num)))
		return -ENOMEM;

	/* Add items to the local list, and index them in the array */
	for (i=0; i<num; i++)
		if (!(newitems[i] = aim_ssi_itemlist_add(&sess->ssi.items, NULL, sn[i], type))) {
			g_free(newitems);
			return -ENOMEM;
		}

	/* Send the add item SNAC */
	if ((i = aim_ssi_addmoddel(sess, conn, newitems, num, AIM_CB_SSI_ADD))) {
		g_free(newitems);
		return -i;
	}

	/* Free the array of pointers to each of the new items */
	g_free(newitems);

	/* Begin sending SSI SNACs */
	if (!(i = aim_ssi_dispatch(sess, conn)))
		return i;

	return 0;
}

/**
 * Move a buddy from one group to another group.  This basically just deletes the 
 * buddy and re-adds it.
 *
 * @param sess The oscar session.
 * @param conn The bos connection for this session.
 * @param oldgn The group that the buddy is currently in.
 * @param newgn The group that the buddy should be moved in to.
 * @param sn The name of the buddy to be moved.
 * @return Return 0 if no errors, otherwise return the error number.
 */
int aim_ssi_movebuddy(aim_session_t *sess, aim_conn_t *conn, char *oldgn, char *newgn, char *sn)
{
	struct aim_ssi_item **groups, *buddy, *cur;
	guint16 i;

	if (!sess || !conn || !oldgn || !newgn || !sn)
		return -EINVAL;

	/* Look up the buddy */
	if (!(buddy = aim_ssi_itemlist_finditem(sess->ssi.items, NULL, sn, AIM_SSI_TYPE_BUDDY)))
		return -ENOMEM;

	/* Allocate an array of pointers to the two groups */
	if (!(groups = g_new0(struct aim_ssi_item *, 2)))
		return -ENOMEM;

	/* Look up the old parent group */
	if (!(groups[0] = aim_ssi_itemlist_finditem(sess->ssi.items, NULL, oldgn, AIM_SSI_TYPE_GROUP))) {
		g_free(groups);
		return -ENOMEM;
	}

	/* Look up the new parent group */
	if (!(groups[1] = aim_ssi_itemlist_finditem(sess->ssi.items, NULL, newgn, AIM_SSI_TYPE_GROUP))) {
		g_free(groups);
		return -ENOMEM;
	}

	/* Send the delete item SNAC */
	aim_ssi_addmoddel(sess, conn, &buddy, 1, AIM_CB_SSI_DEL);

	/* Put the buddy in the new group */
	buddy->gid = groups[1]->gid;

	/* Assign a new buddy ID#, because the new group might already have a buddy with this ID# */
	buddy->bid = 0;
	do {
		buddy->bid += 0x0001;
		for (cur=sess->ssi.items, i=0; ((cur) && (!i)); cur=cur->next)
			if ((cur->bid == buddy->bid) && (cur->gid == buddy->gid) && (cur->type == AIM_SSI_TYPE_BUDDY) && (cur->name) && aim_sncmp(cur->name, buddy->name))
				i=1;
	} while (i);

	/* Rebuild the additional data in the two parent groups */
	aim_ssi_itemlist_rebuildgroup(&sess->ssi.items, groups[0]);
	aim_ssi_itemlist_rebuildgroup(&sess->ssi.items, groups[1]);

	/* Send the add item SNAC */
	aim_ssi_addmoddel(sess, conn, &buddy, 1, AIM_CB_SSI_ADD);

	/* Send the mod item SNAC */
	aim_ssi_addmoddel(sess, conn, groups, 2, AIM_CB_SSI_MOD);

	/* Free the temporary array */
	g_free(groups);

	/* Begin sending SSI SNACs */
	aim_ssi_dispatch(sess, conn);

	return 0;
}

/**
 * Delete an array of screen names from the given group.
 *
 * @param sess The oscar session.
 * @param conn The bos connection for this session.
 * @param gn The name of the group from which you want to delete these names.
 * @param sn An array of null terminated strings of the names you want to delete.
 * @param num The number of screen names you are deleting (size of the sn array).
 * @return Return 0 if no errors, otherwise return the error number.
 */
int aim_ssi_delbuddies(aim_session_t *sess, aim_conn_t *conn, char *gn, char **sn, unsigned int num)
{
	struct aim_ssi_item *cur, *parentgroup, **delitems;
	int i;

	if (!sess || !conn || !gn || !sn || !num)
		return -EINVAL;

	/* Look up the parent group */
	if (!(parentgroup = aim_ssi_itemlist_finditem(sess->ssi.items, NULL, gn, AIM_SSI_TYPE_GROUP)))
		return -EINVAL;

	/* Allocate an array of pointers to each of the items to be deleted */
	delitems = g_new0(struct aim_ssi_item *, num);

	/* Make the delitems array a pointer to the aim_ssi_item structs to be deleted */
	for (i=0; i<num; i++) {
		if (!(delitems[i] = aim_ssi_itemlist_finditem(sess->ssi.items, NULL, sn[i], AIM_SSI_TYPE_BUDDY))) {
			g_free(delitems);
			return -EINVAL;
		}

		/* Remove the delitems from the item list */
		if (sess->ssi.items == delitems[i]) {
			sess->ssi.items = sess->ssi.items->next;
		} else {
			for (cur=sess->ssi.items; (cur->next && (cur->next!=delitems[i])); cur=cur->next);
			if (cur->next)
				cur->next = cur->next->next;
		}
	}

	/* Send the del item SNAC */
	aim_ssi_addmoddel(sess, conn, delitems, num, AIM_CB_SSI_DEL);

	/* Free the items */
	for (i=0; i<num; i++) {
		if (delitems[i]->name)
			g_free(delitems[i]->name);
		if (delitems[i]->data)
			aim_freetlvchain((aim_tlvlist_t **)&delitems[i]->data);
		g_free(delitems[i]);
	}
	g_free(delitems);

	/* Rebuild the additional data in the parent group */
	aim_ssi_itemlist_rebuildgroup(&sess->ssi.items, parentgroup);

	/* Send the mod item SNAC */
	aim_ssi_addmoddel(sess, conn, &parentgroup, 1, AIM_CB_SSI_MOD);

	/* Delete the group, but only if it's empty */
	if (!parentgroup->data)
		aim_ssi_delgroups(sess, conn, &parentgroup->name, 1);

	/* Begin sending SSI SNACs */
	aim_ssi_dispatch(sess, conn);

	return 0;
}

/**
 * Delete the master group from the item list.  There can be only one.
 * Er, so just find the one master group and delete it.
 *
 * @param sess The oscar session.
 * @param conn The bos connection for this session.
 * @return Return 0 if no errors, otherwise return the error number.
 */
int aim_ssi_delmastergroup(aim_session_t *sess, aim_conn_t *conn)
{
	struct aim_ssi_item *cur, *delitem;

	if (!sess || !conn)
		return -EINVAL;

	/* Make delitem a pointer to the aim_ssi_item to be deleted */
	if (!(delitem = aim_ssi_itemlist_find(sess->ssi.items, 0, 0)))
		return -EINVAL;

	/* Remove delitem from the item list */
	if (sess->ssi.items == delitem) {
		sess->ssi.items = sess->ssi.items->next;
	} else {
		for (cur=sess->ssi.items; (cur->next && (cur->next!=delitem)); cur=cur->next);
		if (cur->next)
			cur->next = cur->next->next;
	}

	/* Send the del item SNAC */
	aim_ssi_addmoddel(sess, conn, &delitem, 1, AIM_CB_SSI_DEL);

	/* Free the item */
	if (delitem->name)
		g_free(delitem->name);
	if (delitem->data)
		aim_freetlvchain((aim_tlvlist_t **)&delitem->data);
	g_free(delitem);

	/* Begin sending SSI SNACs */
	aim_ssi_dispatch(sess, conn);

	return 0;
}

/**
 * Delete an array of groups.
 *
 * @param sess The oscar session.
 * @param conn The bos connection for this session.
 * @param gn An array of null terminated strings of the groups you want to delete.
 * @param num The number of groups you are deleting (size of the gn array).
 * @return Return 0 if no errors, otherwise return the error number.
 */
int aim_ssi_delgroups(aim_session_t *sess, aim_conn_t *conn, char **gn, unsigned int num) {
	struct aim_ssi_item *cur, *parentgroup, **delitems;
	int i;

	if (!sess || !conn || !gn || !num)
		return -EINVAL;

	/* Look up the parent group */
	if (!(parentgroup = aim_ssi_itemlist_find(sess->ssi.items, 0, 0)))
		return -EINVAL;

	/* Allocate an array of pointers to each of the items to be deleted */
	delitems = g_new0(struct aim_ssi_item *, num);

	/* Make the delitems array a pointer to the aim_ssi_item structs to be deleted */
	for (i=0; i<num; i++) {
		if (!(delitems[i] = aim_ssi_itemlist_finditem(sess->ssi.items, NULL, gn[i], AIM_SSI_TYPE_GROUP))) {
			g_free(delitems);
			return -EINVAL;
		}

		/* Remove the delitems from the item list */
		if (sess->ssi.items == delitems[i]) {
			sess->ssi.items = sess->ssi.items->next;
		} else {
			for (cur=sess->ssi.items; (cur->next && (cur->next!=delitems[i])); cur=cur->next);
			if (cur->next)
				cur->next = cur->next->next;
		}
	}

	/* Send the del item SNAC */
	aim_ssi_addmoddel(sess, conn, delitems, num, AIM_CB_SSI_DEL);

	/* Free the items */
	for (i=0; i<num; i++) {
		if (delitems[i]->name)
			g_free(delitems[i]->name);
		if (delitems[i]->data)
			aim_freetlvchain((aim_tlvlist_t **)&delitems[i]->data);
		g_free(delitems[i]);
	}
	g_free(delitems);

	/* Rebuild the additional data in the parent group */
	aim_ssi_itemlist_rebuildgroup(&sess->ssi.items, parentgroup);

	/* Send the mod item SNAC */
	aim_ssi_addmoddel(sess, conn, &parentgroup, 1, AIM_CB_SSI_MOD);

	/* Delete the group, but only if it's empty */
	if (!parentgroup->data)
		aim_ssi_delmastergroup(sess, conn);

	/* Begin sending SSI SNACs */
	aim_ssi_dispatch(sess, conn);

	return 0;
}

/**
 * Delete an array of a certain type of item from the list.  This can be 
 * used for permit buddies, deny buddies, ICQ's ignore buddies, and 
 * probably other types, also.
 *
 * @param sess The oscar session.
 * @param conn The bos connection for this session.
 * @param sn An array of null terminated strings of the items you want to delete.
 * @param num The number of items you are deleting (size of the sn array).
 * @return Return 0 if no errors, otherwise return the error number.
 */
int aim_ssi_delpord(aim_session_t *sess, aim_conn_t *conn, char **sn, unsigned int num, guint16 type) {
	struct aim_ssi_item *cur, **delitems;
	int i;

	if (!sess || !conn || !sn || !num || (type!=AIM_SSI_TYPE_PERMIT && type!=AIM_SSI_TYPE_DENY))
		return -EINVAL;

	/* Allocate an array of pointers to each of the items to be deleted */
	delitems = g_new0(struct aim_ssi_item *, num);

	/* Make the delitems array a pointer to the aim_ssi_item structs to be deleted */
	for (i=0; i<num; i++) {
		if (!(delitems[i] = aim_ssi_itemlist_finditem(sess->ssi.items, NULL, sn[i], type))) {
			g_free(delitems);
			return -EINVAL;
		}

		/* Remove the delitems from the item list */
		if (sess->ssi.items == delitems[i]) {
			sess->ssi.items = sess->ssi.items->next;
		} else {
			for (cur=sess->ssi.items; (cur->next && (cur->next!=delitems[i])); cur=cur->next);
			if (cur->next)
				cur->next = cur->next->next;
		}
	}

	/* Send the del item SNAC */
	aim_ssi_addmoddel(sess, conn, delitems, num, AIM_CB_SSI_DEL);

	/* Free the items */
	for (i=0; i<num; i++) {
		if (delitems[i]->name)
			g_free(delitems[i]->name);
		if (delitems[i]->data)
			aim_freetlvchain((aim_tlvlist_t **)&delitems[i]->data);
		g_free(delitems[i]);
	}
	g_free(delitems);

	/* Begin sending SSI SNACs */
	aim_ssi_dispatch(sess, conn);

	return 0;
}

/*
 * Request SSI Rights.
 */
int aim_ssi_reqrights(aim_session_t *sess, aim_conn_t *conn)
{
	return aim_genericreq_n(sess, conn, AIM_CB_FAM_SSI, AIM_CB_SSI_REQRIGHTS);
}

/*
 * SSI Rights Information.
 */
static int parserights(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{
	int ret = 0;
	aim_rxcallback_t userfunc;

	if ((userfunc = aim_callhandler(sess, rx->conn, snac->family, snac->subtype)))
		ret = userfunc(sess, rx);

	return ret;
}

int aim_ssi_reqalldata(aim_session_t *sess, aim_conn_t *conn)
{
	aim_frame_t *fr;
	aim_snacid_t snacid;

	if (!sess || !conn)
		return -EINVAL;

	if (!(fr = aim_tx_new(sess, conn, AIM_FRAMETYPE_FLAP, 0x02, 10)))
		return -ENOMEM;

	snacid = aim_cachesnac(sess, AIM_CB_FAM_SSI, AIM_CB_SSI_REQFULLLIST, 0x0000, NULL, 0);

	aim_putsnac(&fr->data, AIM_CB_FAM_SSI, AIM_CB_SSI_REQFULLLIST, 0x0000, snacid);

	aim_tx_enqueue(sess, fr);

	return 0;
}

/*
 * SSI Data.
 */
static int parsedata(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{
	int ret = 0;
	aim_rxcallback_t userfunc;
	struct aim_ssi_item *cur = NULL;
	guint8 fmtver; /* guess */
	guint16 revision;
	guint32 timestamp;

	/* When you set the version for the SSI family to 2-4, the beginning of this changes.
	 * Instead of the version and then the revision, there is "0x0006" and then a type 
	 * 0x0001 TLV containing the 2 byte SSI family version that you sent earlier.  Also, 
	 * the SNAC flags go from 0x0000 to 0x8000.  I guess the 0x0006 is the length of the 
	 * TLV(s) that follow.  The rights SNAC does the same thing, with the differing flag 
	 * and everything.
	 */

	fmtver = aimbs_get8(bs); /* Version of ssi data.  Should be 0x00 */
	revision = aimbs_get16(bs); /* # of times ssi data has been modified */
	if (revision != 0)
		sess->ssi.revision = revision;

	for (cur = sess->ssi.items; cur && cur->next; cur=cur->next) ;

	while (aim_bstream_empty(bs) > 4) { /* last four bytes are stamp */
		guint16 namelen, tbslen;

		if (!sess->ssi.items) {
			if (!(sess->ssi.items = g_new0(struct aim_ssi_item, 1)))
				return -ENOMEM;
			cur = sess->ssi.items;
		} else {
			if (!(cur->next = g_new0(struct aim_ssi_item, 1)))
				return -ENOMEM;
			cur = cur->next;
		}

		if ((namelen = aimbs_get16(bs)))
			cur->name = aimbs_getstr(bs, namelen);
		cur->gid = aimbs_get16(bs);
		cur->bid = aimbs_get16(bs);
		cur->type = aimbs_get16(bs);

		if ((tbslen = aimbs_get16(bs))) {
			aim_bstream_t tbs;

			aim_bstream_init(&tbs, bs->data + bs->offset /* XXX */, tbslen);
			cur->data = (void *)aim_readtlvchain(&tbs);
			aim_bstream_advance(bs, tbslen);
		}
	}

	timestamp = aimbs_get32(bs);
	if (timestamp != 0)
		sess->ssi.timestamp = timestamp;
	sess->ssi.received_data = 1;

	if ((userfunc = aim_callhandler(sess, rx->conn, snac->family, snac->subtype)))
		ret = userfunc(sess, rx, fmtver, sess->ssi.revision, sess->ssi.timestamp, sess->ssi.items);

	return ret;
}

/*
 * SSI Data Enable Presence.
 *
 * Should be sent after receiving 13/6 or 13/f to tell the server you
 * are ready to begin using the list.  It will promptly give you the
 * presence information for everyone in your list and put your permit/deny
 * settings into effect.
 * 
 */
int aim_ssi_enable(aim_session_t *sess, aim_conn_t *conn)
{
	return aim_genericreq_n(sess, conn, AIM_CB_FAM_SSI, 0x0007);
}

/*
 * Stuff for SSI authorizations. The code used to work with the old im_ch4
 * messages, but those are supposed to be obsolete. This is probably
 * ICQ-specific.
 */

/**
 * Request authorization to add someone to the server-side buddy list.
 *
 * @param sess The oscar session.
 * @param conn The bos connection for this session.
 * @param uin The contact's ICQ UIN.
 * @param reason The reason string to send with the request.
 * @return Return 0 if no errors, otherwise return the error number.
 */
int aim_ssi_auth_request( aim_session_t *sess, aim_conn_t *conn, char *uin, char *reason )
{
	aim_frame_t *fr;
	aim_snacid_t snacid;
	int snaclen;
	
	snaclen = 10 + 1 + strlen( uin ) + 2 + strlen( reason ) + 2;
	
	if( !( fr = aim_tx_new( sess, conn, AIM_FRAMETYPE_FLAP, 0x02, snaclen ) ) )
		return -ENOMEM;

	snacid = aim_cachesnac( sess, AIM_CB_FAM_SSI, AIM_CB_SSI_SENDAUTHREQ, 0x0000, NULL, 0 );
	aim_putsnac( &fr->data, AIM_CB_FAM_SSI, AIM_CB_SSI_SENDAUTHREQ, 0x0000, snacid );
	
	aimbs_put8( &fr->data, strlen( uin ) );
	aimbs_putraw( &fr->data, (guint8 *)uin, strlen( uin ) );
	aimbs_put16( &fr->data, strlen( reason ) );
	aimbs_putraw( &fr->data, (guint8 *)reason, strlen( reason ) );
	aimbs_put16( &fr->data, 0 );
	
	aim_tx_enqueue( sess, fr );
	
	return( 0 );
}

/**
 * Reply to an authorization request to add someone to the server-side buddy list.
 *
 * @param sess The oscar session.
 * @param conn The bos connection for this session.
 * @param uin The contact's ICQ UIN.
 * @param yesno 1 == Permit, 0 == Deny
 * @param reason The reason string to send with the request.
 * @return Return 0 if no errors, otherwise return the error number.
 */
int aim_ssi_auth_reply( aim_session_t *sess, aim_conn_t *conn, char *uin, int yesno, char *reason )
{
	aim_frame_t *fr;
	aim_snacid_t snacid;
	int snaclen;
	
	snaclen = 10 + 1 + strlen( uin ) + 3 + strlen( reason );
	
	if( !( fr = aim_tx_new( sess, conn, AIM_FRAMETYPE_FLAP, 0x02, snaclen ) ) )
		return -ENOMEM;
	
	snacid = aim_cachesnac( sess, AIM_CB_FAM_SSI, AIM_CB_SSI_SENDAUTHREP, 0x0000, NULL, 0 );
	aim_putsnac( &fr->data, AIM_CB_FAM_SSI, AIM_CB_SSI_SENDAUTHREP, 0x0000, snacid );
	
	aimbs_put8( &fr->data, strlen( uin ) );
	aimbs_putraw( &fr->data, (guint8 *)uin, strlen( uin ) );
	aimbs_put8( &fr->data, yesno );
	aimbs_put16( &fr->data, strlen( reason ) );
	aimbs_putraw( &fr->data, (guint8 *)reason, strlen( reason ) );
	
	aim_tx_enqueue( sess, fr );
	
	return( 0 );
}


/*
 * SSI Add/Mod/Del Item(s).
 *
 * Sends the SNAC to add, modify, or delete an item from the server-stored
 * information.  These 3 SNACs all have an identical structure.  The only
 * difference is the subtype that is set for the SNAC.
 * 
 */
int aim_ssi_addmoddel(aim_session_t *sess, aim_conn_t *conn, struct aim_ssi_item **items, unsigned int num, guint16 subtype)
{
	aim_frame_t *fr;
	aim_snacid_t snacid;
	int i, snaclen, listlen;
	char *list = NULL;

	if (!sess || !conn || !items || !num)
		return -EINVAL;

	snaclen = 10; /* For family, subtype, flags, and SNAC ID */
	listlen = 0;
	for (i=0; i<num; i++) {
		snaclen += 10; /* For length, GID, BID, type, and length */
		if (items[i]->name) {
			snaclen += strlen(items[i]->name);
			
			if (subtype == AIM_CB_SSI_ADD) {
				list = g_realloc(list, listlen + strlen(items[i]->name) + 1);
				strcpy(list + listlen, items[i]->name);
				listlen += strlen(items[i]->name) + 1;
			}
		} else {
			if (subtype == AIM_CB_SSI_ADD) {
				list = g_realloc(list, listlen + 1);
				list[listlen] = '\0';
				listlen ++;
			}
		}
		if (items[i]->data)
			snaclen += aim_sizetlvchain((aim_tlvlist_t **)&items[i]->data);
	}
	
	if (!(fr = aim_tx_new(sess, conn, AIM_FRAMETYPE_FLAP, 0x02, snaclen)))
		return -ENOMEM;

	snacid = aim_cachesnac(sess, AIM_CB_FAM_SSI, subtype, 0x0000, list, list ? listlen : 0);
	aim_putsnac(&fr->data, AIM_CB_FAM_SSI, subtype, 0x0000, snacid);
	
	g_free(list);

	for (i=0; i<num; i++) {
		aimbs_put16(&fr->data, items[i]->name ? strlen(items[i]->name) : 0);
		if (items[i]->name)
			aimbs_putraw(&fr->data, (guint8 *)items[i]->name, strlen(items[i]->name));
		aimbs_put16(&fr->data, items[i]->gid);
		aimbs_put16(&fr->data, items[i]->bid);
		aimbs_put16(&fr->data, items[i]->type);
		aimbs_put16(&fr->data, items[i]->data ? aim_sizetlvchain((aim_tlvlist_t **)&items[i]->data) : 0);
		if (items[i]->data)
			aim_writetlvchain(&fr->data, (aim_tlvlist_t **)&items[i]->data);
	}

	aim_ssi_enqueue(sess, conn, fr);

	return 0;
}

/*
 * SSI Add/Mod/Del Ack.
 *
 * Response to add, modify, or delete SNAC (sent with aim_ssi_addmoddel).
 *
 */
static int parseack(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{
	int ret = 0;
	aim_rxcallback_t userfunc;
	aim_snac_t *origsnac;

	sess->ssi.waiting_for_ack = 0;
	aim_ssi_dispatch(sess, rx->conn);
	
	origsnac = aim_remsnac(sess, snac->id);
	
	if ((userfunc = aim_callhandler(sess, rx->conn, snac->family, snac->subtype)))
		ret = userfunc(sess, rx, origsnac);
	
	if (origsnac) {
		g_free(origsnac->data);
		g_free(origsnac);
	}
	
	return ret;
}

/*
 * SSI Begin Data Modification.
 *
 * Tells the server you're going to start modifying data.
 * 
 */
int aim_ssi_modbegin(aim_session_t *sess, aim_conn_t *conn)
{
	return aim_genericreq_n(sess, conn, AIM_CB_FAM_SSI, AIM_CB_SSI_EDITSTART);
}

/*
 * SSI End Data Modification.
 *
 * Tells the server you're done modifying data.
 *
 */
int aim_ssi_modend(aim_session_t *sess, aim_conn_t *conn)
{
	return aim_genericreq_n(sess, conn, AIM_CB_FAM_SSI, AIM_CB_SSI_EDITSTOP);
}

/*
 * SSI Data Unchanged.
 *
 * Response to aim_ssi_reqdata() if the server-side data is not newer than
 * posted local stamp/revision.
 *
 */
static int parsedataunchanged(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{
	int ret = 0;
	aim_rxcallback_t userfunc;

	sess->ssi.received_data = 1;

	if ((userfunc = aim_callhandler(sess, rx->conn, snac->family, snac->subtype)))
		ret = userfunc(sess, rx);

	return ret;
}

static int snachandler(aim_session_t *sess, aim_module_t *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs)
{

	if (snac->subtype == AIM_CB_SSI_RIGHTSINFO)
		return parserights(sess, mod, rx, snac, bs);
	else if (snac->subtype == AIM_CB_SSI_LIST)
		return parsedata(sess, mod, rx, snac, bs);
	else if (snac->subtype == AIM_CB_SSI_SRVACK)
		return parseack(sess, mod, rx, snac, bs);
	else if (snac->subtype == AIM_CB_SSI_NOLIST)
		return parsedataunchanged(sess, mod, rx, snac, bs);

	return 0;
}

static void ssi_shutdown(aim_session_t *sess, aim_module_t *mod)
{
	aim_ssi_freelist(sess);

	return;
}

int ssi_modfirst(aim_session_t *sess, aim_module_t *mod)
{

	mod->family = AIM_CB_FAM_SSI;
	mod->version = 0x0003;
	mod->toolid = 0x0110;
	mod->toolversion = 0x0629;
	mod->flags = 0;
	strncpy(mod->name, "ssi", sizeof(mod->name));
	mod->snachandler = snachandler;
	mod->shutdown = ssi_shutdown;

	return 0;
}
