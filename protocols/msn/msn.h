/********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2012 Wilmer van der Gaast and others                *
  \********************************************************************/

/* MSN module                                                           */

/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License with
  the Debian GNU/Linux distribution in /usr/share/common-licenses/GPL;
  if not, write to the Free Software Foundation, Inc., 51 Franklin St.,
  Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef _MSN_H
#define _MSN_H

/* Some hackish magicstrings to make special-purpose messages/switchboards.
 */
#define TYPING_NOTIFICATION_MESSAGE "\r\r\rBEWARE, ME R TYPINK MESSAGE!!!!\r\r\r"
#define NUDGE_MESSAGE "\r\r\rSHAKE THAT THING\r\r\r"
#define GROUPCHAT_SWITCHBOARD_MESSAGE "\r\r\rME WANT TALK TO MANY PEOPLE\r\r\r"

#ifdef DEBUG_MSN
#define debug(text ...) imcb_log(ic, text);
#else
#define debug(text ...)
#endif

/* This should be MSN Messenger 7.0.0813
#define MSNP11_PROD_KEY "CFHUR$52U_{VIX5T"
#define MSNP11_PROD_ID  "PROD0101{0RM?UBW"
*/

#define MSN_NS_HOST "messenger.hotmail.com"
#define MSN_NS_PORT "1863"

/* Some other version.
#define MSNP11_PROD_KEY "O4BG@C7BWLYQX?5G"
#define MSNP11_PROD_ID  "PROD01065C%ZFN6F"
*/

/* <= BitlBee 3.0.5
#define MSNP11_PROD_KEY "ILTXC!4IXB5FB*PX"
#define MSNP11_PROD_ID  "PROD0119GSJUC$18"
*/

#define MSNP11_PROD_KEY "C1BX{V4W}Q3*10SM"
#define MSNP11_PROD_ID  "PROD0120PW!CCV9@"
#define MSNP_VER        "MSNP21"
#define MSNP_BUILD      "14.0.8117.416"

#define MSN_SB_NEW         -24062002

#define MSN_CAP1        0xC000
#define MSN_CAP2        0x0000

#define MSN_BASE_HEADERS \
	"Routing: 1.0\r\n" \
	"To: 1:%s\r\n" \
	"From: 1:%s;epid={%s}\r\n" \
	"\r\n" \
	"Reliability: 1.0\r\n" \
	"\r\n"

#define MSN_MESSAGE_HEADERS MSN_BASE_HEADERS \
	"Messaging: 2.0\r\n" \
	"Message-Type: Text\r\n" \
	"Content-Length: %zd\r\n" \
	"Content-Type: text/plain; charset=UTF-8\r\n" \
	"X-MMS-IM-Format: FN=Segoe%%20UI; EF=; CO=0; CS=0; PF=0\r\n" \
	"\r\n" \
	"%s"

#define MSN_PUT_HEADERS MSN_BASE_HEADERS \
	"Publication: 1.0\r\n" \
	"Uri: %s\r\n" \
	"Content-Type: %s\r\n" \
	"Content-Length: %zd\r\n" \
	"\r\n" \
	"%s"

#define MSN_PUT_USER_BODY \
	"<user>" \
	"<s n=\"PE\"><UserTileLocation></UserTileLocation><FriendlyName>%s</FriendlyName><PSM>%s</PSM><DDP></DDP>" \
	"<Scene></Scene><ASN></ASN><ColorScheme>-3</ColorScheme><BDG></BDG><RUM>%s</RUM><RUL></RUL><RLT>0</RLT>" \
	"<RID></RID><SUL></SUL><MachineGuid>%s</MachineGuid></s>" \
	"<s n=\"IM\"><Status>%s</Status><CurrentMedia></CurrentMedia></s>" \
	"<sep n=\"PD\"><ClientType>1</ClientType><EpName>%s</EpName><Idle>%s</Idle><State>%s</State></sep>" \
	"<sep n=\"PE\"><VER>BitlBee:" BITLBEE_VERSION "</VER><TYP>1</TYP><Capabilities>%d:%d</Capabilities></sep>" \
	"<sep n=\"IM\"><Capabilities>%d:%d</Capabilities></sep>" \
	"</user>"

#define MSN_TYPING_HEADERS "MIME-Version: 1.0\r\n" \
	"Content-Type: text/x-msmsgscontrol\r\n" \
	"TypingUser: %s\r\n" \
	"\r\n\r\n"

#define MSN_NUDGE_HEADERS "MIME-Version: 1.0\r\n" \
	"Content-Type: text/x-msnmsgr-datacast\r\n" \
	"\r\n" \
	"ID: 1\r\n" \
	"\r\n"

#define PROFILE_URL "http://members.msn.com/"

typedef enum {
	MSN_GOT_PROFILE = 1,
	MSN_GOT_PROFILE_DN = 2,
	MSN_DONE_ADL = 4,
	MSN_REAUTHING = 8,
	MSN_EMAIL_UNVERIFIED = 16,
} msn_flags_t;

struct msn_gw {
	char *last_host;
	int port;
	gboolean ssl;

	char *session_id;

	GByteArray *in;
	GByteArray *out;

	int poll_timeout;

	b_event_handler callback;

	struct im_connection *ic;
	struct msn_data *md;

	gboolean open;
	gboolean waiting;
	gboolean polling;
};

struct msn_data {
	int fd, inpa;
	int rxlen;
	char *rxq;

	int msglen;
	char *cmd_text;

	struct im_connection *ic;

	msn_flags_t flags;

	int trId;
	char *tokens[4];
	char *lock_key, *pp_policy;
	char *uuid;

	GSList *msgq, *grpq, *soapq;

	const struct msn_away_state *away_state;
	GSList *groups;
	char *profile_rid;

	/* Mostly used for sending the ADL command; since MSNP13 the client
	   is responsible for downloading the contact list and then sending
	   it to the MSNP server. */
	GTree *domaintree;
	int adl_todo;

	gboolean is_http;
	struct msn_gw *gw;
};

struct msn_away_state {
	char code[4];
	char name[16];
};

struct msn_status_code {
	int number;
	char *text;
	int flags;
};

struct msn_message {
	char *who;
	char *text;
};

struct msn_groupadd {
	char *who;
	char *group;
};

typedef enum {
	MSN_BUDDY_FL = 1,   /* Warning: FL,AL,BL *must* be 1,2,4. */
	MSN_BUDDY_AL = 2,
	MSN_BUDDY_BL = 4,
	MSN_BUDDY_RL = 8,
	MSN_BUDDY_PL = 16,
	MSN_BUDDY_ADL_SYNCED = 256,
	MSN_BUDDY_FED = 512,
} msn_buddy_flags_t;

struct msn_buddy_data {
	char *cid;
	msn_buddy_flags_t flags;
};

struct msn_group {
	char *name;
	char *id;
};

/* Bitfield values for msn_status_code.flags */
#define STATUS_FATAL            1
#define STATUS_SB_FATAL         2

extern int msn_chat_id;
extern const struct msn_away_state msn_away_state_list[];
extern const struct msn_status_code msn_status_code_list[];

/* Keep a list of all the active connections. We need these lists because
   "connected" callbacks might be called when the connection they belong too
   is down already (for example, when an impatient user disabled the
   connection), the callback should check whether it's still listed here
   before doing *anything* else. */
extern GSList *msn_connections;

/* ns.c */
int msn_ns_write(struct im_connection *ic, int fd, const char *fmt, ...) G_GNUC_PRINTF(3, 4);
gboolean msn_ns_connect(struct im_connection *ic, const char *host, int port);
void msn_ns_close(struct msn_data *handler);
void msn_auth_got_passport_token(struct im_connection *ic, const char *token, const char *error);
void msn_auth_got_contact_list(struct im_connection *ic);
int msn_ns_finish_login(struct im_connection *ic);
int msn_ns_sendmessage(struct im_connection *ic, struct bee_user *bu, const char *text);
int msn_ns_command(struct msn_data *md, char **cmd, int num_parts);
int msn_ns_message(struct msn_data *md, char *msg, int msglen, char **cmd, int num_parts);

/* msn_util.c */
int msn_buddy_list_add(struct im_connection *ic, msn_buddy_flags_t list, const char *who, const char *realname_,
                       const char *group);
int msn_buddy_list_remove(struct im_connection *ic, msn_buddy_flags_t list, const char *who, const char *group);
void msn_buddy_ask(bee_user_t *bu);
void msn_queue_feed(struct msn_data *h, char *bytes, int st);
int msn_handler(struct msn_data *h);
char *msn_p11_challenge(char *challenge);
gint msn_domaintree_cmp(gconstpointer a_, gconstpointer b_);
struct msn_group *msn_group_by_name(struct im_connection *ic, const char *name);
struct msn_group *msn_group_by_id(struct im_connection *ic, const char *id);
int msn_ns_set_display_name(struct im_connection *ic, const char *value);
const char *msn_normalize_handle(const char *handle);

/* tables.c */
const struct msn_away_state *msn_away_state_by_number(int number);
const struct msn_away_state *msn_away_state_by_code(char *code);
const struct msn_away_state *msn_away_state_by_name(char *name);
const struct msn_status_code *msn_status_by_number(int number);

/* gw.c */
struct msn_gw *msn_gw_new(struct im_connection *ic);
void msn_gw_free(struct msn_gw *gw);
void msn_gw_open(struct msn_gw *gw);
ssize_t msn_gw_read(struct msn_gw *gw, char **buf);
void msn_gw_write(struct msn_gw *gw, char *buf, size_t len);

#endif //_MSN_H
