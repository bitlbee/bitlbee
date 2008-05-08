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
#include <regex.h>

/* Some ifdefs for ulibc (Thanks to Whoopie) */
#ifndef HOST_NAME_MAX
#include <sys/param.h>
#ifdef MAXHOSTNAMELEN
#define HOST_NAME_MAX MAXHOSTNAMELEN
#else
#define HOST_NAME_MAX 255
#endif
#endif

#ifndef AI_NUMERICSERV
#define AI_NUMERICSERV 0x0400   /* Don't use name resolution.  */
#endif

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

int max_packet_size = 0;

static void dcc_finish( file_transfer_t *file );
static void dcc_close( file_transfer_t *file );
gboolean dccs_send_proto( gpointer data, gint fd, b_input_condition cond );
gboolean dcc_listen( dcc_file_transfer_t *df, struct sockaddr_storage **saddr_ptr );
int dccs_send_request( struct dcc_file_transfer *df, char *user_nick, struct sockaddr_storage *saddr );
gboolean dccs_recv_start( file_transfer_t *ft );
gboolean dccs_recv_proto( gpointer data, gint fd, b_input_condition cond);
gboolean dccs_recv_write_request( file_transfer_t *ft );
gboolean dcc_progress( gpointer data, gint fd, b_input_condition cond );

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
gboolean imcb_file_recv_start( file_transfer_t *ft )
{
	return dccs_recv_start( ft );
}

dcc_file_transfer_t *dcc_alloc_transfer( char *file_name, size_t file_size, struct im_connection *ic )
{
	file_transfer_t *file = g_new0( file_transfer_t, 1 );
	dcc_file_transfer_t *df = file->priv = g_new0( dcc_file_transfer_t, 1);
	file->file_size = file_size;
	file->file_name = g_strdup( file_name );
	file->local_id = local_transfer_id++;
	df->ic = ic;
	df->ft = file;
	
	return df;
}

/* This is where the sending magic starts... */
file_transfer_t *dccs_send_start( struct im_connection *ic, char *user_nick, char *file_name, size_t file_size )
{
	file_transfer_t *file;
	dcc_file_transfer_t *df;
	struct sockaddr_storage *saddr;

	if( file_size > global.conf->max_filetransfer_size )
		return NULL;
	
	df = dcc_alloc_transfer( file_name, file_size, ic );
	file = df->ft;
	file->write = dccs_send_write;

	/* listen and request */
	if( !dcc_listen( df, &saddr ) ||
	    !dccs_send_request( df, user_nick, saddr ) )
		return NULL;

	g_free( saddr );

	/* watch */
	df->watch_in = b_input_add( df->fd, GAIM_INPUT_READ, dccs_send_proto, df );

	df->ic->irc->file_transfers = g_slist_prepend( df->ic->irc->file_transfers, file );

	df->progress_timeout = b_timeout_add( DCC_MAX_STALL * 1000, dcc_progress, df );

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

	imcb_log( df->ic, "File %s: DCC transfer aborted: %s", file->file_name, msg );

	g_free( msg );

	dcc_close( df->ft );

	return FALSE;
}

gboolean dcc_progress( gpointer data, gint fd, b_input_condition cond )
{
	struct dcc_file_transfer *df = data;

	if( df->bytes_sent == df->progress_bytes_last )
	{
		/* no progress. cancel */
		if( df->bytes_sent == 0 )
			return dcc_abort( df, "Couldnt establish transfer within %d seconds", DCC_MAX_STALL );
		else 
			return dcc_abort( df, "Transfer stalled for %d seconds at %d kb", DCC_MAX_STALL, df->bytes_sent / 1024 );

	}

	df->progress_bytes_last = df->bytes_sent;

	return TRUE;
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

		sprintf( ipaddr, "%d", 
			 ntohl( saddr_ipv4->sin_addr.s_addr ) );
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

	return TRUE;
}

/*
 * Creates a listening socket and returns it in saddr_ptr.
 */
gboolean dcc_listen( dcc_file_transfer_t *df, struct sockaddr_storage **saddr_ptr )
{
	file_transfer_t *file = df->ft;
	struct sockaddr_storage *saddr;
	int fd,gret;
	char hostname[ HOST_NAME_MAX + 1 ];
	struct addrinfo hints, *rp;
	socklen_t ssize = sizeof( struct sockaddr_storage );

	/* won't be long till someone asks for this to be configurable :) */

	ASSERTSOCKOP( gethostname( hostname, sizeof( hostname ) ), "gethostname()" );

	memset( &hints, 0, sizeof( struct addrinfo ) );
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_NUMERICSERV;

	if ( ( gret = getaddrinfo( hostname, "0", &hints, &rp ) != 0 ) )
		return dcc_abort( df, "getaddrinfo(): %s", gai_strerror( gret ) );

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
 * Checks poll(), same for receiving and sending
 */
gboolean dcc_poll( dcc_file_transfer_t *df, int fd, short *revents )
{
	struct pollfd pfd = { .fd = fd, .events = POLLHUP|POLLERR|POLLIN|POLLOUT };

	ASSERTSOCKOP( poll( &pfd, 1, 0 ), "poll()" )

	if( pfd.revents & POLLERR )
	{
		int sockerror;
		socklen_t errlen = sizeof( sockerror );

		if ( getsockopt( fd, SOL_SOCKET, SO_ERROR, &sockerror, &errlen ) )
			return dcc_abort( df, "getsockopt() failed, unknown socket error (weird!)" );

		return dcc_abort( df, "Socket error: %s", strerror( sockerror ) );
	}
	
	if( pfd.revents & POLLHUP ) 
		return dcc_abort( df, "Remote end closed connection" );
	
	*revents = pfd.revents;

	return TRUE;
}

/*
 * fills max_packet_size with twice the TCP maximum segment size
 */
gboolean  dcc_check_maxseg( dcc_file_transfer_t *df, int fd )
{
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
	short revents;
	
	if( !dcc_poll( df, fd, &revents) )
		return FALSE;

	if( ( revents & POLLIN ) &&
	    ( file->status & FT_STATUS_LISTENING ) )
	{ 	
		struct sockaddr *clt_addr;
		socklen_t ssize = sizeof( clt_addr );

		/* Connect */

		ASSERTSOCKOP( df->fd = accept( fd, (struct sockaddr *) &clt_addr, &ssize ), "Accepting connection" );

		closesocket( fd );
		fd = df->fd;
		file->status = FT_STATUS_TRANSFERRING;
		sock_make_nonblocking( fd );

		if ( !dcc_check_maxseg( df, fd ) )
			return FALSE;

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
	
		return TRUE;
	}

	return TRUE;
}

gboolean dccs_recv_start( file_transfer_t *ft )
{
	dcc_file_transfer_t *df = ft->priv;
	struct sockaddr_storage *saddr = &df->saddr;
	int fd;
	char ipaddr[INET6_ADDRSTRLEN]; 
	socklen_t sa_len = saddr->ss_family == AF_INET ? 
		sizeof( struct sockaddr_in ) : sizeof( struct sockaddr_in6 );
	
	if( !ft->write )
		return dcc_abort( df, "BUG: protocol didn't register write()" );
	
	ASSERTSOCKOP( fd = df->fd = socket( saddr->ss_family, SOCK_STREAM, 0 ) , "Opening Socket" );

	sock_make_nonblocking( fd );

	if( ( connect( fd, (struct sockaddr *)saddr, sa_len ) == -1 ) &&
	    ( errno != EINPROGRESS ) )
		return dcc_abort( df, "Connecting to %s:%d : %s", 
			inet_ntop( saddr->ss_family, 
				saddr->ss_family == AF_INET ? 
				    ( void* ) &( ( struct sockaddr_in *) saddr )->sin_addr.s_addr :
				    ( void* ) &( ( struct sockaddr_in6 *) saddr )->sin6_addr.s6_addr,
				ipaddr, 
				sizeof( ipaddr ) ),
			ntohs( saddr->ss_family == AF_INET ?
			    ( ( struct sockaddr_in *) saddr )->sin_port :
			    ( ( struct sockaddr_in6 *) saddr )->sin6_port ),
			strerror( errno ) );

	ft->status = FT_STATUS_CONNECTING;

	/* watch */
	df->watch_out = b_input_add( df->fd, GAIM_INPUT_WRITE, dccs_recv_proto, df );
	ft->write_request = dccs_recv_write_request;

	df->progress_timeout = b_timeout_add( DCC_MAX_STALL * 1000, dcc_progress, df );

	return TRUE;
}

gboolean dccs_recv_proto( gpointer data, gint fd, b_input_condition cond)
{
	dcc_file_transfer_t *df = data;
	file_transfer_t *ft = df->ft;
	short revents;

	if( !dcc_poll( df, fd, &revents ) )
		return FALSE;
	
	if( ( revents & POLLOUT ) &&
	    ( ft->status & FT_STATUS_CONNECTING ) )
	{
		ft->status = FT_STATUS_TRANSFERRING;
		if ( !dcc_check_maxseg( df, fd ) )
			return FALSE;

		//df->watch_in = b_input_add( df->fd, GAIM_INPUT_READ, dccs_recv_proto, df );

		df->watch_out = 0;
		return FALSE;
	}

	if( revents & POLLIN )
	{
		int ret, done;

		ASSERTSOCKOP( ret = recv( fd, ft->buffer, sizeof( ft->buffer ), 0 ), "Receiving" );

		if( ret == 0 )
			return dcc_abort( df, "Remote end closed connection" );

		df->bytes_sent += ret;

		done = df->bytes_sent >= ft->file_size;

		if( ( ( df->bytes_sent - ft->bytes_transferred ) > DCC_PACKET_SIZE ) ||
		    done )
		{
			int ack, ackret;
			ack = htonl( ft->bytes_transferred = df->bytes_sent );

			ASSERTSOCKOP( ackret = send( fd, &ack, 4, 0 ), "Sending DCC ACK" );
			
			if ( ackret != 4 )
				return dcc_abort( df, "Error sending DCC ACK, sent %d instead of 4 bytes", ackret );
		}
		
		if( !ft->write( df->ft, ft->buffer, ret ) )
			return FALSE;

		if( done )
		{
			closesocket( fd );
			dcc_finish( ft );

			df->watch_in = 0;
			return FALSE;
		}

		df->watch_in = 0;
		return FALSE;
	}

	return TRUE;
}

gboolean dccs_recv_write_request( file_transfer_t *ft )
{
	dcc_file_transfer_t *df = ft->priv;

	if( df->watch_in )
		return dcc_abort( df, "BUG: write_request() called while watching" );

	df->watch_in = b_input_add( df->fd, GAIM_INPUT_READ, dccs_recv_proto, df );

	return TRUE;
}

gboolean dccs_send_can_write( gpointer data, gint fd, b_input_condition cond )
{
	struct dcc_file_transfer *df = data;
	df->watch_out = 0;

	df->ft->write_request( df->ft );
	return FALSE;
}

/* 
 * Incoming data.
 * 
 */
gboolean dccs_send_write( file_transfer_t *file, char *data, unsigned int data_len )
{
	dcc_file_transfer_t *df = file->priv;
	int ret;

	receivedchunks++; receiveddata += data_len;

	if( df->watch_out )
		return dcc_abort( df, "BUG: write() called while watching" );

	ASSERTSOCKOP( ret = send( df->fd, data, data_len, 0 ), "Sending data" );

	if( ret == 0 )
		return dcc_abort( df, "Remote end closed connection" );

	/* TODO: this should really not be fatal */
	if( ret < data_len )
		return dcc_abort( df, "send() sent %d instead of %d", ret, data_len );

	df->bytes_sent += ret;

	if( df->bytes_sent < df->ft->file_size )
		df->watch_out = b_input_add( df->fd, GAIM_INPUT_WRITE, dccs_send_can_write, df );

	return TRUE;
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
	
	if( df->progress_timeout )
		b_event_remove( df->progress_timeout );
	
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

/* 
 * DCC SEND <filename> <IP> <port> <filesize>
 *
 * filename can be in "" or not. If it is, " can probably be escaped...
 * IP can be an unsigned int (IPV4) or something else (IPV6)
 * 
 */
file_transfer_t *dcc_request( struct im_connection *ic, char *line )
{
	char *pattern = "SEND"
		" (([^\"][^ ]*)|\"([^\"]|\\\")*\")"
		" (([0-9]*)|([^ ]*))"
		" ([0-9]*)"
		" ([0-9]*)\001";
	regmatch_t pmatch[9];
	regex_t re;
	file_transfer_t *ft;
	dcc_file_transfer_t *df;
	char errbuf[256];
	int regerrcode, gret;

	if( ( regerrcode = regcomp( &re, pattern, REG_EXTENDED ) ) ||
	    ( regerrcode = regexec( &re, line, 9, pmatch, 0 ) ) ) {
		regerror( regerrcode,&re,errbuf,sizeof( errbuf ) );
		imcb_log( ic, 
			  "DCC: error parsing 'DCC SEND': %s, line: %s", 
			  errbuf, line );
		return NULL;
	}

	if( ( pmatch[1].rm_so > 0 ) &&
	    ( pmatch[4].rm_so > 0 ) &&
	    ( pmatch[7].rm_so > 0 ) &&
	    ( pmatch[8].rm_so > 0 ) )
	{
		char *input = g_strdup( line );
		char *filename, *host, *port;
		size_t filesize;
		struct addrinfo hints, *rp;

		/* "filename" or filename */
		if ( pmatch[2].rm_so > 0 )
		{
			input[pmatch[2].rm_eo] = '\0';
			filename = input + pmatch[2].rm_so;
		} else
		{
			input[pmatch[3].rm_eo] = '\0';
			filename = input + pmatch[3].rm_so;
		}
			
		input[pmatch[4].rm_eo] = '\0';

		/* number means ipv4, something else means ipv6 */
		if ( pmatch[5].rm_so > 0 )
		{
			struct in_addr ipaddr = { .s_addr = htonl( atoi( input + pmatch[5].rm_so ) ) };
			host = inet_ntoa( ipaddr );
		} else
		{
			/* Contains non-numbers, hopefully an IPV6 address */
			host = input + pmatch[6].rm_so;
		}

		input[pmatch[7].rm_eo] = '\0';
		input[pmatch[8].rm_eo] = '\0';

		port = input + pmatch[7].rm_so;
		filesize = atoll( input + pmatch[8].rm_so );

		memset( &hints, 0, sizeof ( struct addrinfo ) );
		if ( ( gret = getaddrinfo( host, port, &hints, &rp ) ) )
		{
			g_free( input );
			imcb_log( ic, "DCC: getaddrinfo() failed with %s "
				  "when parsing incoming 'DCC SEND': "
				  "host %s, port %s", 
				  gai_strerror( gret ), host, port );
			return NULL;
		}

		df = dcc_alloc_transfer( filename, filesize, ic );
		ft = df->ft;
		ft->sending = TRUE;
		memcpy( &df->saddr, rp->ai_addr, rp->ai_addrlen );

		freeaddrinfo( rp );
		g_free( input );

		df->ic->irc->file_transfers = g_slist_prepend( df->ic->irc->file_transfers, ft );

		return ft;
	}

	imcb_log( ic, "DCC: couldnt parse 'DCC SEND' line: %s", line );

	return NULL;
}

