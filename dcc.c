/********************************************************************\
* BitlBee -- An IRC to other IM-networks gateway                     *
*                                                                    *
* Copyright 2007 Uli Meis <a.sporto+bee@gmail.com>                   *
\********************************************************************/

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
  if not, write to the Free Software Foundation, Inc., 59 Temple Place,
  Suite 330, Boston, MA  02111-1307  USA
*/

#define BITLBEE_CORE
#include "bitlbee.h"
#include "ft.h"
#include "dcc.h"
#include <poll.h>
#include <netinet/tcp.h>

/* 
 * Since that might be confusing a note on naming:
 *
 * Generic dcc functions start with 
 *
 * 	dcc_
 *
 * ,methods specific to DCC SEND start with
 *
 * 	dccs_
 *
 * . Since we can be on both ends of a DCC SEND,
 * functions specific to one end are called
 *
 * 	dccs_send and dccs_recv
 *
 * ,respectively.
 */


/* 
 * used to generate a unique local transfer id the user
 * can use to reject/cancel transfers
 */
unsigned int local_transfer_id=1;

/* 
 * just for debugging the nr. of chunks we received from im-protocols and the total data
 */
unsigned int receivedchunks=0, receiveddata=0;

/* 
 * If using DCC SEND AHEAD this value will be set before the first transfer starts.
 * Not that in this case it degenerates to the maximum message size to send() and
 * has nothing to do with packets.
 */
#ifdef DCC_SEND_AHEAD
int max_packet_size = DCC_PACKET_SIZE;
#else
int max_packet_size = 0;
#endif

static void dcc_finish( file_transfer_t *file );
static void dcc_close( file_transfer_t *file );
gboolean dccs_send_proto( gpointer data, gint fd, b_input_condition cond );
gboolean dcc_listen( dcc_file_transfer_t *df, struct sockaddr_storage **saddr_ptr );
int dccs_send_request( struct dcc_file_transfer *df, char *user_nick, struct sockaddr_storage *saddr );

/* As defined in ft.h */
file_transfer_t *imcb_file_send_start( struct im_connection *ic, char *handle, char *file_name, size_t file_size )
{
	user_t *u = user_findhandle( ic, handle );
	/* one could handle this more intelligent like imcb_buddy_msg.
	 * can't call it directly though cause it does some wrapping.
	 * Maybe give imcb_buddy_msg a parameter NO_WRAPPING? */
	if (!u) return NULL;

	return dccs_send_start( ic, u->nick, file_name, file_size );
};

/* As defined in ft.h */
void imcb_file_canceled( file_transfer_t *file, char *reason )
{
	if( file->canceled )
		file->canceled( file, reason );

	dcc_close( file );
}

/* As defined in ft.h */
gboolean imcb_file_write( file_transfer_t *file, gpointer data, size_t data_size )
{
	return dccs_send_write( file, data, data_size );
}

/* This is where the sending magic starts... */
file_transfer_t *dccs_send_start( struct im_connection *ic, char *user_nick, char *file_name, size_t file_size )
{
	file_transfer_t *file;
	dcc_file_transfer_t *df;
	struct sockaddr_storage **saddr;

	if( file_size > global.conf->max_filetransfer_size )
		return NULL;
	
	/* alloc stuff */
	file = g_new0( file_transfer_t, 1 );
	file->priv = df = g_new0( dcc_file_transfer_t, 1);
	file->file_size = file_size;
	file->file_name = g_strdup( file_name );
	file->local_id = local_transfer_id++;
	df->ic = ic;
	df->ft = file;
	
	/* listen and request */
	if( !dcc_listen( df, saddr ) ||
	    !dccs_send_request( df, user_nick, *saddr ) )
		return NULL;

	g_free( *saddr );

	/* watch */
	df->watch_in = b_input_add( df->fd, GAIM_INPUT_READ, dccs_send_proto, df );

	df->ic->irc->file_transfers = g_slist_prepend( df->ic->irc->file_transfers, file );

	return file;
}

/* Used pretty much everywhere in the code to abort a transfer */
gboolean dcc_abort( dcc_file_transfer_t *df, char *reason, ... )
{
	file_transfer_t *file = df->ft;
	va_list params;
	va_start( params, reason );
	char *msg = g_strdup_vprintf( reason, params );
	va_end( params );
	
	file->status |= FT_STATUS_CANCELED;
	
	if( file->canceled )
		file->canceled( file, msg );
	else 
		imcb_log( df->ic, "DCC transfer aborted: %s", msg );

	g_free( msg );

	dcc_close( df->ft );

	return FALSE;
}

/* used extensively for socket operations */
#define ASSERTSOCKOP(op, msg) \
	if( (op) == -1 ) \
		return dcc_abort( df , msg ": %s", strerror( errno ) );

/* Creates the "DCC SEND" line and sends it to the server */
int dccs_send_request( struct dcc_file_transfer *df, char *user_nick, struct sockaddr_storage *saddr )
{
	char ipaddr[INET6_ADDRSTRLEN]; 
	const void *netaddr;
	int port;
	char *cmd;

	if( saddr->ss_family == AF_INET )
	{
		struct sockaddr_in *saddr_ipv4 = ( struct sockaddr_in *) saddr;

		/* 
		 * this is so ridiculous. We're supposed to convert the address to
		 * host byte order!!! Let's exclude anyone running big endian just
		 * for the fun of it...
		 */
		sprintf( ipaddr, "%d", 
			 htonl( saddr_ipv4->sin_addr.s_addr ) );
		port = saddr_ipv4->sin_port;
	} else 
	{
		struct sockaddr_in6 *saddr_ipv6 = ( struct sockaddr_in6 *) saddr;

		netaddr = &saddr_ipv6->sin6_addr.s6_addr;
		port = saddr_ipv6->sin6_port;

		/* 
		 * Didn't find docs about this, but it seems that's the way irssi does it
		 */
		if( !inet_ntop( saddr->ss_family, netaddr, ipaddr, sizeof( ipaddr ) ) )
			return dcc_abort( df, "inet_ntop failed: %s", strerror( errno ) );
	}

	port = ntohs( port );

	cmd = g_strdup_printf( "\001DCC SEND %s %s %u %zu\001",
				df->ft->file_name, ipaddr, port, df->ft->file_size );
	
	if ( !irc_msgfrom( df->ic->irc, user_nick, cmd ) )
		return dcc_abort( df, "couldn't send 'DCC SEND' message to %s", user_nick );

	g_free( cmd );

	/* message is sortof redundant cause the users client probably informs him about that. remove? */
	imcb_log( df->ic, "Transferring file %s: Chose local address %s for DCC connection", df->ft->file_name, ipaddr );

	return TRUE;
}

/*
 * Creates a listening socket and returns it in saddr_ptr.
 */
gboolean dcc_listen( dcc_file_transfer_t *df, struct sockaddr_storage **saddr_ptr )
{
	file_transfer_t *file = df->ft;
	struct sockaddr_storage *saddr;
	int fd;
	char hostname[ HOST_NAME_MAX + 1 ];
	struct addrinfo hints, *rp;
	socklen_t ssize = sizeof( struct sockaddr_storage );

	/* won't be long till someone asks for this to be configurable :) */

	ASSERTSOCKOP( gethostname( hostname, sizeof( hostname ) ), "gethostname()" );

	memset( &hints, 0, sizeof( struct addrinfo ) );
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_NUMERICSERV;

	if ( getaddrinfo( hostname, "0", &hints, &rp ) != 0 )
		return dcc_abort( df, "getaddrinfo()" );

	saddr = g_new( struct sockaddr_storage, 1 );

	*saddr_ptr = saddr;

	memcpy( saddr, rp->ai_addr, rp->ai_addrlen );

	ASSERTSOCKOP( fd = df->fd = socket( saddr->ss_family, SOCK_STREAM, 0 ), "Opening socket" );

	ASSERTSOCKOP( bind( fd, ( struct sockaddr *)saddr, rp->ai_addrlen ), "Binding socket" );
	
	freeaddrinfo( rp );

	ASSERTSOCKOP( getsockname( fd, ( struct sockaddr *)saddr, &ssize ), "Getting socket name" );

	ASSERTSOCKOP( listen( fd, 1 ), "Making socket listen" );

	file->status = FT_STATUS_LISTENING;

	return TRUE;
}

/*
 * After setup, the transfer itself is handled entirely by this function.
 * There are basically four things to handle: connect, receive, send, and error.
 */
gboolean dccs_send_proto( gpointer data, gint fd, b_input_condition cond )
{
	dcc_file_transfer_t *df = data;
	file_transfer_t *file = df->ft;
	struct pollfd pfd = { .fd = fd, .events = POLLHUP|POLLERR|POLLIN|POLLOUT };
	short revents;
	
	if ( poll( &pfd, 1, 0 ) == -1 )
	{
		imcb_log( df->ic, "poll() failed, weird!" );
		revents = 0;
	};

	revents = pfd.revents;

	if( revents & POLLERR )
	{
		int sockerror;
		socklen_t errlen = sizeof( sockerror );

		if ( getsockopt( fd, SOL_SOCKET, SO_ERROR, &sockerror, &errlen ) )
			return dcc_abort( df, "getsockopt() failed, unknown socket error (weird!)" );

		return dcc_abort( df, "Socket error: %s", strerror( sockerror ) );
	}
	
	if( revents & POLLHUP ) 
		return dcc_abort( df, "Remote end closed connection" );
	
	if( ( revents & POLLIN ) &&
	    ( file->status & FT_STATUS_LISTENING ) )
	{ 	
		struct sockaddr *clt_addr;
		socklen_t ssize = sizeof( clt_addr );

		/* Connect */

		ASSERTSOCKOP( df->fd = accept( fd, (struct sockaddr *) &clt_addr, &ssize ), "Accepting connection" );

		closesocket( fd );
		fd = df->fd;
		file->status = FT_STATUS_TRANSFERING;
		sock_make_nonblocking( fd );

#ifdef DCC_SEND_AHEAD
		/* 
		 * use twice the maximum segment size as a maximum for calls to send().
		 */
		if( max_packet_size == 0 )
		{
			unsigned int mpslen = sizeof( max_packet_size );
			if( getsockopt( fd, IPPROTO_TCP, TCP_MAXSEG, &max_packet_size, &mpslen ) )
				return dcc_abort( df, "getsockopt() failed" );
			max_packet_size *= 2;
		}
#endif
		/* IM protocol callback */

		if( file->accept )
			file->accept( file );
		/* reschedule for reading on new fd */
		df->watch_in = b_input_add( fd, GAIM_INPUT_READ, dccs_send_proto, df );

		return FALSE;
	}

	if( revents & POLLIN ) 
	{
		int bytes_received;
		int ret;
		
		ASSERTSOCKOP( ret = recv( fd, &bytes_received, sizeof( bytes_received  ), MSG_PEEK ), "Receiving" );

		if( ret == 0 )
			return dcc_abort( df, "Remote end closed connection" );
			
		if( ret < 4 )
		{
			imcb_log( df->ic, "WARNING: DCC SEND: receiver sent only 2 bytes instead of 4, shouldn't happen too often!" );
			return TRUE;
		}

		ASSERTSOCKOP( ret = recv( fd, &bytes_received, sizeof( bytes_received  ), 0 ), "Receiving" );
		if( ret != 4 )
			return dcc_abort( df, "MSG_PEEK'ed 4, but can only dequeue %d bytes", ret );

		bytes_received = ntohl( bytes_received );

		/* If any of this is actually happening, the receiver should buy a new IRC client */

		if ( bytes_received > df->bytes_sent )
			return dcc_abort( df, "Receiver magically received more bytes than sent ( %d > %d ) (BUG at receiver?)", bytes_received, df->bytes_sent );

		if ( bytes_received < file->bytes_transferred )
			return dcc_abort( df, "Receiver lost bytes? ( has %d, had %d ) (BUG at receiver?)", bytes_received, file->bytes_transferred );
		
		file->bytes_transferred = bytes_received;
	
		if( file->bytes_transferred >= file->file_size ) {
			dcc_finish( file );
			return FALSE;
		}
	
#ifndef DCC_SEND_AHEAD
		/* reschedule writer if neccessary */
		if( file->bytes_transferred >= df->bytes_sent && 
		    df->watch_out == 0 && 
		    df->queued_bytes > 0 ) {
			df->watch_out = b_input_add( fd, GAIM_INPUT_WRITE, dcc_send_proto, df );
		}
#endif
		return TRUE;
	}

	if( revents & POLLOUT )
	{
		struct dcc_buffer *dccb;
		int ret;
		char *msg;

		if( df->queued_bytes == 0 )
		{
			/* shouldn't happen */
			imcb_log( df->ic, "WARNING: DCC SEND: write called with empty queue" );

			df->watch_out = 0;
			return FALSE;
		}

		/* start where we left off */
 		if( !( df->queued_buffers ) ||
		    !( dccb = df->queued_buffers->data ) )
			return dcc_abort( df, "BUG in DCC SEND: queued data but no buffers" );

		msg = dccb->b + df->buffer_pos;

		int msgsize = MIN( 
#ifndef DCC_SEND_AHEAD
				  file->bytes_transferred + MAX_PACKET_SIZE - df->bytes_sent,
#else
				  max_packet_size,
#endif
				  dccb->len - df->buffer_pos );

		if ( msgsize == 0 )
		{
			df->watch_out = 0;
			return FALSE;
		}

		ASSERTSOCKOP( ret = send( fd, msg, msgsize, 0 ), "Sending data" );

		if( ret == 0 )
			return dcc_abort( df, "Remote end closed connection" );

		df->bytes_sent += ret;
		df->queued_bytes -= ret;
		df->buffer_pos += ret;

		if( df->buffer_pos == dccb->len )
		{
			df->buffer_pos = 0;
			df->queued_buffers = g_slist_remove( df->queued_buffers, dccb );
			g_free( dccb->b );
			g_free( dccb );
		}

		if( ( df->queued_bytes < DCC_QUEUE_THRESHOLD_LOW ) && file->out_of_data )
			file->out_of_data( file );
	
		if( df->queued_bytes > 0 )
		{
			/* Who knows how long the event loop cycle will take, 
			 * let's just try to send some more now. */
#ifndef DCC_SEND_AHEAD
			if( df->bytes_sent < ( file->bytes_transferred + max_packet_size ) )
#endif
				return dccs_send_proto( df, fd, cond );
		}

		df->watch_out = 0;
		return FALSE;
	}

	/* Send buffer full, come back later */

	return TRUE;
}

/* 
 * Incoming data. Note that the buffer MUST NOT be freed by the caller!
 * We don't copy the buffer but put it in our queue.
 * 
 * */
gboolean dccs_send_write( file_transfer_t *file, gpointer data, unsigned int data_size )
{
	dcc_file_transfer_t *df = file->priv;
	struct dcc_buffer *dccb = g_new0( struct dcc_buffer, 1 );

	receivedchunks++; receiveddata += data_size;

	dccb->b = data;
	dccb->len = data_size;

	df->queued_buffers = g_slist_append( df->queued_buffers, dccb );

	df->queued_bytes += data_size;

	if( ( file->status & FT_STATUS_TRANSFERING ) && 
#ifndef DCC_SEND_AHEAD
	    ( file->bytes_transferred >= df->bytes_sent ) && 
#endif
	    ( df->watch_out == 0 ) && 
	    ( df->queued_bytes > 0 ) )
	{
		df->watch_out = b_input_add( df->fd, GAIM_INPUT_WRITE, dccs_send_proto, df );
	}
	
	return df->queued_bytes > DCC_QUEUE_THRESHOLD_HIGH;
}

/*
 * Cleans up after a transfer.
 */
static void dcc_close( file_transfer_t *file )
{
	dcc_file_transfer_t *df = file->priv;

	if( file->free )
		file->free( file );
	
	closesocket( df->fd );

	if( df->watch_in )
		b_event_remove( df->watch_in );

	if( df->watch_out )
		b_event_remove( df->watch_out );
	
	if( df->queued_buffers )
	{
		struct dcc_buffer *dccb;
		GSList *gslist = df->queued_buffers;

		for( ; gslist ; gslist = g_slist_next( gslist ) )
		{
			dccb = gslist->data;
			g_free( dccb->b );
			g_free( dccb );
		}
		g_slist_free( df->queued_buffers );
	}

	df->ic->irc->file_transfers = g_slist_remove( df->ic->irc->file_transfers, file );
	
	g_free( df );
	g_free( file->file_name );
	g_free( file );
}

void dcc_finish( file_transfer_t *file )
{
	file->status |= FT_STATUS_FINISHED;
	
	if( file->finished )
		file->finished( file );

	dcc_close( file );
}
