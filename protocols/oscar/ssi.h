#ifndef __OSCAR_SSI_H__
#define __OSCAR_SSI_H__

#define AIM_CB_FAM_SSI 0x0013 /* Server stored information */

/*
 * SNAC Family: Server-Stored Buddy Lists
 */
#define AIM_CB_SSI_ERROR 0x0001
#define AIM_CB_SSI_REQRIGHTS 0x0002
#define AIM_CB_SSI_RIGHTSINFO 0x0003
#define AIM_CB_SSI_REQFULLLIST 0x0004
#define AIM_CB_SSI_REQLIST 0x0005
#define AIM_CB_SSI_LIST 0x0006
#define AIM_CB_SSI_ACTIVATE 0x0007
#define AIM_CB_SSI_ADD 0x0008
#define AIM_CB_SSI_MOD 0x0009
#define AIM_CB_SSI_DEL 0x000A
#define AIM_CB_SSI_SRVACK 0x000E
#define AIM_CB_SSI_NOLIST 0x000F
#define AIM_CB_SSI_EDITSTART 0x0011
#define AIM_CB_SSI_EDITSTOP 0x0012
#define AIM_CB_SSI_SENDAUTHREQ 0x0018
#define AIM_CB_SSI_SERVAUTHREQ 0x0019
#define AIM_CB_SSI_SENDAUTHREP 0x001A
#define AIM_CB_SSI_SERVAUTHREP 0x001B


#define AIM_SSI_TYPE_BUDDY         0x0000
#define AIM_SSI_TYPE_GROUP         0x0001
#define AIM_SSI_TYPE_PERMIT        0x0002
#define AIM_SSI_TYPE_DENY          0x0003
#define AIM_SSI_TYPE_PDINFO        0x0004
#define AIM_SSI_TYPE_PRESENCEPREFS 0x0005

struct aim_ssi_item {
	char *name;
	guint16 gid;
	guint16 bid;
	guint16 type;
	void *data;
	struct aim_ssi_item *next;
};

/* These build the actual SNACs and queue them to be sent */
int aim_ssi_reqrights(aim_session_t *sess, aim_conn_t *conn);
int aim_ssi_reqdata(aim_session_t *sess, aim_conn_t *conn, time_t localstamp, guint16 localrev);
int aim_ssi_reqalldata(aim_session_t *sess, aim_conn_t *conn);
int aim_ssi_enable(aim_session_t *sess, aim_conn_t *conn);
int aim_ssi_addmoddel(aim_session_t *sess, aim_conn_t *conn, struct aim_ssi_item **items, unsigned int num, guint16 subtype);
int aim_ssi_modbegin(aim_session_t *sess, aim_conn_t *conn);
int aim_ssi_modend(aim_session_t *sess, aim_conn_t *conn);

/* These handle the local variables */
struct aim_ssi_item *aim_ssi_itemlist_find(struct aim_ssi_item *list, guint16 gid, guint16 bid);
struct aim_ssi_item *aim_ssi_itemlist_finditem(struct aim_ssi_item *list, char *gn, char *sn, guint16 type);
struct aim_ssi_item *aim_ssi_itemlist_findparent(struct aim_ssi_item *list, char *sn);
int aim_ssi_getpermdeny(struct aim_ssi_item *list);
guint32 aim_ssi_getpresence(struct aim_ssi_item *list);

/* Send packets */
int aim_ssi_cleanlist(aim_session_t *sess, aim_conn_t *conn);
int aim_ssi_addbuddies(aim_session_t *sess, aim_conn_t *conn, char *gn, char **sn, unsigned int num, unsigned int flags);
int aim_ssi_addmastergroup(aim_session_t *sess, aim_conn_t *conn);
int aim_ssi_addgroups(aim_session_t *sess, aim_conn_t *conn, char **gn, unsigned int num);
int aim_ssi_addpord(aim_session_t *sess, aim_conn_t *conn, char **sn, unsigned int num, guint16 type);
int aim_ssi_movebuddy(aim_session_t *sess, aim_conn_t *conn, char *oldgn, char *newgn, char *sn);
int aim_ssi_delbuddies(aim_session_t *sess, aim_conn_t *conn, char *gn, char **sn, unsigned int num);
int aim_ssi_delmastergroup(aim_session_t *sess, aim_conn_t *conn);
int aim_ssi_delgroups(aim_session_t *sess, aim_conn_t *conn, char **gn, unsigned int num);
int aim_ssi_deletelist(aim_session_t *sess, aim_conn_t *conn);
int aim_ssi_delpord(aim_session_t *sess, aim_conn_t *conn, char **sn, unsigned int num, guint16 type);
int aim_ssi_setpresence(aim_session_t *sess, aim_conn_t *conn, guint32 presence);
int aim_ssi_auth_request(aim_session_t *sess, aim_conn_t *conn, char *uin, char *reason);
int aim_ssi_auth_reply(aim_session_t *sess, aim_conn_t *conn, char *uin, int yesno, char *reason);

#endif /* __OSCAR_SSI_H__ */
