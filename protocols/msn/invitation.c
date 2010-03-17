/********************************************************************\
* BitlBee -- An IRC to other IM-networks gateway                     *
*                                                                    *
* Copyright 2008 Uli Meis					     *
* Copyright 2006 Marijn Kruisselbrink and others                     *
\********************************************************************/

/* MSN module - File transfer support             */

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

#include "bitlbee.h"
#include "invitation.h"
#include "msn.h"
#include "lib/ftutil.h"

#ifdef debug
#undef debug
#endif
#define debug(msg...) log_message( LOGLVL_INFO, msg )

static void msn_ftp_free( file_transfer_t *file );
static void msn_ftpr_accept( file_transfer_t *file );
static void msn_ftp_finished( file_transfer_t *file );
static void msn_ftp_canceled( file_transfer_t *file, char *reason );
static gboolean msn_ftpr_write_request( file_transfer_t *file );

static gboolean msn_ftp_connected( gpointer data, gint fd, b_input_condition cond );
static gboolean msn_ftp_read( gpointer data, gint fd, b_input_condition cond );
gboolean msn_ftps_write( file_transfer_t *file, char *buffer, unsigned int len );

/*
 * Vararg wrapper for imcb_file_canceled().
 */
gboolean msn_ftp_abort( file_transfer_t *file, char *format, ... )
{
        va_list params;
        va_start( params, format );
        char error[128];

        if( vsnprintf( error, 128, format, params ) < 0 )
                sprintf( error, "internal error parsing error string (BUG)" );
        va_end( params );
	imcb_file_canceled( file, error );
	return FALSE;
}

/* very useful */
#define ASSERTSOCKOP(op, msg) \
	if( (op) == -1 ) \
		return msn_ftp_abort( file , msg ": %s", strerror( errno ) );

void msn_ftp_invitation_cmd( struct im_connection *ic, char *who, int cookie, char *icmd,
			     char *trailer )
{
	struct msn_message *m = g_new0( struct msn_message, 1 );
	
	m->text = g_strdup_printf( "%s"
		    "Invitation-Command: %s\r\n"
		    "Invitation-Cookie: %u\r\n"
		    "%s",
		    MSN_INVITE_HEADERS,
		    icmd,
		    cookie,
		    trailer);
	
	m->who = g_strdup( who );

	msn_sb_write_msg( ic, m );
}

void msn_ftp_cancel_invite( struct im_connection *ic, char *who,  int cookie, char *code )
{
	char buf[64];

	g_snprintf( buf, sizeof( buf ), "Cancel-Code: %s\r\n", code );
	msn_ftp_invitation_cmd( ic, who, cookie, "CANCEL", buf );
}

void msn_ftp_transfer_request( struct im_connection *ic, file_transfer_t *file, char *who )
{
	unsigned int cookie = time( NULL ); /* TODO: randomize */
	char buf[2048];

	msn_filetransfer_t *msn_file = g_new0( msn_filetransfer_t, 1 );
	file->data = msn_file;
	file->free = msn_ftp_free;
	file->canceled = msn_ftp_canceled;
	file->write = msn_ftps_write;
	msn_file->md = ic->proto_data;
	msn_file->invite_cookie = cookie;
	msn_file->handle = g_strdup( who );
	msn_file->dcc = file;
	msn_file->md->filetransfers = g_slist_prepend( msn_file->md->filetransfers, msn_file->dcc );
	msn_file->fd = -1;
	msn_file->sbufpos = 3;

	g_snprintf( buf, sizeof( buf ), 
		"Application-Name: File Transfer\r\n"
		"Application-GUID: {5D3E02AB-6190-11d3-BBBB-00C04F795683}\r\n"
		"Application-File: %s\r\n"
		"Application-FileSize: %zd\r\n",
		file->file_name,
		file->file_size);

	msn_ftp_invitation_cmd( msn_file->md->ic, msn_file->handle, cookie, "INVITE", buf );

	imcb_file_recv_start( file );
}

void msn_invitation_invite( struct msn_switchboard *sb, char *handle, unsigned int cookie, char *body, int blen )
{
	char *itype = msn_findheader( body, "Application-GUID:", blen );
	char *name, *size, *invitecookie, *reject = NULL;
	user_t *u;
	size_t isize;
	file_transfer_t *file;
	
	if( !itype || strcmp( itype, "{5D3E02AB-6190-11d3-BBBB-00C04F795683}" ) != 0 ) {
		/* Don't know what that is - don't care */
		char *iname = msn_findheader( body, "Application-Name:", blen );
		imcb_log( sb->ic, "Received unknown MSN invitation %s (%s) from %s",
			  itype ? : "with no GUID", iname ? iname : "no application name", handle );
		g_free( iname );
		reject = "REJECT_NOT_INSTALLED";
	} else if ( 
		!( name = msn_findheader( body, "Application-File:", blen )) || 
		!( size = msn_findheader( body, "Application-FileSize:", blen )) || 
		!( invitecookie = msn_findheader( body, "Invitation-Cookie:", blen)) ||
		!( isize = atoll( size ) ) ) { 
		imcb_log( sb->ic, "Received corrupted transfer request from %s"
			  "(name=%s, size=%s, invitecookie=%s)",
			  handle, name, size, invitecookie );
		reject = "REJECT";
	} else if ( !( u = user_findhandle( sb->ic, handle ) ) ) {
		imcb_log( sb->ic, "Error in parsing transfer request, User '%s'"
			  "is not in contact list", handle );
		reject = "REJECT";
	} else if ( !( file = imcb_file_send_start( sb->ic, handle, name, isize ) ) ) {
		imcb_log( sb->ic, "Error initiating transfer for request from %s for %s",
			  handle, name );
		reject = "REJECT";
	} else {
		msn_filetransfer_t *msn_file = g_new0( msn_filetransfer_t, 1 );
		file->data = msn_file;
		file->accept = msn_ftpr_accept;
		file->free = msn_ftp_free;
		file->finished = msn_ftp_finished;
		file->canceled = msn_ftp_canceled;
		file->write_request = msn_ftpr_write_request;
		msn_file->md = sb->ic->proto_data;
		msn_file->invite_cookie = cookie;
		msn_file->handle = g_strdup( handle );
		msn_file->dcc = file;
		msn_file->md->filetransfers = g_slist_prepend( msn_file->md->filetransfers, msn_file->dcc );
		msn_file->fd = -1;
	}

	if( reject )
		msn_ftp_cancel_invite( sb->ic, sb->who, cookie, reject );

	g_free( name );
	g_free( size );
	g_free( invitecookie );
	g_free( itype );
}

msn_filetransfer_t* msn_find_filetransfer( struct msn_data *md, unsigned int cookie, char *handle )
{
	GSList *l;
	
	for( l = md->filetransfers; l; l = l->next ) {
		msn_filetransfer_t *file = ( (file_transfer_t*) l->data )->data;
		if( file->invite_cookie == cookie && strcmp( handle, file->handle ) == 0 ) {
			return file;
		}
	}
	return NULL;
}

gboolean msn_ftps_connected( gpointer data, gint fd, b_input_condition cond )
{
	file_transfer_t *file = data;
	msn_filetransfer_t *msn_file = file->data;
	struct sockaddr_storage clt_addr;
	socklen_t ssize = sizeof( clt_addr );
	
	debug( "Connected to MSNFTP client" );
	
	ASSERTSOCKOP( msn_file->fd = accept( fd, (struct sockaddr *) &clt_addr, &ssize ), "Accepting connection" );

	closesocket( fd );
	fd = msn_file->fd;
	sock_make_nonblocking( fd );

	msn_file->r_event_id = b_input_add( fd, GAIM_INPUT_READ, msn_ftp_read, file );

	return FALSE;
}

void msn_invitations_accept( msn_filetransfer_t *msn_file, struct msn_switchboard *sb, char *handle, unsigned int cookie, char *body, int blen )
{
	file_transfer_t *file = msn_file->dcc;
	char buf[1024];
	unsigned int acookie = time ( NULL );
	char host[HOST_NAME_MAX+1];
	char port[6];
	char *errmsg;

	msn_file->auth_cookie = acookie;

	if( ( msn_file->fd = ft_listen( NULL, host, port, FALSE, &errmsg ) ) == -1 ) {
		msn_ftp_abort( file, "Failed to listen locally, check your ft_listen setting in bitlbee.conf: %s", errmsg );
		return;
	}

	msn_file->r_event_id = b_input_add( msn_file->fd, GAIM_INPUT_READ, msn_ftps_connected, file );

	g_snprintf( buf, sizeof( buf ),
		    "IP-Address: %s\r\n"
		    "Port: %s\r\n"
		    "AuthCookie: %d\r\n"
		    "Launch-Application: FALSE\r\n"
		    "Request-Data: IP-Address:\r\n\r\n",
		    host,
		    port,
		    msn_file->auth_cookie );

	msn_ftp_invitation_cmd( msn_file->md->ic, handle, msn_file->invite_cookie, "ACCEPT", buf );
}

void msn_invitationr_accept( msn_filetransfer_t *msn_file, struct msn_switchboard *sb, char *handle, unsigned int cookie, char *body, int blen ) {
	file_transfer_t *file = msn_file->dcc;
	char *authcookie, *ip, *port;

	if( !( authcookie = msn_findheader( body, "AuthCookie:", blen ) ) ||
	    !( ip = msn_findheader( body, "IP-Address:", blen ) ) ||
	    !( port = msn_findheader( body, "Port:", blen ) ) ) {
		msn_ftp_abort( file, "Received invalid accept reply" );
	} else if( 
		( msn_file->fd = proxy_connect( ip, atoi( port ), msn_ftp_connected, file ) )
		< 0 ) {
			msn_ftp_abort( file, "Error connecting to MSN client" );
	} else
		msn_file->auth_cookie = strtoul( authcookie, NULL, 10 );

	g_free( authcookie );
	g_free( ip );
	g_free( port );
}

void msn_invitation_accept( struct msn_switchboard *sb, char *handle, unsigned int cookie, char *body, int blen )
{
	msn_filetransfer_t *msn_file = msn_find_filetransfer( sb->ic->proto_data, cookie, handle );
	file_transfer_t *file = msn_file ? msn_file->dcc : NULL;
	
	if( !msn_file  )
		imcb_log( sb->ic, "Received invitation ACCEPT message for unknown invitation (already aborted?)" );
	else if( file->sending ) 
		msn_invitations_accept( msn_file, sb, handle, cookie, body, blen );
	else
		msn_invitationr_accept( msn_file, sb, handle, cookie, body, blen );
}

void msn_invitation_cancel( struct msn_switchboard *sb, char *handle, unsigned int cookie, char *body, int blen )
{
	msn_filetransfer_t *msn_file = msn_find_filetransfer( sb->ic->proto_data, cookie, handle );
	
	if( !msn_file )
		imcb_log( sb->ic, "Received invitation CANCEL message for unknown invitation (already aborted?)" );
	else
		msn_ftp_abort( msn_file->dcc, msn_findheader( body, "Cancel-Code:", blen ) );
}

int msn_ftp_write( file_transfer_t *file, char *format, ... )
{
	msn_filetransfer_t *msn_file = file->data;
	va_list params;
	int st;
	char *s;
	
	va_start( params, format );
	s = g_strdup_vprintf( format, params );
	va_end( params );
	
	st = write( msn_file->fd, s, strlen( s ) );
	if( st != strlen( s ) )
		return msn_ftp_abort( file, "Error sending data over MSNFTP connection: %s",
				strerror( errno ) );
	
	g_free( s );
	return 1;
}

gboolean msn_ftp_connected( gpointer data, gint fd, b_input_condition cond )
{
	file_transfer_t *file = data;
	msn_filetransfer_t *msn_file = file->data;
	
	debug( "Connected to MSNFTP server, starting authentication" );
	if( !msn_ftp_write( file, "VER MSNFTP\r\n" ) ) 
		return FALSE;
	
	sock_make_nonblocking( msn_file->fd );
	msn_file->r_event_id = b_input_add( msn_file->fd, GAIM_INPUT_READ, msn_ftp_read, file );
	
	return FALSE;
}

gboolean msn_ftp_handle_command( file_transfer_t *file, char* line )
{
	msn_filetransfer_t *msn_file = file->data;
	char **cmd = msn_linesplit( line );
	int count = 0;
	if( cmd[0] ) while( cmd[++count] );
	
	if( count < 1 )
		return msn_ftp_abort( file, "Missing command in MSNFTP communication" );
	
	if( strcmp( cmd[0], "VER" ) == 0 ) {
		if( strcmp( cmd[1], "MSNFTP" ) != 0 )
			return msn_ftp_abort( file, "Unsupported filetransfer protocol: %s", cmd[1] );
		if( file->sending )
			msn_ftp_write( file, "VER MSNFTP\r\n" );
		else 
			msn_ftp_write( file, "USR %s %u\r\n", msn_file->md->ic->acc->user, msn_file->auth_cookie );
	} else if( strcmp( cmd[0], "FIL" ) == 0 ) {
		if( strtoul( cmd[1], NULL, 10 ) != file->file_size )
			return msn_ftp_abort( file, "FIL reply contains a different file size than the size in the invitation" );
		msn_ftp_write( file, "TFR\r\n" );
		msn_file->status |= MSN_TRANSFER_RECEIVING;
	} else if( strcmp( cmd[0], "USR" ) == 0 ) {
		if( ( strcmp( cmd[1], msn_file->handle ) != 0 ) ||
		    ( strtoul( cmd[2], NULL, 10 ) != msn_file->auth_cookie ) )
			msn_ftp_abort( file, "Authentication failed. "
				"Expected handle: %s (got %s), cookie: %u (got %s)",
				msn_file->handle, cmd[1],
				msn_file->auth_cookie, cmd[2] );
		msn_ftp_write( file, "FIL %zu\r\n", file->file_size);
	} else if( strcmp( cmd[0], "TFR" ) == 0 ) {
		file->write_request( file );
	} else if( strcmp( cmd[0], "BYE" ) == 0 ) {
		unsigned int retcode = count > 1 ? atoi(cmd[1]) : 1;

		if( ( retcode==16777989 ) || ( retcode==16777987 ) )
			imcb_file_finished( file );
		else if( retcode==2147942405 )
			imcb_file_canceled( file, "Failure: receiver is out of disk space" );
		else if( retcode==2164261682 )
			imcb_file_canceled( file, "Failure: receiver cancelled the transfer" );
		else if( retcode==2164261683 )
			imcb_file_canceled( file, "Failure: sender has cancelled the transfer" );
		else if( retcode==2164261694 )
			imcb_file_canceled( file, "Failure: connection is blocked" );
		else {
			char buf[128];

			sprintf( buf, "Failure: unknown BYE code: %d", retcode);
			imcb_file_canceled( file, buf );
		}
	} else if( strcmp( cmd[0], "CCL" ) == 0 ) {
		imcb_file_canceled( file, "Failure: receiver cancelled the transfer" );
	} else {
		msn_ftp_abort( file, "Received invalid command %s from msn client", cmd[0] );
	}
	return TRUE;
}

gboolean msn_ftp_send( gpointer data, gint fd, b_input_condition cond )
{
	file_transfer_t *file = data;
	msn_filetransfer_t *msn_file = file->data;

	msn_file->w_event_id = 0;

	file->write_request( file );

	return FALSE;
}

/*
 * This should only be called if we can write, so just do it.
 * Add a write watch so we can write more during the next cycle (if possible).
 * This got a bit complicated because (at least) amsn expects packets of size 2045.
 */
gboolean msn_ftps_write( file_transfer_t *file, char *buffer, unsigned int len )
{
	msn_filetransfer_t *msn_file = file->data;
        int ret, overflow;

	/* what we can't send now */
	overflow = msn_file->sbufpos + len - MSNFTP_PSIZE;

	/* append what we can do the send buffer */
	memcpy( msn_file->sbuf + msn_file->sbufpos, buffer, MIN( len, MSNFTP_PSIZE - msn_file->sbufpos ) );
	msn_file->sbufpos += MIN( len, MSNFTP_PSIZE - msn_file->sbufpos );

	/* if we don't have enough for a full packet and there's more wait for it */
	if( ( msn_file->sbufpos < MSNFTP_PSIZE ) && 
	    ( msn_file->data_sent + msn_file->sbufpos - 3 < file->file_size ) ) {
		if( !msn_file->w_event_id )
			msn_file->w_event_id = b_input_add( msn_file->fd, GAIM_INPUT_WRITE, msn_ftp_send, file );
		return TRUE;
	}

	/* Accumulated enough data, lets send something out */

	msn_file->sbuf[0] = 0;
	msn_file->sbuf[1] = ( msn_file->sbufpos - 3 ) & 0xff;
	msn_file->sbuf[2] = ( ( msn_file->sbufpos - 3 ) >> 8 ) & 0xff;

        ASSERTSOCKOP( ret = send( msn_file->fd, msn_file->sbuf, msn_file->sbufpos, 0 ), "Sending" );

        msn_file->data_sent += ret - 3;

        /* TODO: this should really not be fatal */
        if( ret < msn_file->sbufpos )
                return msn_ftp_abort( file, "send() sent %d instead of %d (send buffer full!)", ret, msn_file->sbufpos );

	msn_file->sbufpos = 3;

	if( overflow > 0 ) {
		while( overflow > ( MSNFTP_PSIZE - 3 ) ) {
			if( !msn_ftps_write( file, buffer + len - overflow, MSNFTP_PSIZE - 3 ) )
				return FALSE;
			overflow -= MSNFTP_PSIZE - 3;
		}
		return msn_ftps_write( file, buffer + len - overflow, overflow );
	}

	if( msn_file->data_sent == file->file_size ) {
		if( msn_file->w_event_id ) {
			b_event_remove( msn_file->w_event_id );
			msn_file->w_event_id = 0;
		}
	} else {
		/* we might already be listening if this is data from an overflow */
		if( !msn_file->w_event_id )
			msn_file->w_event_id = b_input_add( msn_file->fd, GAIM_INPUT_WRITE, msn_ftp_send, file );
	}

        return TRUE;
}

/* Binary part of the file transfer protocol */
gboolean msn_ftpr_read( file_transfer_t *file ) 
{
	msn_filetransfer_t *msn_file = file->data;
	int st;
	unsigned char buf[3];

	if( msn_file->data_remaining ) {
		msn_file->r_event_id = 0;

		ASSERTSOCKOP( st = read( msn_file->fd, file->buffer, MIN( sizeof( file->buffer ), msn_file->data_remaining ) ), "Receiving" );

		if( st == 0 )
			return msn_ftp_abort( file, "Remote end closed connection");

		msn_file->data_sent += st;

		msn_file->data_remaining -= st;

		file->write( file, file->buffer, st );

		if( msn_file->data_sent >= file->file_size )
			imcb_file_finished( file );

		return FALSE;
	} else {
		ASSERTSOCKOP( st = read( msn_file->fd, buf, 1 ), "Receiving" );
		if( st == 0 ) {
			return msn_ftp_abort( file, "read returned EOF while reading data header from msn client" );
		} else if( buf[0] == '\r' || buf[0] == '\n' ) {
			debug( "Discarding extraneous newline" );
		} else if( buf[0] != 0 ) {
			msn_ftp_abort( file, "Remote end canceled the transfer");
			/* don't really care about these last 2 (should be 0,0) */
			read( msn_file->fd, buf, 2 );
			return FALSE;
		} else {
			unsigned int size;
			ASSERTSOCKOP( st = read( msn_file->fd, buf, 2 ), "Receiving" );
			if( st < 2 )
				return msn_ftp_abort( file, "read returned EOF while reading data header from msn client" );

			size = buf[0] + ((unsigned int) buf[1] << 8);
			msn_file->data_remaining = size;
		}
	}
	return TRUE;
}

/* Text mode part of the file transfer protocol */
gboolean msn_ftp_txtproto( file_transfer_t *file )
{
	msn_filetransfer_t *msn_file = file->data;
	int i = msn_file->tbufpos, st;
	char *tbuf = msn_file->tbuf;

	ASSERTSOCKOP( st = read( msn_file->fd, 
				 tbuf + msn_file->tbufpos, 
				 sizeof( msn_file->tbuf ) - msn_file->tbufpos ),
				 "Receiving" );

	if( st == 0 )
		return msn_ftp_abort( file, "read returned EOF while reading text from msn client" );

	msn_file->tbufpos += st;

	do {
		for( ;i < msn_file->tbufpos; i++ ) {
			if( tbuf[i] == '\n' || tbuf[i] == '\r' ) {
				tbuf[i] = '\0';
				if( i > 0 )
					msn_ftp_handle_command( file, tbuf );
				else
					while( tbuf[i] == '\n' || tbuf[i] == '\r' ) i++;
				memmove( tbuf, tbuf + i + 1, msn_file->tbufpos - i - 1 );
				msn_file->tbufpos -= i + 1;
				i = 0;
				break;
			}
		}
	} while ( i < msn_file->tbufpos );

	if( msn_file->tbufpos == sizeof( msn_file->tbuf ) )
		return msn_ftp_abort( file, 
				      "Line exceeded %d bytes in text protocol", 
				      sizeof( msn_file->tbuf ) );
	return TRUE;
}

gboolean msn_ftp_read( gpointer data, gint fd, b_input_condition cond )
{
	file_transfer_t *file = data;
	msn_filetransfer_t *msn_file = file->data;
	
	if( msn_file->status & MSN_TRANSFER_RECEIVING )
		return msn_ftpr_read( file );
	else
		return msn_ftp_txtproto( file );
}

void msn_ftp_free( file_transfer_t *file )
{
	msn_filetransfer_t *msn_file = file->data;
	
	if( msn_file->r_event_id )
		b_event_remove( msn_file->r_event_id );

	if( msn_file->w_event_id )
		b_event_remove( msn_file->w_event_id );

	if( msn_file->fd != -1 )
		closesocket( msn_file->fd );

	msn_file->md->filetransfers = g_slist_remove( msn_file->md->filetransfers, msn_file->dcc );
	
	g_free( msn_file->handle );
	
	g_free( msn_file );
}

void msn_ftpr_accept( file_transfer_t *file )
{
	msn_filetransfer_t *msn_file = file->data;

	msn_ftp_invitation_cmd( msn_file->md->ic, msn_file->handle, msn_file->invite_cookie, "ACCEPT", 
				"Launch-Application: FALSE\r\n" 
				"Request-Data: IP-Address:\r\n");
}

void msn_ftp_finished( file_transfer_t *file )
{
	msn_ftp_write( file, "BYE 16777989\r\n" );
}

void msn_ftp_canceled( file_transfer_t *file, char *reason )
{
	msn_filetransfer_t *msn_file = file->data;

	msn_ftp_cancel_invite( msn_file->md->ic, msn_file->handle, 
			       msn_file->invite_cookie, 
			       file->status & FT_STATUS_TRANSFERRING ? 
					"FTTIMEOUT" : 
					"FAIL" );

	imcb_log( msn_file->md->ic, "File transfer aborted: %s", reason );
}

gboolean msn_ftpr_write_request( file_transfer_t *file )
{
	msn_filetransfer_t *msn_file = file->data;
	if( msn_file->r_event_id != 0 ) {
		msn_ftp_abort( file, 
					"BUG in MSN file transfer:"
					"write_request called when"
					"already watching for input" );
		return FALSE;
	}

	msn_file->r_event_id = 
		b_input_add( msn_file->fd, GAIM_INPUT_READ, msn_ftp_read, file );

	return TRUE;
}
