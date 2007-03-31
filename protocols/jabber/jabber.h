/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Jabber module - Main file                                                *
*                                                                           *
*  Copyright 2006 Wilmer van der Gaast <wilmer@gaast.net>                   *
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

#include "xmltree.h"
#include "bitlbee.h"

typedef enum
{
	JFLAG_STREAM_STARTED = 1,	/* Set when we detected the beginning of the stream
	                                   and want to do auth. */
	JFLAG_AUTHENTICATED = 2,	/* Set when we're successfully authenticatd. */
	JFLAG_STREAM_RESTART = 4,	/* Set when we want to restart the stream (after
	                                   SASL or TLS). */
	JFLAG_WAIT_SESSION = 8,		/* Set if we sent a <session> tag and need a reply
	                                   before we continue. */
	JFLAG_WAIT_BIND = 16,		/* ... for <bind> tag. */
	JFLAG_WANT_TYPING = 32,		/* Set if we ever sent a typing notification, this
	                                   activates all XEP-85 related code. */
} jabber_flags_t;

typedef enum
{
	JBFLAG_PROBED_XEP85 = 1,	/* Set this when we sent our probe packet to make
	                                   sure it gets sent only once. */
	JBFLAG_DOES_XEP85 = 2,		/* Set this when the resource seems to support
	                                   XEP85 (typing notification shite). */
} jabber_buddy_flags_t;

#define JABBER_PORT_DEFAULT "5222"
#define JABBER_PORT_MIN 5220
#define JABBER_PORT_MAX 5229

struct jabber_data
{
	struct im_connection *ic;
	
	int fd;
	void *ssl;
	char *txq;
	int tx_len;
	int r_inpa, w_inpa;
	
	struct xt_parser *xt;
	jabber_flags_t flags;
	
	char *username;		/* USERNAME@server */
	char *server;		/* username@SERVER -=> server/domain, not hostname */
	
	/* After changing one of these two (or the priority setting), call
	   presence_send_update() to inform the server about the changes. */
	struct jabber_away_state *away_state;
	char *away_message;
	
	GHashTable *node_cache;
	GHashTable *buddies;
};

struct jabber_away_state
{
	char code[5];
	char *full_name;
};

typedef xt_status (*jabber_cache_event) ( struct im_connection *ic, struct xt_node *node, struct xt_node *orig );

struct jabber_cache_entry
{
	struct xt_node *node;
	jabber_cache_event func;
};

struct jabber_buddy
{
	char *bare_jid;
	char *full_jid;
	char *resource;
	
	int priority;
	struct jabber_away_state *away_state;
	char *away_message;
	
	time_t last_act;
	jabber_buddy_flags_t flags;
	
	struct jabber_buddy *next;
};

/* Prefixes to use for packet IDs (mainly for IQ packets ATM). Usually the
   first one should be used, but when storing a packet in the cache, a
   "special" kind of ID is assigned to make it easier later to figure out
   if we have to do call an event handler for the response packet. */
#define JABBER_PACKET_ID "BeeP"
#define JABBER_CACHED_ID "BeeC"

/* RFC 392[01] stuff */
#define XMLNS_TLS          "urn:ietf:params:xml:ns:xmpp-tls"
#define XMLNS_SASL         "urn:ietf:params:xml:ns:xmpp-sasl"
#define XMLNS_BIND         "urn:ietf:params:xml:ns:xmpp-bind"
#define XMLNS_SESSION      "urn:ietf:params:xml:ns:xmpp-session"
#define XMLNS_STANZA_ERROR "urn:ietf:params:xml:ns:xmpp-stanzas"
#define XMLNS_STREAM_ERROR "urn:ietf:params:xml:ns:xmpp-streams"
#define XMLNS_ROSTER       "jabber:iq:roster"

/* Some supported extensions/legacy stuff */
#define XMLNS_AUTH         "jabber:iq:auth"                     /* XEP-0078 */
#define XMLNS_VERSION      "jabber:iq:version"                  /* XEP-0092 */
#define XMLNS_TIME         "jabber:iq:time"                     /* XEP-0090 */
#define XMLNS_VCARD        "vcard-temp"                         /* XEP-0054 */
#define XMLNS_CHATSTATES   "http://jabber.org/protocol/chatstates"  /* 0085 */
#define XMLNS_DISCOVER     "http://jabber.org/protocol/disco#info"  /* 0030 */

/* iq.c */
xt_status jabber_pkt_iq( struct xt_node *node, gpointer data );
int jabber_init_iq_auth( struct im_connection *ic );
xt_status jabber_pkt_bind_sess( struct im_connection *ic, struct xt_node *node, struct xt_node *orig );
int jabber_get_roster( struct im_connection *ic );
int jabber_get_vcard( struct im_connection *ic, char *bare_jid );
int jabber_add_to_roster( struct im_connection *ic, char *handle, char *name );
int jabber_remove_from_roster( struct im_connection *ic, char *handle );

/* message.c */
xt_status jabber_pkt_message( struct xt_node *node, gpointer data );

/* presence.c */
xt_status jabber_pkt_presence( struct xt_node *node, gpointer data );
int presence_send_update( struct im_connection *ic );
int presence_send_request( struct im_connection *ic, char *handle, char *request );

/* jabber_util.c */
char *set_eval_priority( set_t *set, char *value );
char *set_eval_tls( set_t *set, char *value );
struct xt_node *jabber_make_packet( char *name, char *type, char *to, struct xt_node *children );
struct xt_node *jabber_make_error_packet( struct xt_node *orig, char *err_cond, char *err_type );
void jabber_cache_add( struct im_connection *ic, struct xt_node *node, jabber_cache_event func );
struct xt_node *jabber_cache_get( struct im_connection *ic, char *id );
void jabber_cache_entry_free( gpointer entry );
void jabber_cache_clean( struct im_connection *ic );
const struct jabber_away_state *jabber_away_state_by_code( char *code );
const struct jabber_away_state *jabber_away_state_by_name( char *name );
void jabber_buddy_ask( struct im_connection *ic, char *handle );
char *jabber_normalize( char *orig );

typedef enum
{
	GET_BUDDY_CREAT = 1,	/* Try to create it, if necessary. */
	GET_BUDDY_EXACT = 2,	/* Get an exact message (only makes sense with bare JIDs). */
} get_buddy_flags_t;

struct jabber_buddy *jabber_buddy_add( struct im_connection *ic, char *full_jid );
struct jabber_buddy *jabber_buddy_by_jid( struct im_connection *ic, char *jid, get_buddy_flags_t flags );
int jabber_buddy_remove( struct im_connection *ic, char *full_jid );
int jabber_buddy_remove_bare( struct im_connection *ic, char *bare_jid );

extern const struct jabber_away_state jabber_away_state_list[];

/* io.c */
int jabber_write_packet( struct im_connection *ic, struct xt_node *node );
int jabber_write( struct im_connection *ic, char *buf, int len );
gboolean jabber_connected_plain( gpointer data, gint source, b_input_condition cond );
gboolean jabber_connected_ssl( gpointer data, void *source, b_input_condition cond );
gboolean jabber_start_stream( struct im_connection *ic );
void jabber_end_stream( struct im_connection *ic );

/* sasl.c */
xt_status sasl_pkt_mechanisms( struct xt_node *node, gpointer data );
xt_status sasl_pkt_challenge( struct xt_node *node, gpointer data );
xt_status sasl_pkt_result( struct xt_node *node, gpointer data );
gboolean sasl_supported( struct im_connection *ic );

#endif
