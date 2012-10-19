#ifndef __OSCAR_IM_H__
#define __OSCAR_IM_H__

#define AIM_CB_FAM_MSG 0x0004

/*
 * SNAC Family: Messaging Services.
 */ 
#define AIM_CB_MSG_ERROR 0x0001
#define AIM_CB_MSG_PARAMINFO 0x0005
#define AIM_CB_MSG_INCOMING 0x0007
#define AIM_CB_MSG_EVIL 0x0009
#define AIM_CB_MSG_MISSEDCALL 0x000a
#define AIM_CB_MSG_CLIENTAUTORESP 0x000b
#define AIM_CB_MSG_ACK 0x000c
#define AIM_CB_MSG_MTN 0x0014
#define AIM_CB_MSG_DEFAULT 0xffff

#define AIM_IMFLAGS_AWAY		0x0001 /* mark as an autoreply */
#define AIM_IMFLAGS_ACK			0x0002 /* request a receipt notice */
#define AIM_IMFLAGS_UNICODE		0x0004
#define AIM_IMFLAGS_ISO_8859_1		0x0008
#define AIM_IMFLAGS_BUDDYREQ		0x0010 /* buddy icon requested */
#define AIM_IMFLAGS_HASICON		0x0020 /* already has icon */
#define AIM_IMFLAGS_SUBENC_MACINTOSH	0x0040 /* damn that Steve Jobs! */
#define AIM_IMFLAGS_CUSTOMFEATURES 	0x0080 /* features field present */
#define AIM_IMFLAGS_EXTDATA		0x0100
#define AIM_IMFLAGS_CUSTOMCHARSET	0x0200 /* charset fields set */
#define AIM_IMFLAGS_MULTIPART		0x0400 /* ->mpmsg section valid */
#define AIM_IMFLAGS_OFFLINE		0x0800 /* send to offline user */

/*
 * Multipart message structures.
 */
typedef struct aim_mpmsg_section_s {
	guint16 charset;
	guint16 charsubset;
	guint8 *data;
	guint16 datalen;
	struct aim_mpmsg_section_s *next;
} aim_mpmsg_section_t;

typedef struct aim_mpmsg_s {
	int numparts;
	aim_mpmsg_section_t *parts;
} aim_mpmsg_t;

int aim_mpmsg_init(aim_session_t *sess, aim_mpmsg_t *mpm);
void aim_mpmsg_free(aim_session_t *sess, aim_mpmsg_t *mpm);

/*
 * Arguments to aim_send_im_ext().
 *
 * This is really complicated.  But immensely versatile.
 *
 */
struct aim_sendimext_args {

	/* These are _required_ */
	const char *destsn;
	guint32 flags; /* often 0 */

	/* Only required if not using multipart messages */
	const char *msg;
	int msglen;

	/* Required if ->msg is not provided */
	aim_mpmsg_t *mpmsg;

	/* Only used if AIM_IMFLAGS_HASICON is set */
	guint32 iconlen;
	time_t iconstamp;
	guint32 iconsum;

	/* Only used if AIM_IMFLAGS_CUSTOMFEATURES is set */
	guint8 *features;
	guint8 featureslen;

	/* Only used if AIM_IMFLAGS_CUSTOMCHARSET is set and mpmsg not used */
	guint16 charset;
	guint16 charsubset;
};

/*
 * This information is provided in the Incoming ICBM callback for
 * Channel 1 ICBM's.  
 *
 * Note that although CUSTOMFEATURES and CUSTOMCHARSET say they
 * are optional, both are always set by the current libfaim code.
 * That may or may not change in the future.  It is mainly for
 * consistency with aim_sendimext_args.
 *
 * Multipart messages require some explanation. If you want to use them,
 * I suggest you read all the comments in im.c.
 *
 */
struct aim_incomingim_ch1_args {

	/* Always provided */
	aim_mpmsg_t mpmsg;
	guint32 icbmflags; /* some flags apply only to ->msg, not all mpmsg */
	
	/* Only provided if message has a human-readable section */
	char *msg;
	int msglen;

	/* Only provided if AIM_IMFLAGS_HASICON is set */
	time_t iconstamp;
	guint32 iconlen;
	guint16 iconsum;

	/* Only provided if AIM_IMFLAGS_CUSTOMFEATURES is set */
	guint8 *features;
	guint8 featureslen;

	/* Only provided if AIM_IMFLAGS_EXTDATA is set */
	guint8 extdatalen;
	guint8 *extdata;

	/* Only used if AIM_IMFLAGS_CUSTOMCHARSET is set */
	guint16 charset;
	guint16 charsubset;
};

/* Valid values for channel 2 args->status */
#define AIM_RENDEZVOUS_PROPOSE 0x0000
#define AIM_RENDEZVOUS_CANCEL  0x0001
#define AIM_RENDEZVOUS_ACCEPT  0x0002

struct aim_incomingim_ch2_args {
	guint8 cookie[8];
	guint16 reqclass;
	guint16 status;
	guint16 errorcode;
	const char *clientip;
	const char *clientip2;
	const char *verifiedip;
	guint16 port;
	const char *msg; /* invite message or file description */
	const char *encoding;
	const char *language;
	union {
		struct {
			guint32 checksum;
			guint32 length;
			time_t timestamp;
			guint8 *icon;
		} icon;
		struct {
			struct aim_chat_roominfo roominfo;
		} chat;
		struct {
			guint32 fgcolor;
			guint32 bgcolor;
			const char *rtfmsg;
		} rtfmsg;
	} info;
	void *destructor; /* used internally only */
};

/* Valid values for channel 4 args->type */
#define AIM_ICQMSG_AUTHREQUEST 0x0006
#define AIM_ICQMSG_AUTHDENIED 0x0007
#define AIM_ICQMSG_AUTHGRANTED 0x0008

struct aim_incomingim_ch4_args {
	guint32 uin; /* Of the sender of the ICBM */
	guint16 type;
	char *msg; /* Reason for auth request, deny, or accept */
};

int aim_send_im_ext(aim_session_t *sess, struct aim_sendimext_args *args);
int aim_send_im(aim_session_t *, const char *destsn, unsigned short flags, const char *msg);
int aim_send_typing(aim_session_t *sess, aim_conn_t *conn, int typing);
int aim_send_im_direct(aim_session_t *, aim_conn_t *, const char *msg, int len);
const char *aim_directim_getsn(aim_conn_t *conn);
aim_conn_t *aim_directim_initiate(aim_session_t *, const char *destsn);
aim_conn_t *aim_directim_connect(aim_session_t *, const char *sn, const char *addr, const guint8 *cookie);

int aim_im_sendmtn(aim_session_t *sess, guint16 type1, const char *sn, guint16 type2);
int aim_send_im_ch2_statusmessage(aim_session_t *sess, const char *sender, const guint8 *cookie, const char *message, const guint8 state, const guint16 dc);

#endif /* __OSCAR_IM_H__ */
