/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Jabber module - Main file                                                *
*                                                                           *
*  Copyright 2006-2013 Wilmer van der Gaast <wilmer@gaast.net>              *
*                                                                           *
*  This program is free software; you can redistribute it and/or modify     *
*  it under the terms of the GNU General Public License as published by     *
*  the Free Software Foundation; either version 2 of the License, or        *
*  (at your option) any later version.                                      *
*                                                                           *
*  This program is distributed in the hope that it will be useful,          *
*  but WITHOUT ANY WARRANTY; without even the implied warranty of           *
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
*  GNU General Public License for more details.                             *
*                                                                           *
*  You should have received a copy of the GNU General Public License along  *
*  with this program; if not, write to the Free Software Foundation, Inc.,  *
*  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.              *
*                                                                           *
\***************************************************************************/

#ifndef _JABBER_H
#define _JABBER_H

#include <glib.h>

#include "bitlbee.h"
#include "md5.h"
#include "xmltree.h"

extern GSList *jabber_connections;

typedef enum {
	JFLAG_STREAM_STARTED = 1,       /* Set when we detected the beginning of the stream
	                                   and want to do auth. */
	JFLAG_AUTHENTICATED = 2,        /* Set when we're successfully authenticatd. */
	JFLAG_STREAM_RESTART = 4,       /* Set when we want to restart the stream (after
	                                   SASL or TLS). */
	JFLAG_WANT_SESSION = 8,         /* Set if the server wants a <session/> tag
	                                   before we continue. */
	JFLAG_WANT_BIND = 16,           /* ... for <bind> tag. */
	JFLAG_WANT_TYPING = 32,         /* Set if we ever sent a typing notification, this
	                                   activates all XEP-85 related code. */
	JFLAG_XMLCONSOLE = 64,          /* If the user added an xmlconsole buddy. */
	JFLAG_STARTTLS_DONE = 128,      /* If a plaintext session was converted to TLS. */

	JFLAG_GTALK =  0x100000,        /* Is Google Talk, as confirmed by iq discovery */
	JFLAG_HIPCHAT = 0x200000,       /* Is hipchat, because prpl->name says so */

	JFLAG_SASL_FB = 0x10000,        /* Trying Facebook authentication. */
} jabber_flags_t;

typedef enum {
	JBFLAG_PROBED_XEP85 = 1,        /* Set this when we sent our probe packet to make
	                                   sure it gets sent only once. */
	JBFLAG_DOES_XEP85 = 2,          /* Set this when the resource seems to support
	                                   XEP85 (typing notification shite). */
	JBFLAG_IS_CHATROOM = 4,         /* It's convenient to use this JID thingy for
	                                   groupchat state info too. */
	JBFLAG_IS_ANONYMOUS = 8,        /* For anonymous chatrooms, when we don't have
	                                   have a real JID. */
	JBFLAG_HIDE_SUBJECT = 16,       /* Hide the subject field since we probably
	                                   showed it already. */
} jabber_buddy_flags_t;

/* Stores a streamhost's (a.k.a. proxy) data */
typedef struct {
	char *jid;
	char *host;
	char port[6];
} jabber_streamhost_t;

typedef enum {
	JCFLAG_MESSAGE_SENT = 1,        /* Set this after sending the first message, so
	                                   we can detect echoes/backlogs. */
} jabber_chat_flags_t;

struct jabber_data {
	struct im_connection *ic;

	int fd;
	void *ssl;
	char *txq;
	int tx_len;
	int r_inpa, w_inpa;

	struct xt_parser *xt;
	jabber_flags_t flags;

	char *username;         /* USERNAME@server */
	char *server;           /* username@SERVER -=> server/domain, not hostname */
	char *me;               /* bare jid */
	char *internal_jid;

	const struct oauth2_service *oauth2_service;
	char *oauth2_access_token;

	/* After changing one of these two (or the priority setting), call
	   presence_send_update() to inform the server about the changes. */
	const struct jabber_away_state *away_state;
	char *away_message;

	md5_state_t cached_id_prefix;
	GHashTable *node_cache;
	GHashTable *buddies;

	GSList *filetransfers;
	GSList *streamhosts;
	int have_streamhosts;
};

struct jabber_away_state {
	char code[5];
	char *full_name;
};

typedef xt_status (*jabber_cache_event) (struct im_connection *ic, struct xt_node *node, struct xt_node *orig);

struct jabber_cache_entry {
	time_t saved_at;
	struct xt_node *node;
	jabber_cache_event func;
};

/* Somewhat messy data structure: We have a hash table with the bare JID as
   the key and the head of a struct jabber_buddy list as the value. The head
   is always a bare JID. If the JID has other resources (often the case,
   except for some transports that don't support multiple resources), those
   follow. In that case, the bare JID at the beginning doesn't actually
   refer to a real session and should only be used for operations that
   support incomplete JIDs. */
struct jabber_buddy {
	char *bare_jid;
	char *full_jid;
	char *resource;

	char *ext_jid; /* The JID to use in BitlBee. The real JID if possible, */
	               /* otherwise something similar to the conference JID. */

	int priority;
	struct jabber_away_state *away_state;
	char *away_message;
	GSList *features;

	time_t last_msg;
	jabber_buddy_flags_t flags;

	struct jabber_buddy *next;
};

struct jabber_chat {
	int flags;
	char *name;
	char *my_full_jid; /* Separate copy because of case sensitivity. */
	struct jabber_buddy *me;
	char *invite;
};

struct jabber_transfer {
	/* bitlbee's handle for this transfer */
	file_transfer_t *ft;

	/* the stream's private handle */
	gpointer streamhandle;

	/* timeout for discover queries */
	gint disco_timeout;
	gint disco_timeout_fired;

	struct im_connection *ic;

	struct jabber_buddy *bud;

	int watch_in;
	int watch_out;

	char *ini_jid;
	char *tgt_jid;
	char *iq_id;
	char *sid;
	int accepted;

	size_t bytesread, byteswritten;
	int fd;
	struct sockaddr_storage saddr;
};

#define JABBER_XMLCONSOLE_HANDLE "_xmlconsole"
#define JABBER_OAUTH_HANDLE "jabber_oauth"

/* Prefixes to use for packet IDs (mainly for IQ packets ATM). Usually the
   first one should be used, but when storing a packet in the cache, a
   "special" kind of ID is assigned to make it easier later to figure out
   if we have to do call an event handler for the response packet. Also
   we'll append a hash to make sure we won't trigger on cached packets from
   other BitlBee users. :-) */
#define JABBER_PACKET_ID "BeeP"
#define JABBER_CACHED_ID "BeeC"

/* The number of seconds to keep cached packets before garbage collecting
   them. This gc is done on every keepalive (every minute). */
#define JABBER_CACHE_MAX_AGE 600

/* RFC 392[01] stuff */
#define XMLNS_TLS          "urn:ietf:params:xml:ns:xmpp-tls"
#define XMLNS_SASL         "urn:ietf:params:xml:ns:xmpp-sasl"
#define XMLNS_BIND         "urn:ietf:params:xml:ns:xmpp-bind"
#define XMLNS_SESSION      "urn:ietf:params:xml:ns:xmpp-session"
#define XMLNS_STANZA_ERROR "urn:ietf:params:xml:ns:xmpp-stanzas"
#define XMLNS_STREAM_ERROR "urn:ietf:params:xml:ns:xmpp-streams"
#define XMLNS_ROSTER       "jabber:iq:roster"

/* Some supported extensions/legacy stuff */
#define XMLNS_AUTH         "jabber:iq:auth"                                      /* XEP-0078 */
#define XMLNS_VERSION      "jabber:iq:version"                                   /* XEP-0092 */
#define XMLNS_TIME_OLD     "jabber:iq:time"                                      /* XEP-0090 */
#define XMLNS_TIME         "urn:xmpp:time"                                       /* XEP-0202 */
#define XMLNS_PING         "urn:xmpp:ping"                                       /* XEP-0199 */
#define XMLNS_RECEIPTS     "urn:xmpp:receipts"                                   /* XEP-0184 */
#define XMLNS_VCARD        "vcard-temp"                                          /* XEP-0054 */
#define XMLNS_DELAY_OLD    "jabber:x:delay"                                      /* XEP-0091 */
#define XMLNS_DELAY        "urn:xmpp:delay"                                      /* XEP-0203 */
#define XMLNS_XDATA        "jabber:x:data"                                       /* XEP-0004 */
#define XMLNS_CHATSTATES   "http://jabber.org/protocol/chatstates"               /* XEP-0085 */
#define XMLNS_DISCO_INFO   "http://jabber.org/protocol/disco#info"               /* XEP-0030 */
#define XMLNS_DISCO_ITEMS  "http://jabber.org/protocol/disco#items"              /* XEP-0030 */
#define XMLNS_MUC          "http://jabber.org/protocol/muc"                      /* XEP-0045 */
#define XMLNS_MUC_USER     "http://jabber.org/protocol/muc#user"                 /* XEP-0045 */
#define XMLNS_CAPS         "http://jabber.org/protocol/caps"                     /* XEP-0115 */
#define XMLNS_FEATURE      "http://jabber.org/protocol/feature-neg"              /* XEP-0020 */
#define XMLNS_SI           "http://jabber.org/protocol/si"                       /* XEP-0095 */
#define XMLNS_FILETRANSFER "http://jabber.org/protocol/si/profile/file-transfer" /* XEP-0096 */
#define XMLNS_BYTESTREAMS  "http://jabber.org/protocol/bytestreams"              /* XEP-0065 */
#define XMLNS_IBB          "http://jabber.org/protocol/ibb"                      /* XEP-0047 */

/* Hipchat protocol extensions*/
#define XMLNS_HIPCHAT         "http://hipchat.com"
#define XMLNS_HIPCHAT_PROFILE "http://hipchat.com/protocol/profile"

/* jabber.c */
void jabber_connect(struct im_connection *ic);

/* iq.c */
xt_status jabber_pkt_iq(struct xt_node *node, gpointer data);
int jabber_init_iq_auth(struct im_connection *ic);
xt_status jabber_pkt_bind_sess(struct im_connection *ic, struct xt_node *node, struct xt_node *orig);
int jabber_get_roster(struct im_connection *ic);
int jabber_get_vcard(struct im_connection *ic, char *bare_jid);
int jabber_add_to_roster(struct im_connection *ic, const char *handle, const char *name, const char *group);
int jabber_remove_from_roster(struct im_connection *ic, char *handle);
xt_status jabber_iq_query_features(struct im_connection *ic, char *bare_jid);
xt_status jabber_iq_query_server(struct im_connection *ic, char *jid, char *xmlns);
void jabber_iq_version_send(struct im_connection *ic, struct jabber_buddy *bud, void *data);
int jabber_iq_disco_server(struct im_connection *ic);

/* si.c */
int jabber_si_handle_request(struct im_connection *ic, struct xt_node *node, struct xt_node *sinode);
void jabber_si_transfer_request(struct im_connection *ic, file_transfer_t *ft, char *who);
void jabber_si_free_transfer(file_transfer_t *ft);

/* s5bytestream.c */
int jabber_bs_recv_request(struct im_connection *ic, struct xt_node *node, struct xt_node *qnode);
gboolean jabber_bs_send_start(struct jabber_transfer *tf);
gboolean jabber_bs_send_write(file_transfer_t *ft, char *buffer, unsigned int len);

/* message.c */
xt_status jabber_pkt_message(struct xt_node *node, gpointer data);

/* presence.c */
xt_status jabber_pkt_presence(struct xt_node *node, gpointer data);
int presence_send_update(struct im_connection *ic);
int presence_send_request(struct im_connection *ic, char *handle, char *request);

/* jabber_util.c */
char *set_eval_priority(set_t *set, char *value);
char *set_eval_tls(set_t *set, char *value);
struct xt_node *jabber_make_packet(char *name, char *type, char *to, struct xt_node *children);
struct xt_node *jabber_make_error_packet(struct xt_node *orig, char *err_cond, char *err_type, char *err_code);
void jabber_cache_add(struct im_connection *ic, struct xt_node *node, jabber_cache_event func);
struct xt_node *jabber_cache_get(struct im_connection *ic, char *id);
void jabber_cache_entry_free(gpointer entry);
void jabber_cache_clean(struct im_connection *ic);
xt_status jabber_cache_handle_packet(struct im_connection *ic, struct xt_node *node);
const struct jabber_away_state *jabber_away_state_by_code(char *code);
const struct jabber_away_state *jabber_away_state_by_name(char *name);
void jabber_buddy_ask(struct im_connection *ic, char *handle);
int jabber_compare_jid(const char *jid1, const char *jid2);
char *jabber_normalize(const char *orig);

typedef enum {
	GET_BUDDY_CREAT = 1,    /* Try to create it, if necessary. */
	GET_BUDDY_EXACT = 2,    /* Get an exact match (only makes sense with bare JIDs). */
	GET_BUDDY_FIRST = 4,    /* No selection, simply get the first resource for this JID. */
	GET_BUDDY_BARE = 8,     /* Get the bare version of the JID (possibly inexistent). */
	GET_BUDDY_BARE_OK = 16, /* Allow returning a bare JID if that seems better. */
} get_buddy_flags_t;

struct jabber_error {
	char *code, *text, *type;
};

struct jabber_buddy *jabber_buddy_add(struct im_connection *ic, char *full_jid);
struct jabber_buddy *jabber_buddy_by_jid(struct im_connection *ic, char *jid, get_buddy_flags_t flags);
struct jabber_buddy *jabber_buddy_by_ext_jid(struct im_connection *ic, char *jid, get_buddy_flags_t flags);
int jabber_buddy_remove(struct im_connection *ic, char *full_jid);
int jabber_buddy_remove_bare(struct im_connection *ic, char *bare_jid);
void jabber_buddy_remove_all(struct im_connection *ic);
time_t jabber_get_timestamp(struct xt_node *xt);
struct jabber_error *jabber_error_parse(struct xt_node *node, char *xmlns);
void jabber_error_free(struct jabber_error *err);
gboolean jabber_set_me(struct im_connection *ic, const char *me);

extern const struct jabber_away_state jabber_away_state_list[];

/* io.c */
int jabber_write_packet(struct im_connection *ic, struct xt_node *node);
int jabber_write(struct im_connection *ic, char *buf, int len);
gboolean jabber_connected_plain(gpointer data, gint source, b_input_condition cond);
gboolean jabber_connected_ssl(gpointer data, int returncode, void *source, b_input_condition cond);
gboolean jabber_start_stream(struct im_connection *ic);
void jabber_end_stream(struct im_connection *ic);

/* sasl.c */
xt_status sasl_pkt_mechanisms(struct xt_node *node, gpointer data);
xt_status sasl_pkt_challenge(struct xt_node *node, gpointer data);
xt_status sasl_pkt_result(struct xt_node *node, gpointer data);
gboolean sasl_supported(struct im_connection *ic);
void sasl_oauth2_init(struct im_connection *ic);
int sasl_oauth2_get_refresh_token(struct im_connection *ic, const char *msg);
int sasl_oauth2_refresh(struct im_connection *ic, const char *refresh_token);

extern const struct oauth2_service oauth2_service_google;
extern const struct oauth2_service oauth2_service_facebook;

/* conference.c */
struct groupchat *jabber_chat_join(struct im_connection *ic, const char *room, const char *nick, const char *password);
struct groupchat *jabber_chat_with(struct im_connection *ic, char *who);
struct groupchat *jabber_chat_by_jid(struct im_connection *ic, const char *name);
void jabber_chat_free(struct groupchat *c);
int jabber_chat_msg(struct groupchat *ic, char *message, int flags);
int jabber_chat_topic(struct groupchat *c, char *topic);
int jabber_chat_leave(struct groupchat *c, const char *reason);
void jabber_chat_pkt_presence(struct im_connection *ic, struct jabber_buddy *bud, struct xt_node *node);
void jabber_chat_pkt_message(struct im_connection *ic, struct jabber_buddy *bud, struct xt_node *node);
void jabber_chat_invite(struct groupchat *c, char *who, char *message);

/* hipchat.c */
int jabber_get_hipchat_profile(struct im_connection *ic);
xt_status jabber_parse_hipchat_profile(struct im_connection *ic, struct xt_node *node, struct xt_node *orig);
xt_status hipchat_handle_success(struct im_connection *ic, struct xt_node *node);

#endif
