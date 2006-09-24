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
	JFLAG_STREAM_STARTED = 1,	/* Set when we detected the beginning of the stream and want to do auth. */
	JFLAG_AUTHENTICATED = 2,	/* Set when we're successfully authenticatd. */
	JFLAG_STREAM_RESTART = 4,	/* Set when we want to restart the stream (after SASL or TLS). */
	JFLAG_WAIT_SESSION = 8,		/* Set if we sent a <session> tag and need a reply before we continue. */
	JFLAG_WAIT_BIND = 16,		/* ... for <bind> tag. */
} jabber_flags_t;

struct jabber_data
{
	struct gaim_connection *gc;
	
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
	
	struct xt_node *node_cache;
};

struct jabber_away_state
{
	char code[5];
	char *full_name;
};

/* iq.c */
xt_status jabber_pkt_iq( struct xt_node *node, gpointer data );
int jabber_start_iq_auth( struct gaim_connection *gc );
int jabber_get_roster( struct gaim_connection *gc );

xt_status jabber_pkt_message( struct xt_node *node, gpointer data );

/* presence.c */
xt_status jabber_pkt_presence( struct xt_node *node, gpointer data );
int presence_send_update( struct gaim_connection *gc );

/* jabber_util.c */
char *set_eval_resprio( set_t *set, char *value );
char *set_eval_tls( set_t *set, char *value );
struct xt_node *jabber_make_packet( char *name, char *type, char *to, struct xt_node *children );
void jabber_cache_packet( struct gaim_connection *gc, struct xt_node *node );
struct xt_node *jabber_packet_from_cache( struct gaim_connection *gc, char *id );
const struct jabber_away_state *jabber_away_state_by_code( char *code );
const struct jabber_away_state *jabber_away_state_by_name( char *name );

extern const struct jabber_away_state jabber_away_state_list[];

/* io.c */
int jabber_write_packet( struct gaim_connection *gc, struct xt_node *node );
int jabber_write( struct gaim_connection *gc, char *buf, int len );
gboolean jabber_connected_plain( gpointer data, gint source, b_input_condition cond );
gboolean jabber_start_stream( struct gaim_connection *gc );
void jabber_end_stream( struct gaim_connection *gc );

/* sasl.c */
xt_status sasl_pkt_mechanisms( struct xt_node *node, gpointer data );
xt_status sasl_pkt_challenge( struct xt_node *node, gpointer data );
xt_status sasl_pkt_result( struct xt_node *node, gpointer data );
gboolean sasl_supported( struct gaim_connection *gc );

#endif
