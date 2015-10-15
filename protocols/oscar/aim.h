/*
 * Main libfaim header.  Must be included in client for prototypes/macros.
 *
 * "come on, i turned a chick lesbian; i think this is the hackish equivalent"
 *                                                -- Josh Meyer
 *
 */

#ifndef __AIM_H__
#define __AIM_H__

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <time.h>
#include <gmodule.h>

#include "bitlbee.h"

/* XXX adjust these based on autoconf-detected platform */
typedef guint32 aim_snacid_t;
typedef guint16 flap_seqnum_t;

/* Portability stuff (DMP) */

#if defined(mach) && defined(__APPLE__)
#define gethostbyname(x) gethostbyname2(x, AF_INET)
#endif

/*
 * Current Maximum Length for Screen Names (not including NULL)
 *
 * Currently only names up to 16 characters can be registered
 * however it is aparently legal for them to be larger.
 */
#define MAXSNLEN 32

/*
 * Current Maximum Length for Instant Messages
 *
 * This was found basically by experiment, but not wholly
 * accurate experiment.  It should not be regarded
 * as completely correct.  But its a decent approximation.
 *
 * Note that although we can send this much, its impossible
 * for WinAIM clients (up through the latest (4.0.1957)) to
 * send any more than 1kb.  Amaze all your windows friends
 * with utterly oversized instant messages!
 *
 * XXX: the real limit is the total SNAC size at 8192. Fix this.
 *
 */
#define MAXMSGLEN 7987

/*
 * Maximum size of a Buddy Icon.
 */
#define MAXICONLEN 7168
#define AIM_ICONIDENT "AVT1picture.id"

/*
 * Current Maximum Length for Chat Room Messages
 *
 * This is actually defined by the protocol to be
 * dynamic, but I have yet to see due cause to
 * define it dynamically here.  Maybe later.
 *
 */
#define MAXCHATMSGLEN 512

/*
 * Standard size of an AIM authorization cookie
 */
#define AIM_COOKIELEN            0x100

#define AIM_MD5_STRING "AOL Instant Messenger (SM)"

/*
 * Default Authorizer server name and TCP port for the OSCAR farm.
 *
 * You shouldn't need to change this unless you're writing
 * your own server.
 *
 * Note that only one server is needed to start the whole
 * AIM process.  The later server addresses come from
 * the authorizer service.
 *
 * This is only here for convenience.  Its still up to
 * the client to connect to it.
 *
 */
#define AIM_DEFAULT_LOGIN_SERVER_AIM "login.messaging.aol.com"
#define AIM_DEFAULT_LOGIN_SERVER_ICQ "login.icq.com"
#define AIM_LOGIN_PORT 5190

/*
 * Size of the SNAC caching hash.
 *
 * Default: 16
 *
 */
#define AIM_SNAC_HASH_SIZE 16

/*
 * Client info.  Filled in by the client and passed in to
 * aim_send_login().  The information ends up getting passed to OSCAR
 * through the initial login command.
 *
 */
struct client_info_s {
	const char *clientstring;
	guint16 clientid;
	int major;
	int minor;
	int point;
	int build;
	const char *country; /* two-letter abbrev */
	const char *lang; /* two-letter abbrev */
};

#define AIM_CLIENTINFO_KNOWNGOOD_3_5_1670 { \
		"AOL Instant Messenger (SM), version 3.5.1670/WIN32", \
		0x0004, \
		0x0003, \
		0x0005, \
		0x0000, \
		0x0686, \
		"us", \
		"en", \
}

#define AIM_CLIENTINFO_KNOWNGOOD_4_1_2010 { \
		"AOL Instant Messenger (SM), version 4.1.2010/WIN32", \
		0x0004, \
		0x0004, \
		0x0001, \
		0x0000, \
		0x07da, \
		"us", \
		"en", \
}

#define AIM_CLIENTINFO_KNOWNGOOD_5_1_3036 { \
		"AOL Instant Messenger, version 5.1.3036/WIN32", \
		0x0109, \
		0x0005, \
		0x0001, \
		0x0000, \
		0x0bdc, \
		"us", \
		"en", \
}

/*
 * I would make 4.1.2010 the default, but they seem to have found
 * an alternate way of breaking that one.
 *
 * 3.5.1670 should work fine, however, you will be subjected to the
 * memory test, which may require you to have a WinAIM binary laying
 * around. (see login.c::memrequest())
 */
#define AIM_CLIENTINFO_KNOWNGOOD AIM_CLIENTINFO_KNOWNGOOD_5_1_3036

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

/*
 * These could be arbitrary, but its easier to use the actual AIM values
 */
#define AIM_CONN_TYPE_AUTH          0x0007
#define AIM_CONN_TYPE_ADS           0x0005
#define AIM_CONN_TYPE_BOS           0x0002
#define AIM_CONN_TYPE_CHAT          0x000e
#define AIM_CONN_TYPE_CHATNAV       0x000d

/*
 * Status values returned from aim_conn_new().  ORed together.
 */
#define AIM_CONN_STATUS_READY       0x0001
#define AIM_CONN_STATUS_INTERNALERR 0x0002
#define AIM_CONN_STATUS_RESOLVERR   0x0040
#define AIM_CONN_STATUS_CONNERR     0x0080
#define AIM_CONN_STATUS_INPROGRESS  0x0100

#define AIM_FRAMETYPE_FLAP 0x0000

/*
 * message type flags
 */
#define AIM_MTYPE_PLAIN     0x01
#define AIM_MTYPE_CHAT      0x02
#define AIM_MTYPE_FILEREQ   0x03
#define AIM_MTYPE_URL       0x04
#define AIM_MTYPE_AUTHREQ   0x06
#define AIM_MTYPE_AUTHDENY  0x07
#define AIM_MTYPE_AUTHOK    0x08
#define AIM_MTYPE_SERVER    0x09
#define AIM_MTYPE_ADDED     0x0C
#define AIM_MTYPE_WWP       0x0D
#define AIM_MTYPE_EEXPRESS  0x0E
#define AIM_MTYPE_CONTACTS  0x13
#define AIM_MTYPE_PLUGIN    0x1A
#define AIM_MTYPE_AUTOAWAY      0xE8
#define AIM_MTYPE_AUTOBUSY      0xE9
#define AIM_MTYPE_AUTONA        0xEA
#define AIM_MTYPE_AUTODND       0xEB
#define AIM_MTYPE_AUTOFFC       0xEC

typedef struct aim_conn_s {
	int fd;
	guint16 type;
	guint16 subtype;
	flap_seqnum_t seqnum;
	guint32 status;
	void *priv; /* misc data the client may want to store */
	void *internal; /* internal conn-specific libfaim data */
	time_t lastactivity; /* time of last transmit */
	int forcedlatency;
	void *handlerlist;
	void *sessv; /* pointer to parent session */
	void *inside; /* only accessible from inside libfaim */
	struct aim_conn_s *next;
} aim_conn_t;

/*
 * Byte Stream type. Sort of.
 *
 * Use of this type serves a couple purposes:
 *   - Buffer/buflen pairs are passed all around everywhere. This turns
 *     that into one value, as well as abstracting it slightly.
 *   - Through the abstraction, it is possible to enable bounds checking
 *     for robustness at the cost of performance.  But a clean failure on
 *     weird packets is much better than a segfault.
 *   - I like having variables named "bs".
 *
 * Don't touch the insides of this struct.  Or I'll have to kill you.
 *
 */
typedef struct aim_bstream_s {
	guint8 *data;
	guint32 len;
	guint32 offset;
} aim_bstream_t;

typedef struct aim_frame_s {
	guint8 hdrtype; /* defines which piece of the union to use */
	union {
		struct {
			guint8 type;
			flap_seqnum_t seqnum;
		} flap;
	} hdr;
	aim_bstream_t data;     /* payload stream */
	guint8 handled;         /* 0 = new, !0 = been handled */
	guint8 nofree;          /* 0 = free data on purge, 1 = only unlink */
	aim_conn_t *conn;  /* the connection it came in on... */
	struct aim_frame_s *next;
} aim_frame_t;

typedef struct aim_msgcookie_s {
	unsigned char cookie[8];
	int type;
	void *data;
	time_t addtime;
	struct aim_msgcookie_s *next;
} aim_msgcookie_t;

/*
 * AIM Session: The main client-data interface.
 *
 */
typedef struct aim_session_s {

	/* ---- Client Accessible ------------------------ */

	/* Our screen name. */
	char sn[MAXSNLEN + 1];

	/*
	 * Pointer to anything the client wants to
	 * explicitly associate with this session.
	 *
	 * This is for use in the callbacks mainly. In any
	 * callback, you can access this with sess->aux_data.
	 *
	 */
	void *aux_data;

	/* ---- Internal Use Only ------------------------ */

	/* Server-stored information (ssi) */
	struct {
		int received_data;
		guint16 revision;
		struct aim_ssi_item *items;
		time_t timestamp;
		int waiting_for_ack;
		aim_frame_t *holding_queue;
	} ssi;

	/* Connection information */
	aim_conn_t *connlist;

	/*
	 * Transmit/receive queues.
	 *
	 * These are only used when you don't use your own lowlevel
	 * I/O.  I don't suggest that you use libfaim's internal I/O.
	 * Its really bad and the API/event model is quirky at best.
	 *
	 */
	aim_frame_t *queue_outgoing;
	aim_frame_t *queue_incoming;

	/*
	 * Tx Enqueuing function.
	 *
	 * This is how you override the transmit direction of libfaim's
	 * internal I/O.  This function will be called whenever it needs
	 * to send something.
	 *
	 */
	int (*tx_enqueue)(struct aim_session_s *, aim_frame_t *);

	/*
	 * Outstanding snac handling
	 *
	 * XXX: Should these be per-connection? -mid
	 */
	void *snac_hash[AIM_SNAC_HASH_SIZE];
	aim_snacid_t snacid_next;

	struct aim_icq_info *icq_info;
	struct aim_oft_info *oft_info;
	struct aim_authresp_info *authinfo;
	struct aim_emailinfo *emailinfo;

	struct {
		struct aim_userinfo_s *userinfo;
		struct userinfo_node *torequest;
		struct userinfo_node *requested;
		int waiting_for_response;
	} locate;

	guint32 flags; /* AIM_SESS_FLAGS_ */

	aim_msgcookie_t *msgcookies;

	void *modlistv;

	guint8 aim_icq_state;  /* ICQ representation of away state */
} aim_session_t;

/* Values for sess->flags */
#define AIM_SESS_FLAGS_SNACLOGIN         0x00000001
#define AIM_SESS_FLAGS_XORLOGIN          0x00000002
#define AIM_SESS_FLAGS_NONBLOCKCONNECT   0x00000004
#define AIM_SESS_FLAGS_DONTTIMEOUTONICBM 0x00000008

/* Valid for calling aim_icq_setstatus() and for aim_userinfo_t->icqinfo.status */
#define AIM_ICQ_STATE_NORMAL    0x00000000
#define AIM_ICQ_STATE_AWAY      0x00000001
#define AIM_ICQ_STATE_DND       0x00000002
#define AIM_ICQ_STATE_OUT       0x00000004
#define AIM_ICQ_STATE_BUSY      0x00000010
#define AIM_ICQ_STATE_CHAT      0x00000020
#define AIM_ICQ_STATE_INVISIBLE 0x00000100
#define AIM_ICQ_STATE_WEBAWARE  0x00010000
#define AIM_ICQ_STATE_HIDEIP            0x00020000
#define AIM_ICQ_STATE_BIRTHDAY          0x00080000
#define AIM_ICQ_STATE_DIRECTDISABLED    0x00100000
#define AIM_ICQ_STATE_ICQHOMEPAGE       0x00200000
#define AIM_ICQ_STATE_DIRECTREQUIREAUTH 0x10000000
#define AIM_ICQ_STATE_DIRECTCONTACTLIST 0x20000000

/*
 * AIM User Info, Standard Form.
 */
typedef struct {
	char sn[MAXSNLEN + 1];
	guint16 warnlevel;
	guint16 idletime;
	guint16 flags;
	guint32 membersince;
	guint32 onlinesince;
	guint32 sessionlen;
	guint32 capabilities;
	struct {
		guint32 status;
		guint32 ipaddr;
		guint8 crap[0x25]; /* until we figure it out... */
	} icqinfo;
	guint32 present;
} aim_userinfo_t;

#define AIM_USERINFO_PRESENT_FLAGS        0x00000001
#define AIM_USERINFO_PRESENT_MEMBERSINCE  0x00000002
#define AIM_USERINFO_PRESENT_ONLINESINCE  0x00000004
#define AIM_USERINFO_PRESENT_IDLE         0x00000008
#define AIM_USERINFO_PRESENT_ICQEXTSTATUS 0x00000010
#define AIM_USERINFO_PRESENT_ICQIPADDR    0x00000020
#define AIM_USERINFO_PRESENT_ICQDATA      0x00000040
#define AIM_USERINFO_PRESENT_CAPABILITIES 0x00000080
#define AIM_USERINFO_PRESENT_SESSIONLEN   0x00000100

#define AIM_FLAG_UNCONFIRMED    0x0001 /* "damned transients" */
#define AIM_FLAG_ADMINISTRATOR  0x0002
#define AIM_FLAG_AOL            0x0004
#define AIM_FLAG_OSCAR_PAY      0x0008
#define AIM_FLAG_FREE           0x0010
#define AIM_FLAG_AWAY           0x0020
#define AIM_FLAG_ICQ            0x0040
#define AIM_FLAG_WIRELESS       0x0080
#define AIM_FLAG_UNKNOWN100     0x0100
#define AIM_FLAG_UNKNOWN200     0x0200
#define AIM_FLAG_ACTIVEBUDDY    0x0400
#define AIM_FLAG_UNKNOWN800     0x0800
#define AIM_FLAG_ABINTERNAL     0x1000

#define AIM_FLAG_ALLUSERS       0x001f

/*
 * TLV handling
 */

/* Generic TLV structure. */
typedef struct aim_tlv_s {
	guint16 type;
	guint16 length;
	guint8 *value;
} aim_tlv_t;

/* List of above. */
typedef struct aim_tlvlist_s {
	aim_tlv_t *tlv;
	struct aim_tlvlist_s *next;
} aim_tlvlist_t;

/* TLV-handling functions */

#if 0
/* Very, very raw TLV handling. */
int aim_puttlv_8(guint8 *buf, const guint16 t, const guint8 v);
int aim_puttlv_16(guint8 *buf, const guint16 t, const guint16 v);
int aim_puttlv_32(guint8 *buf, const guint16 t, const guint32 v);
int aim_puttlv_raw(guint8 *buf, const guint16 t, const guint16 l, const guint8 *v);
#endif

/* TLV list handling. */
aim_tlvlist_t *aim_readtlvchain(aim_bstream_t *bs);
void aim_freetlvchain(aim_tlvlist_t **list);
aim_tlv_t *aim_gettlv(aim_tlvlist_t *list, guint16 t, const int n);
char *aim_gettlv_str(aim_tlvlist_t *list, const guint16 t, const int n);
guint8 aim_gettlv8(aim_tlvlist_t *list, const guint16 type, const int num);
guint16 aim_gettlv16(aim_tlvlist_t *list, const guint16 t, const int n);
guint32 aim_gettlv32(aim_tlvlist_t *list, const guint16 t, const int n);
int aim_writetlvchain(aim_bstream_t *bs, aim_tlvlist_t **list);
int aim_addtlvtochain8(aim_tlvlist_t **list, const guint16 t, const guint8 v);
int aim_addtlvtochain16(aim_tlvlist_t **list, const guint16 t, const guint16 v);
int aim_addtlvtochain32(aim_tlvlist_t **list, const guint16 type, const guint32 v);
int aim_addtlvtochain_raw(aim_tlvlist_t **list, const guint16 t, const guint16 l, const guint8 *v);
int aim_addtlvtochain_caps(aim_tlvlist_t **list, const guint16 t, const guint32 caps);
int aim_addtlvtochain_noval(aim_tlvlist_t **list, const guint16 type);
int aim_addtlvtochain_chatroom(aim_tlvlist_t **list, guint16 type, guint16 exchange, const char *roomname,
                               guint16 instance);
int aim_addtlvtochain_userinfo(aim_tlvlist_t **list, guint16 type, aim_userinfo_t *ui);
int aim_addtlvtochain_frozentlvlist(aim_tlvlist_t **list, guint16 type, aim_tlvlist_t **tl);
int aim_counttlvchain(aim_tlvlist_t **list);
int aim_sizetlvchain(aim_tlvlist_t **list);


/*
 * Get command from connections
 *
 * aim_get_commmand() is the libfaim lowlevel I/O in the receive direction.
 * XXX Make this easily overridable.
 *
 */
int aim_get_command(aim_session_t *, aim_conn_t *);

/*
 * Dispatch commands that are in the rx queue.
 */
void aim_rxdispatch(aim_session_t *);

int aim_debugconn_sendconnect(aim_session_t *sess, aim_conn_t *conn);

typedef int (*aim_rxcallback_t)(aim_session_t *, aim_frame_t *, ...);

struct aim_clientrelease {
	char *name;
	guint32 build;
	char *url;
	char *info;
};

struct aim_authresp_info {
	char *sn;
	guint16 errorcode;
	char *errorurl;
	guint16 regstatus;
	char *email;
	char *bosip;
	guint8 *cookie;
	struct aim_clientrelease latestrelease;
	struct aim_clientrelease latestbeta;
};

/* Callback data for redirect. */
struct aim_redirect_data {
	guint16 group;
	const char *ip;
	const guint8 *cookie;
	struct { /* group == AIM_CONN_TYPE_CHAT */
		guint16 exchange;
		const char *room;
		guint16 instance;
	} chat;
};

int aim_clientready(aim_session_t *sess, aim_conn_t *conn);
int aim_sendflapver(aim_session_t *sess, aim_conn_t *conn);
int aim_request_login(aim_session_t *sess, aim_conn_t *conn, const char *sn);
int aim_send_login(aim_session_t *, aim_conn_t *, const char *, const char *, struct client_info_s *, const char *key);
int aim_encode_password_md5(const char *password, const char *key, unsigned char *digest);
void aim_purge_rxqueue(aim_session_t *);

#define AIM_TX_QUEUED    0 /* default */
#define AIM_TX_IMMEDIATE 1
#define AIM_TX_USER      2
int aim_tx_setenqueue(aim_session_t *sess, int what, int (*func)(aim_session_t *, aim_frame_t *));

int aim_tx_flushqueue(aim_session_t *);
void aim_tx_purgequeue(aim_session_t *);

int aim_conn_setlatency(aim_conn_t *conn, int newval);

void aim_conn_kill(aim_session_t *sess, aim_conn_t **deadconn);

int aim_conn_addhandler(aim_session_t *, aim_conn_t *conn, u_short family, u_short type, aim_rxcallback_t newhandler,
                        u_short flags);
int aim_clearhandlers(aim_conn_t *conn);

aim_conn_t *aim_conn_findbygroup(aim_session_t *sess, guint16 group);
aim_session_t *aim_conn_getsess(aim_conn_t *conn);
void aim_conn_close(aim_conn_t *deadconn);
aim_conn_t *aim_newconn(aim_session_t *, int type, const char *dest);
int aim_conngetmaxfd(aim_session_t *);
aim_conn_t *aim_select(aim_session_t *, struct timeval *, int *);
int aim_conn_isready(aim_conn_t *);
int aim_conn_setstatus(aim_conn_t *, int);
int aim_conn_completeconnect(aim_session_t *sess, aim_conn_t *conn);
int aim_conn_isconnecting(aim_conn_t *conn);

typedef void (*faim_debugging_callback_t)(aim_session_t *sess, int level, const char *format, va_list va);
int aim_setdebuggingcb(aim_session_t * sess, faim_debugging_callback_t);
void aim_session_init(aim_session_t *, guint32 flags, int debuglevel);
void aim_session_kill(aim_session_t *);
void aim_setupproxy(aim_session_t *sess, const char *server, const char *username, const char *password);
aim_conn_t *aim_getconn_type(aim_session_t *, int type);
aim_conn_t *aim_getconn_type_all(aim_session_t *, int type);
aim_conn_t *aim_getconn_fd(aim_session_t *, int fd);

/* aim_misc.c */


struct aim_chat_roominfo {
	unsigned short exchange;
	char *name;
	unsigned short instance;
};

struct aim_chat_invitation {
	struct im_connection * ic;
	char * name;
	guint8 exchange;
};

#define AIM_VISIBILITYCHANGE_PERMITADD    0x05
#define AIM_VISIBILITYCHANGE_PERMITREMOVE 0x06
#define AIM_VISIBILITYCHANGE_DENYADD      0x07
#define AIM_VISIBILITYCHANGE_DENYREMOVE   0x08

#define AIM_PRIVFLAGS_ALLOWIDLE           0x01
#define AIM_PRIVFLAGS_ALLOWMEMBERSINCE    0x02

#define AIM_WARN_ANON                     0x01

int aim_flap_nop(aim_session_t *sess, aim_conn_t *conn);
int aim_bos_setprofile(aim_session_t *sess, aim_conn_t *conn, const char *profile, const char *awaymsg, guint32 caps);
int aim_bos_setgroupperm(aim_session_t *, aim_conn_t *, guint32 mask);
int aim_bos_setprivacyflags(aim_session_t *, aim_conn_t *, guint32);
int aim_reqpersonalinfo(aim_session_t *, aim_conn_t *);
int aim_reqservice(aim_session_t *, aim_conn_t *, guint16);
int aim_bos_reqrights(aim_session_t *, aim_conn_t *);
int aim_bos_reqbuddyrights(aim_session_t *, aim_conn_t *);
int aim_bos_reqlocaterights(aim_session_t *, aim_conn_t *);
int aim_setextstatus(aim_session_t *sess, aim_conn_t *conn, guint32 status);

struct aim_fileheader_t *aim_getlisting(aim_session_t *sess, FILE *);

#define AIM_CLIENTTYPE_UNKNOWN  0x0000
#define AIM_CLIENTTYPE_MC       0x0001
#define AIM_CLIENTTYPE_WINAIM   0x0002
#define AIM_CLIENTTYPE_WINAIM41 0x0003
#define AIM_CLIENTTYPE_AOL_TOC  0x0004

#define AIM_RATE_CODE_CHANGE     0x0001
#define AIM_RATE_CODE_WARNING    0x0002
#define AIM_RATE_CODE_LIMIT      0x0003
#define AIM_RATE_CODE_CLEARLIMIT 0x0004
int aim_ads_requestads(aim_session_t *sess, aim_conn_t *conn);

/* aim_im.c */

aim_conn_t *aim_sendfile_initiate(aim_session_t *, const char *destsn, const char *filename, guint16 numfiles,
                                  guint32 totsize);

aim_conn_t *aim_getfile_initiate(aim_session_t *sess, aim_conn_t *conn, const char *destsn);
int aim_oft_getfile_request(aim_session_t *sess, aim_conn_t *conn, const char *name, int size);
int aim_oft_getfile_ack(aim_session_t *sess, aim_conn_t *conn);
int aim_oft_getfile_end(aim_session_t *sess, aim_conn_t *conn);

#define AIM_SENDMEMBLOCK_FLAG_ISREQUEST  0
#define AIM_SENDMEMBLOCK_FLAG_ISHASH     1

#define AIM_GETINFO_GENERALINFO 0x00001
#define AIM_GETINFO_AWAYMESSAGE 0x00003
#define AIM_GETINFO_CAPABILITIES 0x0004

struct aim_invite_priv {
	char *sn;
	char *roomname;
	guint16 exchange;
	guint16 instance;
};

#define AIM_COOKIETYPE_UNKNOWN  0x00
#define AIM_COOKIETYPE_ICBM     0x01
#define AIM_COOKIETYPE_ADS      0x02
#define AIM_COOKIETYPE_BOS      0x03
#define AIM_COOKIETYPE_IM       0x04
#define AIM_COOKIETYPE_CHAT     0x05
#define AIM_COOKIETYPE_CHATNAV  0x06
#define AIM_COOKIETYPE_INVITE   0x07

int aim_handlerendconnect(aim_session_t *sess, aim_conn_t *cur);

#define AIM_TRANSFER_DENY_NOTSUPPORTED 0x0000
#define AIM_TRANSFER_DENY_DECLINE 0x0001
#define AIM_TRANSFER_DENY_NOTACCEPTING 0x0002
aim_conn_t *aim_accepttransfer(aim_session_t *sess, aim_conn_t *conn, const char *sn, const guint8 *cookie,
                               const guint8 *ip, guint16 listingfiles, guint16 listingtotsize, guint16 listingsize,
                               guint32 listingchecksum, guint16 rendid);

int aim_getinfo(aim_session_t *, aim_conn_t *, const char *, unsigned short);

#define AIM_IMPARAM_FLAG_CHANMSGS_ALLOWED       0x00000001
#define AIM_IMPARAM_FLAG_MISSEDCALLS_ENABLED    0x00000002

/* This is what the server will give you if you don't set them yourself. */
#define AIM_IMPARAM_DEFAULTS { \
		0, \
		AIM_IMPARAM_FLAG_CHANMSGS_ALLOWED | AIM_IMPARAM_FLAG_MISSEDCALLS_ENABLED, \
		512, /* !! Note how small this is. */ \
		(99.9) * 10, (99.9) * 10, \
		1000 /* !! And how large this is. */ \
}

/* This is what most AIM versions use. */
#define AIM_IMPARAM_REASONABLE { \
		0, \
		AIM_IMPARAM_FLAG_CHANMSGS_ALLOWED | AIM_IMPARAM_FLAG_MISSEDCALLS_ENABLED, \
		8000, \
		(99.9) * 10, (99.9) * 10, \
		0 \
}


struct aim_icbmparameters {
	guint16 maxchan;
	guint32 flags; /* AIM_IMPARAM_FLAG_ */
	guint16 maxmsglen; /* message size that you will accept */
	guint16 maxsenderwarn; /* this and below are *10 (999=99.9%) */
	guint16 maxrecverwarn;
	guint32 minmsginterval; /* in milliseconds? */
};

int aim_reqicbmparams(aim_session_t *sess);
int aim_seticbmparam(aim_session_t *sess, struct aim_icbmparameters *params);

/* auth.c */
int aim_sendcookie(aim_session_t *, aim_conn_t *, const guint8 *);

int aim_admin_changepasswd(aim_session_t *, aim_conn_t *, const char *newpw, const char *curpw);
int aim_admin_reqconfirm(aim_session_t *sess, aim_conn_t *conn);
int aim_admin_getinfo(aim_session_t *sess, aim_conn_t *conn, guint16 info);
int aim_admin_setemail(aim_session_t *sess, aim_conn_t *conn, const char *newemail);
int aim_admin_setnick(aim_session_t *sess, aim_conn_t *conn, const char *newnick);

/* These apply to exchanges as well. */
#define AIM_CHATROOM_FLAG_EVILABLE 0x0001
#define AIM_CHATROOM_FLAG_NAV_ONLY 0x0002
#define AIM_CHATROOM_FLAG_INSTANCING_ALLOWED 0x0004
#define AIM_CHATROOM_FLAG_OCCUPANT_PEEK_ALLOWED 0x0008

struct aim_chat_exchangeinfo {
	guint16 number;
	guint16 flags;
	char *name;
	char *charset1;
	char *lang1;
	char *charset2;
	char *lang2;
};

#define AIM_CHATFLAGS_NOREFLECT         0x0001
#define AIM_CHATFLAGS_AWAY              0x0002
#define AIM_CHATFLAGS_UNICODE           0x0004
#define AIM_CHATFLAGS_ISO_8859_1        0x0008

int aim_chat_send_im(aim_session_t *sess, aim_conn_t *conn, guint16 flags, const char *msg, int msglen);
int aim_chat_join(aim_session_t *sess, aim_conn_t *conn, guint16 exchange, const char *roomname, guint16 instance);

int aim_chatnav_reqrights(aim_session_t *sess, aim_conn_t *conn);

int aim_chat_invite(aim_session_t *sess, aim_conn_t *conn, const char *sn, const char *msg, guint16 exchange,
                    const char *roomname, guint16 instance);

int aim_chatnav_createroom(aim_session_t *sess, aim_conn_t *conn, const char *name, guint16 exchange);

/* aim_util.c */
/*
 * These are really ugly.  You'd think this was LISP.  I wish it was.
 *
 * XXX With the advent of bstream's, these should be removed to enforce
 * their use.
 *
 */
#define aimutil_put8(buf, data) ((*(buf) = (u_char) (data) & 0xff), 1)
#define aimutil_get8(buf) ((*(buf)) & 0xff)
#define aimutil_put16(buf, data) ( \
	        (*(buf) = (u_char) ((data) >> 8) & 0xff), \
	        (*((buf) + 1) = (u_char) (data) & 0xff),  \
	        2)
#define aimutil_get16(buf) ((((*(buf)) << 8) & 0xff00) + ((*((buf) + 1)) & 0xff))
#define aimutil_put32(buf, data) ( \
	        (*((buf)) = (u_char) ((data) >> 24) & 0xff), \
	        (*((buf) + 1) = (u_char) ((data) >> 16) & 0xff), \
	        (*((buf) + 2) = (u_char) ((data) >> 8) & 0xff), \
	        (*((buf) + 3) = (u_char) (data) & 0xff), \
	        4)
#define aimutil_get32(buf) ((((*(buf)) << 24) & 0xff000000) + \
	                    (((*((buf) + 1)) << 16) & 0x00ff0000) + \
	                    (((*((buf) + 2)) << 8) & 0x0000ff00) + \
	                    (((*((buf) + 3)) & 0x000000ff)))

/* Little-endian versions (damn ICQ) */
#define aimutil_putle8(buf, data) ( \
	        (*(buf) = (unsigned char) (data) & 0xff), \
	        1)
#define aimutil_getle8(buf) ( \
	        (*(buf)) & 0xff \
	        )
#define aimutil_putle16(buf, data) ( \
	        (*((buf) + 0) = (unsigned char) ((data) >> 0) & 0xff),  \
	        (*((buf) + 1) = (unsigned char) ((data) >> 8) & 0xff), \
	        2)
#define aimutil_getle16(buf) ( \
	        (((*((buf) + 0)) << 0) & 0x00ff) + \
	        (((*((buf) + 1)) << 8) & 0xff00) \
	        )
#define aimutil_putle32(buf, data) ( \
	        (*((buf) + 0) = (unsigned char) ((data) >>  0) & 0xff), \
	        (*((buf) + 1) = (unsigned char) ((data) >>  8) & 0xff), \
	        (*((buf) + 2) = (unsigned char) ((data) >> 16) & 0xff), \
	        (*((buf) + 3) = (unsigned char) ((data) >> 24) & 0xff), \
	        4)
#define aimutil_getle32(buf) ( \
	        (((*((buf) + 0)) <<  0) & 0x000000ff) + \
	        (((*((buf) + 1)) <<  8) & 0x0000ff00) + \
	        (((*((buf) + 2)) << 16) & 0x00ff0000) + \
	        (((*((buf) + 3)) << 24) & 0xff000000))


int aim_sncmp(const char *a, const char *b);

#include <aim_internal.h>

/*
 * SNAC Families.
 */
#define AIM_CB_FAM_ACK 0x0000
#define AIM_CB_FAM_GEN 0x0001
#define AIM_CB_FAM_SPECIAL 0xffff /* Internal libfaim use */

/*
 * SNAC Family: Ack.
 *
 * Not really a family, but treating it as one really
 * helps it fit into the libfaim callback structure better.
 *
 */
#define AIM_CB_ACK_ACK 0x0001

/*
 * SNAC Family: General.
 */
#define AIM_CB_GEN_ERROR 0x0001
#define AIM_CB_GEN_CLIENTREADY 0x0002
#define AIM_CB_GEN_SERVERREADY 0x0003
#define AIM_CB_GEN_SERVICEREQ 0x0004
#define AIM_CB_GEN_REDIRECT 0x0005
#define AIM_CB_GEN_RATEINFOREQ 0x0006
#define AIM_CB_GEN_RATEINFO 0x0007
#define AIM_CB_GEN_RATEINFOACK 0x0008
#define AIM_CB_GEN_RATECHANGE 0x000a
#define AIM_CB_GEN_SERVERPAUSE 0x000b
#define AIM_CB_GEN_SERVERRESUME 0x000d
#define AIM_CB_GEN_REQSELFINFO 0x000e
#define AIM_CB_GEN_SELFINFO 0x000f
#define AIM_CB_GEN_EVIL 0x0010
#define AIM_CB_GEN_SETIDLE 0x0011
#define AIM_CB_GEN_MIGRATIONREQ 0x0012
#define AIM_CB_GEN_MOTD 0x0013
#define AIM_CB_GEN_SETPRIVFLAGS 0x0014
#define AIM_CB_GEN_WELLKNOWNURL 0x0015
#define AIM_CB_GEN_NOP 0x0016
#define AIM_CB_GEN_DEFAULT 0xffff

/*
 * SNAC Family: Advertisement Services
 */
#define AIM_CB_ADS_ERROR 0x0001
#define AIM_CB_ADS_DEFAULT 0xffff

/*
 * OFT Services
 *
 * See non-SNAC note below.
 */
#define AIM_CB_OFT_DIRECTIMCONNECTREQ 0x0001 /* connect request -- actually an OSCAR CAP*/
#define AIM_CB_OFT_DIRECTIMINCOMING 0x0002
#define AIM_CB_OFT_DIRECTIMDISCONNECT 0x0003
#define AIM_CB_OFT_DIRECTIMTYPING 0x0004
#define AIM_CB_OFT_DIRECTIMINITIATE 0x0005

#define AIM_CB_OFT_GETFILECONNECTREQ 0x0006 /* connect request -- actually an OSCAR CAP*/
#define AIM_CB_OFT_GETFILELISTINGREQ 0x0007 /* OFT listing.txt request */
#define AIM_CB_OFT_GETFILEFILEREQ 0x0008    /* received file request */
#define AIM_CB_OFT_GETFILEFILESEND 0x0009   /* received file request confirm -- send data */
#define AIM_CB_OFT_GETFILECOMPLETE 0x000a   /* received file send complete*/
#define AIM_CB_OFT_GETFILEINITIATE 0x000b   /* request for file get acknowledge */
#define AIM_CB_OFT_GETFILEDISCONNECT 0x000c   /* OFT connection disconnected.*/
#define AIM_CB_OFT_GETFILELISTING 0x000d   /* OFT listing.txt received.*/
#define AIM_CB_OFT_GETFILERECEIVE 0x000e   /* OFT file incoming.*/
#define AIM_CB_OFT_GETFILELISTINGRXCONFIRM 0x000f
#define AIM_CB_OFT_GETFILESTATE4 0x0010

#define AIM_CB_OFT_SENDFILEDISCONNECT 0x0020   /* OFT connection disconnected.*/



/*
 * SNAC Family: Internal Messages
 *
 * This isn't truly a SNAC family either, but using
 * these, we can integrated non-SNAC services into
 * the SNAC-centered libfaim callback structure.
 *
 */
#define AIM_CB_SPECIAL_AUTHSUCCESS 0x0001
#define AIM_CB_SPECIAL_AUTHOTHER 0x0002
#define AIM_CB_SPECIAL_CONNERR 0x0003
#define AIM_CB_SPECIAL_CONNCOMPLETE 0x0004
#define AIM_CB_SPECIAL_FLAPVER 0x0005
#define AIM_CB_SPECIAL_CONNINITDONE 0x0006
#define AIM_CB_SPECIAL_IMAGETRANSFER 0x007
#define AIM_CB_SPECIAL_UNKNOWN 0xffff
#define AIM_CB_SPECIAL_DEFAULT AIM_CB_SPECIAL_UNKNOWN

#endif /* __AIM_H__ */
