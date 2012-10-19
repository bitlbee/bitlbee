/*
 * aim_internal.h -- prototypes/structs for the guts of libfaim
 *
 */

#ifndef __AIM_INTERNAL_H__
#define __AIM_INTERNAL_H__ 1

typedef struct {
	guint16 family;
	guint16 subtype;
	guint16 flags;
	guint32 id;
} aim_modsnac_t;

#define AIM_MODULENAME_MAXLEN 16
#define AIM_MODFLAG_MULTIFAMILY 0x0001
typedef struct aim_module_s {
	guint16 family;
	guint16 version;
	guint16 toolid;
	guint16 toolversion;
	guint16 flags;
	char name[AIM_MODULENAME_MAXLEN+1];
	int (*snachandler)(aim_session_t *sess, struct aim_module_s *mod, aim_frame_t *rx, aim_modsnac_t *snac, aim_bstream_t *bs);
	void (*shutdown)(aim_session_t *sess, struct aim_module_s *mod);
	void *priv;
	struct aim_module_s *next;
} aim_module_t;

int aim__registermodule(aim_session_t *sess, int (*modfirst)(aim_session_t *, aim_module_t *));
void aim__shutdownmodules(aim_session_t *sess);
aim_module_t *aim__findmodulebygroup(aim_session_t *sess, guint16 group);

int buddylist_modfirst(aim_session_t *sess, aim_module_t *mod);
int admin_modfirst(aim_session_t *sess, aim_module_t *mod);
int bos_modfirst(aim_session_t *sess, aim_module_t *mod);
int search_modfirst(aim_session_t *sess, aim_module_t *mod);
int stats_modfirst(aim_session_t *sess, aim_module_t *mod);
int auth_modfirst(aim_session_t *sess, aim_module_t *mod);
int msg_modfirst(aim_session_t *sess, aim_module_t *mod);
int misc_modfirst(aim_session_t *sess, aim_module_t *mod);
int chatnav_modfirst(aim_session_t *sess, aim_module_t *mod);
int chat_modfirst(aim_session_t *sess, aim_module_t *mod);
int locate_modfirst(aim_session_t *sess, aim_module_t *mod);
int general_modfirst(aim_session_t *sess, aim_module_t *mod);
int ssi_modfirst(aim_session_t *sess, aim_module_t *mod);
int icq_modfirst(aim_session_t *sess, aim_module_t *mod);

int aim_genericreq_n(aim_session_t *, aim_conn_t *conn, guint16 family, guint16 subtype);
int aim_genericreq_n_snacid(aim_session_t *, aim_conn_t *conn, guint16 family, guint16 subtype);
int aim_genericreq_l(aim_session_t *, aim_conn_t *conn, guint16 family, guint16 subtype, guint32 *);
int aim_genericreq_s(aim_session_t *, aim_conn_t *conn, guint16 family, guint16 subtype, guint16 *);

#define AIMBS_CURPOSPAIR(x) ((x)->data + (x)->offset), ((x)->len - (x)->offset)

void aim_rxqueue_cleanbyconn(aim_session_t *sess, aim_conn_t *conn);
int aim_recv(int fd, void *buf, size_t count);
int aim_bstream_init(aim_bstream_t *bs, guint8 *data, int len);
int aim_bstream_empty(aim_bstream_t *bs);
int aim_bstream_curpos(aim_bstream_t *bs);
int aim_bstream_setpos(aim_bstream_t *bs, int off);
void aim_bstream_rewind(aim_bstream_t *bs);
int aim_bstream_advance(aim_bstream_t *bs, int n);
guint8 aimbs_get8(aim_bstream_t *bs);
guint16 aimbs_get16(aim_bstream_t *bs);
guint32 aimbs_get32(aim_bstream_t *bs);
guint8 aimbs_getle8(aim_bstream_t *bs);
guint16 aimbs_getle16(aim_bstream_t *bs);
guint32 aimbs_getle32(aim_bstream_t *bs);
int aimbs_put8(aim_bstream_t *bs, guint8 v);
int aimbs_put16(aim_bstream_t *bs, guint16 v);
int aimbs_put32(aim_bstream_t *bs, guint32 v);
int aimbs_putle8(aim_bstream_t *bs, guint8 v);
int aimbs_putle16(aim_bstream_t *bs, guint16 v);
int aimbs_putle32(aim_bstream_t *bs, guint32 v);
int aimbs_getrawbuf(aim_bstream_t *bs, guint8 *buf, int len);
guint8 *aimbs_getraw(aim_bstream_t *bs, int len);
char *aimbs_getstr(aim_bstream_t *bs, int len);
int aimbs_putraw(aim_bstream_t *bs, const guint8 *v, int len);
int aimbs_putbs(aim_bstream_t *bs, aim_bstream_t *srcbs, int len);

int aim_get_command_rendezvous(aim_session_t *sess, aim_conn_t *conn);

int aim_tx_sendframe(aim_session_t *sess, aim_frame_t *cur);
flap_seqnum_t aim_get_next_txseqnum(aim_conn_t *);
aim_frame_t *aim_tx_new(aim_session_t *sess, aim_conn_t *conn, guint8 framing, guint8 chan, int datalen);
void aim_frame_destroy(aim_frame_t *);
int aim_tx_enqueue(aim_session_t *, aim_frame_t *);
int aim_tx_printqueue(aim_session_t *);
void aim_tx_cleanqueue(aim_session_t *, aim_conn_t *);

aim_rxcallback_t aim_callhandler(aim_session_t *sess, aim_conn_t *conn, u_short family, u_short type);

/*
 * Generic SNAC structure.  Rarely if ever used.
 */
typedef struct aim_snac_s {
	aim_snacid_t id;
	guint16 family;
	guint16 type;
	guint16 flags;
	void *data;
	time_t issuetime;
	struct aim_snac_s *next;
} aim_snac_t;

void aim_initsnachash(aim_session_t *sess);
aim_snacid_t aim_cachesnac(aim_session_t *sess, const guint16 family, const guint16 type, const guint16 flags, const void *data, const int datalen);
aim_snac_t *aim_remsnac(aim_session_t *, aim_snacid_t id);
void aim_cleansnacs(aim_session_t *, int maxage);
int aim_putsnac(aim_bstream_t *, guint16 family, guint16 type, guint16 flags, aim_snacid_t id);

int aim_oft_buildheader(unsigned char *,struct aim_fileheader_t *);

int aim_parse_unknown(aim_session_t *, aim_frame_t *, ...);

/* Stored in ->priv of the service request SNAC for chats. */
struct chatsnacinfo {
	guint16 exchange;
	char name[128];
	guint16 instance;
};

/* these are used by aim_*_clientready */
#define AIM_TOOL_JAVA   0x0001
#define AIM_TOOL_MAC    0x0002
#define AIM_TOOL_WIN16  0x0003
#define AIM_TOOL_WIN32  0x0004
#define AIM_TOOL_MAC68K 0x0005
#define AIM_TOOL_MACPPC 0x0006
#define AIM_TOOL_NEWWIN 0x0010
struct aim_tool_version {
	guint16 group;
	guint16 version;
	guint16 tool;
	guint16 toolversion;
};

/* 
 * In SNACland, the terms 'family' and 'group' are synonymous -- the former
 * is my term, the latter is AOL's.
 */
struct snacgroup {
	guint16 group;
	struct snacgroup *next;
};

struct snacpair {
	guint16 group;
	guint16 subtype;
	struct snacpair *next;
};

struct rateclass {
	guint16 classid;
	guint32 windowsize;
	guint32 clear;
	guint32 alert;
	guint32 limit;
	guint32 disconnect;
	guint32 current;
	guint32 max;
	guint8 unknown[5]; /* only present in versions >= 3 */
	struct snacpair *members;
	struct rateclass *next;
};

/*
 * This is inside every connection.  But it is a void * to anything
 * outside of libfaim.  It should remain that way.  It's called data
 * abstraction.  Maybe you've heard of it.  (Probably not if you're a 
 * libfaim user.)
 * 
 */
typedef struct aim_conn_inside_s {
	struct snacgroup *groups;
	struct rateclass *rates;
} aim_conn_inside_t;

void aim_conn_addgroup(aim_conn_t *conn, guint16 group);

guint32 aim_getcap(aim_session_t *sess, aim_bstream_t *bs, int len);
int aim_putcap(aim_bstream_t *bs, guint32 caps);

int aim_cachecookie(aim_session_t *sess, aim_msgcookie_t *cookie);
aim_msgcookie_t *aim_uncachecookie(aim_session_t *sess, guint8 *cookie, int type);
aim_msgcookie_t *aim_mkcookie(guint8 *, int, void *);
aim_msgcookie_t *aim_checkcookie(aim_session_t *sess, const unsigned char *, const int);
int aim_freecookie(aim_session_t *sess, aim_msgcookie_t *cookie);
int aim_cookie_free(aim_session_t *sess, aim_msgcookie_t *cookie);

int aim_extractuserinfo(aim_session_t *sess, aim_bstream_t *bs, aim_userinfo_t *);

int aim_chat_readroominfo(aim_bstream_t *bs, struct aim_chat_roominfo *outinfo);

void aim_conn_close_rend(aim_session_t *sess, aim_conn_t *conn);
void aim_conn_kill_rend(aim_session_t *sess, aim_conn_t *conn);

void aim_conn_kill_chat(aim_session_t *sess, aim_conn_t *conn);

/* These are all handled internally now. */
int aim_setversions(aim_session_t *sess, aim_conn_t *conn);
int aim_reqrates(aim_session_t *, aim_conn_t *);
int aim_rates_addparam(aim_session_t *, aim_conn_t *);

#endif /* __AIM_INTERNAL_H__ */
