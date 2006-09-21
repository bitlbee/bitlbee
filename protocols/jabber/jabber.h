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
	JFLAG_STREAM_STARTED = 1,
	JFLAG_AUTHENTICATED = 2,
} jabber_flags_t;

/* iq.c */
xt_status jabber_pkt_iq( struct xt_node *node, gpointer data );
int jabber_start_auth( struct gaim_connection *gc );
int jabber_get_roster( struct gaim_connection *gc );

xt_status jabber_pkt_message( struct xt_node *node, gpointer data );

/* presence.c */
xt_status jabber_pkt_presence( struct xt_node *node, gpointer data );
int presence_announce( struct gaim_connection *gc );

/* jabber_util.c */
char *set_eval_resprio( set_t *set, char *value );
char *set_eval_tls( set_t *set, char *value );
struct xt_node *jabber_make_packet( char *name, char *type, char *to, struct xt_node *children );

/* io.c */
int jabber_write_packet( struct gaim_connection *gc, struct xt_node *node );
int jabber_write( struct gaim_connection *gc, char *buf, int len );
gboolean jabber_connected_plain( gpointer data, gint source, b_input_condition cond );
gboolean jabber_start_stream( struct gaim_connection *gc );
void jabber_end_stream( struct gaim_connection *gc );

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
};

#endif
