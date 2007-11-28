/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Jabber module - stream handling                                          *
*                                                                           *
*  Copyright 2007 Uli Meis <a.sporto+bee@gmail.com>                         *
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

#include "jabber.h"
#include "sha1.h"
#include <poll.h>

/* Some structs for the SOCKS5 handshake */

struct bs_handshake_data {

	struct jabber_transfer *tf;

	/* <query> element and <streamhost> elements */
	struct xt_node *qnode, *shnode;

	enum 
	{ 
		BS_PHASE_CONNECT, 
		BS_PHASE_CONNECTED, 
		BS_PHASE_REQUEST, 
		BS_PHASE_REPLY, 
		BS_PHASE_REPLY_HAVE_LEN 
	} phase;

	/* SHA1( SID + Initiator JID + Target JID) */
	char *pseudoadr;

	void (*parentfree) ( file_transfer_t *ft );

	gint connect_timeout;
};

struct socks5_hdr
{
	unsigned char ver;
	union
	{
		unsigned char cmd;
		unsigned char rep;
	} cmdrep;
	unsigned char rsv;
	unsigned char atyp;
};

struct socks5_message
{
	struct socks5_hdr hdr;
	unsigned char addrlen;
	unsigned char address[64];
}; 

/* connect() timeout in seconds. */
#define JABBER_BS_CONTIMEOUT 15

/* shouldn't matter if it's mostly too much, kernel's smart about that
 * and will only reserve some address space */
#define JABBER_BS_BUFSIZE 65536

gboolean jabber_bs_handshake( gpointer data, gint fd, b_input_condition cond );

gboolean jabber_bs_handshake_abort( struct bs_handshake_data *bhd, char *format, ... );

void jabber_bs_answer_request( struct bs_handshake_data *bhd );

gboolean jabber_bs_read( gpointer data, gint fd, b_input_condition cond );

void jabber_bs_out_of_data( file_transfer_t *ft );

void jabber_bs_canceled( file_transfer_t *ft , char *reason );


void jabber_bs_free_transfer( file_transfer_t *ft) {
	struct jabber_transfer *tf = ft->data;
	struct bs_handshake_data *bhd = tf->streamhandle;
	void (*parentfree) ( file_transfer_t *ft );

	parentfree = bhd->parentfree;

	if ( tf->watch_in )
		b_event_remove( tf->watch_in );
	
	if( tf->watch_out )
		b_event_remove( tf->watch_out );
	
	g_free( bhd->pseudoadr );
	xt_free_node( bhd->qnode );
	g_free( bhd );

	parentfree( ft );
}

/*
 * Parses an incoming bytestream request and calls jabber_bs_handshake on success.
 */
int jabber_bs_request( struct im_connection *ic, struct xt_node *node, struct xt_node *qnode)
{
	char *sid, *ini_jid, *tgt_jid, *mode, *iq_id;
	struct jabber_data *jd = ic->proto_data;
	struct jabber_transfer *tf = NULL;
	GSList *tflist;
	struct bs_handshake_data *bhd;

	sha1_state_t sha;
	char hash_hex[41];
	unsigned char hash[20];
	int i;
	
	if( !(iq_id   = xt_find_attr( node, "id" ) ) ||
	    !(ini_jid = xt_find_attr( node, "from" ) ) ||
	    !(tgt_jid = xt_find_attr( node, "to" ) ) ||
	    !(sid     = xt_find_attr( qnode, "sid" ) ) )
	{
		imcb_log( ic, "WARNING: Received incomplete SI bytestream request");
		return XT_HANDLED;
	}

	if( ( mode = xt_find_attr( qnode, "mode" ) ) &&
	      ( strcmp( mode, "tcp" ) != 0 ) ) 
	{
		imcb_log( ic, "WARNING: Received SI Request for unsupported bytestream mode %s", xt_find_attr( qnode, "mode" ) );
		return XT_HANDLED;
	}

	/* Let's see if we can find out what this bytestream should be for... */

	for( tflist = jd->filetransfers ; tflist; tflist = g_slist_next(tflist) )
	{
		struct jabber_transfer *tft = tflist->data;
		if( ( strcmp( tft->sid, sid ) == 0 ) &&
		    ( strcmp( tft->ini_jid, ini_jid ) == 0 ) &&
		    ( strcmp( tft->tgt_jid, tgt_jid ) == 0 ) )
		{
		    	tf = tft;
			break;
		}
	}

	if (!tf) 
	{
		imcb_log( ic, "WARNING: Received bytestream request from %s that doesn't match an SI request", ini_jid );
		return XT_HANDLED;
	}

	/* iq_id and canceled can be reused since SI is done */
	g_free( tf->iq_id );
	tf->iq_id = g_strdup( iq_id );

	tf->ft->canceled = jabber_bs_canceled;

	/* SHA1( SID + Initiator JID + Target JID ) is given to the streamhost which it will match against the initiator's value */
	sha1_init( &sha );
	sha1_append( &sha, (unsigned char*) sid, strlen( sid ) );
	sha1_append( &sha, (unsigned char*) ini_jid, strlen( ini_jid ) );
	sha1_append( &sha, (unsigned char*) tgt_jid, strlen( tgt_jid ) );
	sha1_finish( &sha, hash );
	
	for( i = 0; i < 20; i ++ )
		sprintf( hash_hex + i * 2, "%02x", hash[i] );
		
	bhd = g_new0( struct bs_handshake_data, 1 );
	bhd->tf = tf;
	bhd->qnode = xt_dup( qnode );
	bhd->shnode = bhd->qnode->children;
	bhd->phase = BS_PHASE_CONNECT;
	bhd->pseudoadr = g_strdup( hash_hex );
	tf->streamhandle = bhd;
	bhd->parentfree = tf->ft->free;
	tf->ft->free = jabber_bs_free_transfer;

	jabber_bs_handshake( bhd, 0, 0 ); 

	return XT_HANDLED;
}

/* 
 * This function is scheduled in bs_handshake via b_timeout_add after a (non-blocking) connect().
 */
gboolean jabber_bs_connect_timeout( gpointer data, gint fd, b_input_condition cond )
{
	struct bs_handshake_data *bhd = data;

	bhd->connect_timeout = 0;

	jabber_bs_handshake_abort( bhd, "no connection after %d seconds", JABBER_BS_CONTIMEOUT );

	return FALSE;
}

/*
 * This is what a protocol handshake can look like in cooperative multitasking :)
 * Might be confusing at first because it's called from different places and is recursing.
 * (places being the event thread, bs_request, bs_handshake_abort, and itself)
 *
 * All in all, it turned out quite nice :)
 */
gboolean jabber_bs_handshake( gpointer data, gint fd, b_input_condition cond )
{

/* very useful */
#define ASSERTSOCKOP(op, msg) \
	if( (op) == -1 ) \
		return jabber_bs_handshake_abort( bhd , msg ": %s", strerror( errno ) );

	struct bs_handshake_data *bhd = data;
	struct pollfd pfd = { .fd = fd, .events = POLLHUP|POLLERR };
	short revents;
	
	if ( bhd->connect_timeout )
	{
		b_event_remove( bhd->connect_timeout );
		bhd->connect_timeout = 0;
	}

	
	/* we need the real io condition */
	if ( poll( &pfd, 1, 0 ) == -1 )
	{
		imcb_log( bhd->tf->ic, "poll() failed, weird!" );
		revents = 0;
	};

	revents = pfd.revents;

	if( revents & POLLERR )
	{
		int sockerror;
		socklen_t errlen = sizeof( sockerror );

		if ( getsockopt( fd, SOL_SOCKET, SO_ERROR, &sockerror, &errlen ) )
			return jabber_bs_handshake_abort( bhd, "getsockopt() failed, unknown socket error during SOCKS5 handshake (weird!)" );

		if ( bhd->phase == BS_PHASE_CONNECTED )
			return jabber_bs_handshake_abort( bhd, "connect() failed: %s", strerror( sockerror ) );

		return jabber_bs_handshake_abort( bhd, "Socket error during SOCKS5 handshake(weird!): %s", strerror( sockerror ) );
	}

	if( revents & POLLHUP )
		return jabber_bs_handshake_abort( bhd, "Remote end closed connection" );
	

	switch( bhd->phase ) 
	{
	case BS_PHASE_CONNECT:
		{
			struct xt_node *c;
			char *host, *port;
			struct addrinfo hints, *rp;

			if( ( c = bhd->shnode = xt_find_node( bhd->shnode, "streamhost" ) ) &&
			    ( port = xt_find_attr( c, "port" ) ) &&
			    ( host = xt_find_attr( c, "host" ) ) &&
			    xt_find_attr( c, "jid" ) )
			{
				memset( &hints, 0, sizeof( struct addrinfo ) );
				hints.ai_socktype = SOCK_STREAM;

				if ( getaddrinfo( host, port, &hints, &rp ) != 0 )
					return jabber_bs_handshake_abort( bhd, "getaddrinfo() failed: %s", strerror( errno ) );

				ASSERTSOCKOP( bhd->tf->fd = fd = socket( rp->ai_family, rp->ai_socktype, 0 ), "Opening socket" );

				sock_make_nonblocking( fd );

				imcb_log( bhd->tf->ic, "Transferring file %s: Connecting to streamhost %s:%s", bhd->tf->ft->file_name, host, port );

				if( ( connect( fd, rp->ai_addr, rp->ai_addrlen ) == -1 ) &&
				    ( errno != EINPROGRESS ) )
					return jabber_bs_handshake_abort( bhd , "connect() failed: %s", strerror( errno ) );

				freeaddrinfo( rp );

				bhd->phase = BS_PHASE_CONNECTED;
				
				bhd->tf->watch_out = b_input_add( fd, GAIM_INPUT_WRITE, jabber_bs_handshake, bhd );

				/* since it takes forever(3mins?) till connect() fails on itself we schedule a timeout */
				bhd->connect_timeout = b_timeout_add( JABBER_BS_CONTIMEOUT * 1000, jabber_bs_connect_timeout, bhd );

				bhd->tf->watch_in = 0;
				return FALSE;
			} else
				return jabber_bs_handshake_abort( bhd, c ? "incomplete streamhost entry: host=%s port=%s jid=%s" : NULL,
								  host, port, xt_find_attr( c, "jid" ) );
		}
	case BS_PHASE_CONNECTED:
		{
			struct {
				unsigned char ver;
				unsigned char nmethods;
				unsigned char method;
			} socks5_hello = {
				.ver = 5,
				.nmethods = 1,
				.method = 0x00 /* no auth */
				/* one could also implement username/password. If you know
				 * a jabber client or proxy that actually does it, tell me.
				 */
			};
			
			ASSERTSOCKOP( send( fd, &socks5_hello, sizeof( socks5_hello ) , 0 ), "Sending auth request" );

			bhd->phase = BS_PHASE_REQUEST;

			bhd->tf->watch_in = b_input_add( fd, GAIM_INPUT_READ, jabber_bs_handshake, bhd );

			bhd->tf->watch_out = 0;
			return FALSE;
		}
	case BS_PHASE_REQUEST:
		{
			struct socks5_message socks5_connect = 
			{
				.hdr =
				{
					.ver = 5,
					.cmdrep.cmd = 0x01,
					.rsv = 0,
					.atyp = 0x03
				},
				.addrlen = strlen( bhd->pseudoadr )
			};
			int ret;
			char buf[2];

			/* If someone's trying to be funny and sends only one byte at a time we'll fail :) */
			ASSERTSOCKOP( ret = recv( fd, buf, 2, 0 ) , "Receiving auth reply" );

			if( !( ret == 2 ) ||
			    !( buf[0] == 5 ) ||
			    !( buf[1] == 0 ) )
				return jabber_bs_handshake_abort( bhd, "Auth not accepted by streamhost (reply: len=%d, ver=%d, status=%d)",
									ret, buf[0], buf[1] );

			/* copy hash into connect message */
			memcpy( socks5_connect.address, bhd->pseudoadr, socks5_connect.addrlen );

			/* after the address comes the port, which is always 0 */
			memset( socks5_connect.address + socks5_connect.addrlen, 0, sizeof( in_port_t ) );

			ASSERTSOCKOP( send( fd, &socks5_connect, sizeof( struct socks5_hdr ) + 1 + socks5_connect.addrlen + sizeof( in_port_t ), 0 ) , "Sending SOCKS5 Connect" );

			bhd->phase = BS_PHASE_REPLY;

			return TRUE;
		}
	case BS_PHASE_REPLY:
	case BS_PHASE_REPLY_HAVE_LEN:
		{
			/* we have to wait till we have the address length, then we know how much data is left
			 * (not that we'd actually care about that data, but we need to eat it all up anyway)
			 */
			struct socks5_message socks5_reply;
			int ret;
			int expectedbytes = 
				sizeof( struct socks5_hdr ) + 1 + 
				( bhd->phase == BS_PHASE_REPLY_HAVE_LEN ? socks5_reply.addrlen + sizeof( in_port_t ) : 0 );

			/* notice the peek, we're doing this till enough is there */
			ASSERTSOCKOP( ret = recv( fd, &socks5_reply, expectedbytes, MSG_PEEK ) , "Peeking for SOCKS5 CONNECT reply" );

			if ( ret == 0 )
				return jabber_bs_handshake_abort( bhd , "peer has shutdown connection" );

			/* come again */
			if ( ret < expectedbytes )
				return TRUE;

			if ( bhd->phase == BS_PHASE_REPLY )
			{
				if( !( socks5_reply.hdr.ver == 5 ) ||
				    !( socks5_reply.hdr.cmdrep.rep == 0 ) ||
				    !( socks5_reply.hdr.atyp == 3 ) ||
				    !( socks5_reply.addrlen <= 62 ) ) /* should also be 40, but who cares as long as all fits in the buffer... */
					return jabber_bs_handshake_abort( bhd, "SOCKS5 CONNECT failed (reply: ver=%d, rep=%d, atyp=%d, addrlen=%d", 
						socks5_reply.hdr.ver,
						socks5_reply.hdr.cmdrep.rep,
						socks5_reply.hdr.atyp,
						socks5_reply.addrlen);

				/* and again for the rest */
				bhd->phase = BS_PHASE_REPLY_HAVE_LEN;
				
				/* since it's very likely that the rest is there as well, 
				 * let's not wait for the event loop to call us again */
				return jabber_bs_handshake( bhd , fd, 0 );
			}

			/* got it all, remove it from the queue */
			ASSERTSOCKOP( ret = recv( fd, &socks5_reply, expectedbytes, 0 ) , "Dequeueing MSG_PEEK'ed data after SOCKS5 CONNECT" );

			/* this shouldn't happen */
			if ( ret < expectedbytes )
				return jabber_bs_handshake_abort( bhd, "internal error, couldn't dequeue MSG_PEEK'ed data after SOCKS5 CONNECT" );

			/* we're actually done now... */

			jabber_bs_answer_request( bhd );

			bhd->tf->watch_in = 0;
			return FALSE;
		}
	default:
		/* BUG */
		imcb_log( bhd->tf->ic, "BUG in file transfer code: undefined handshake phase" );

		bhd->tf->watch_in = 0;
		return FALSE;
	}
#undef ASSERTSOCKOP
#undef JABBER_BS_ERR_CONDS
}

/*
 * If the handshake failed we can try the next streamhost, if there is one.
 * An intelligent sender would probably specify himself as the first streamhost and
 * a proxy as the second (Kopete is an example here). That way, a (potentially) 
 * slow proxy is only used if neccessary.
 */
gboolean jabber_bs_handshake_abort( struct bs_handshake_data *bhd, char *format, ... )
{
	struct jabber_transfer *tf = bhd->tf;
	struct xt_node *reply, *iqnode;

	if( bhd->shnode ) 
	{
		if( format ) {
			va_list params;
			va_start( params, format );
			char error[128];

			if( vsnprintf( error, 128, format, params ) < 0 )
				sprintf( error, "internal error parsing error string (BUG)" );
			va_end( params );

			imcb_log( tf->ic, "Transferring file %s: connection to streamhost %s:%s failed (%s)", 
				  tf->ft->file_name, 
				  xt_find_attr( bhd->shnode, "host" ),
				  xt_find_attr( bhd->shnode, "port" ),
				  error );
		}

		/* Alright, this streamhost failed, let's try the next... */
		bhd->phase = BS_PHASE_CONNECT;
		bhd->shnode = bhd->shnode->next;
		
		/* the if is not neccessary but saves us one recursion */
		if( bhd->shnode )
			return jabber_bs_handshake( bhd, 0, 0 );
	}

	/* out of stream hosts */

	iqnode = jabber_make_packet( "iq", "result", tf->ini_jid, NULL );
	reply = jabber_make_error_packet( iqnode, "item-not-found", "cancel" , "404" );
	xt_free_node( iqnode );

	xt_add_attr( reply, "id", tf->iq_id );
		
	if( !jabber_write_packet( tf->ic, reply ) )
		imcb_log( tf->ic, "WARNING: Error transmitting bytestream response" );
	xt_free_node( reply );

	imcb_file_canceled( tf->ft, "couldn't connect to any streamhosts" );

	bhd->tf->watch_in = 0;
	return FALSE;
}

/* 
 * After the SOCKS5 handshake succeeds we need to inform the initiator which streamhost we chose.
 * If he is the streamhost himself, he might already know that. However, if it's a proxy,
 * the initiator will have to make a connection himself.
 */
void jabber_bs_answer_request( struct bs_handshake_data *bhd )
{
	struct jabber_transfer *tf = bhd->tf;
	struct xt_node *reply;

	imcb_log( tf->ic, "Transferring file %s: established SOCKS5 connection to %s:%s", 
		  tf->ft->file_name, 
		  xt_find_attr( bhd->shnode, "host" ),
		  xt_find_attr( bhd->shnode, "port" ) );

	tf->ft->data = tf;
	tf->ft->started = time( NULL );
	tf->watch_in = b_input_add( tf->fd, GAIM_INPUT_READ, jabber_bs_read, tf );
	tf->ft->out_of_data = jabber_bs_out_of_data;

	reply = xt_new_node( "streamhost-used", NULL, NULL );
	xt_add_attr( reply, "jid", xt_find_attr( bhd->shnode, "jid" ) );

	reply = xt_new_node( "query", NULL, reply );
	xt_add_attr( reply, "xmlns", XMLNS_BYTESTREAMS );

	reply = jabber_make_packet( "iq", "result", tf->ini_jid, reply );

	xt_add_attr( reply, "id", tf->iq_id );
		
	if( !jabber_write_packet( tf->ic, reply ) )
		imcb_file_canceled( tf->ft, "Error transmitting bytestream response" );
	xt_free_node( reply );
}

/* Reads till it is unscheduled or the receiver signifies an overflow. */
gboolean jabber_bs_read( gpointer data, gint fd, b_input_condition cond )
{
	int ret;
	struct jabber_transfer *tf = data;
	char *buffer = g_malloc( JABBER_BS_BUFSIZE );

	if (tf->receiver_overflow)
	{
		if( tf->watch_in )
		{
			/* should never happen, BUG */
			imcb_file_canceled( tf->ft, "Bug in jabber file transfer code: read while overflow is true. Please report" );
			return FALSE;
		}
	}

	ret = recv( fd, buffer, JABBER_BS_BUFSIZE, 0 );

	if( ret == -1 )
	{
		/* shouldn't actually happen */
		if( errno == EAGAIN )
			return TRUE;

		imcb_file_canceled( tf->ft, "Error reading tcp socket" ); /* , strerror( errnum ) */

		return FALSE;
	}

	/* that should be all */
	if( ret == 0 )
		return FALSE;
	
	tf->bytesread += ret;

	buffer = g_realloc( buffer, ret );

	if ( ( tf->receiver_overflow = imcb_file_write( tf->ft, buffer, ret ) ) )
	{
		/* wait for imcb to run out of data */
		tf->watch_in = 0;
		return FALSE;
	}
		

	return TRUE;
}

/* imcb callback that is invoked when it runs out of data.
 * We reschedule jabber_bs_read here if neccessary. */
void jabber_bs_out_of_data( file_transfer_t *ft )
{
	struct jabber_transfer *tf = ft->data;

	tf->receiver_overflow = FALSE;

	if ( !tf->watch_in )
		tf->watch_in = b_input_add( tf->fd, GAIM_INPUT_READ, jabber_bs_read, tf );
}

/* Bad luck */
void jabber_bs_canceled( file_transfer_t *ft , char *reason )
{
	struct jabber_transfer *tf = ft->data;

	imcb_log( tf->ic, "File transfer aborted: %s", reason );
}
