  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2004 Wilmer van der Gaast and others                *
  \********************************************************************/

/* MSN module - Notification server callbacks                           */

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

#include <ctype.h>
#include "nogaim.h"
#include "msn.h"
#include "passport.h"
#include "md5.h"

static gboolean msn_ns_callback( gpointer data, gint source, b_input_condition cond );
static int msn_ns_command( gpointer data, char **cmd, int num_parts );
static int msn_ns_message( gpointer data, char *msg, int msglen, char **cmd, int num_parts );

static void msn_auth_got_passport_token( struct msn_auth_data *mad );

gboolean msn_ns_connected( gpointer data, gint source, b_input_condition cond )
{
	struct im_connection *ic = data;
	struct msn_data *md;
	char s[1024];
	
	if( !g_slist_find( msn_connections, ic ) )
		return FALSE;
	
	if( source == -1 )
	{
		imcb_error( ic, "Could not connect to server" );
		imc_logout( ic, TRUE );
		return FALSE;
	}
	
	md = ic->proto_data;
	
	if( !md->handler )
	{
		md->handler = g_new0( struct msn_handler_data, 1 );
		md->handler->data = ic;
		md->handler->exec_command = msn_ns_command;
		md->handler->exec_message = msn_ns_message;
	}
	else
	{
		if( md->handler->rxq )
			g_free( md->handler->rxq );
		
		md->handler->rxlen = 0;
	}
	
	md->handler->fd = md->fd;
	md->handler->rxq = g_new0( char, 1 );
	
	g_snprintf( s, sizeof( s ), "VER %d MSNP8 CVR0\r\n", ++md->trId );
	if( msn_write( ic, s, strlen( s ) ) )
	{
		ic->inpa = b_input_add( md->fd, B_EV_IO_READ, msn_ns_callback, ic );
		imcb_log( ic, "Connected to server, waiting for reply" );
	}
	
	return FALSE;
}

static gboolean msn_ns_callback( gpointer data, gint source, b_input_condition cond )
{
	struct im_connection *ic = data;
	struct msn_data *md = ic->proto_data;
	
	if( msn_handler( md->handler ) == -1 ) /* Don't do this on ret == 0, it's already done then. */
	{
		imcb_error( ic, "Error while reading from server" );
		imc_logout( ic, TRUE );
		
		return FALSE;
	}
	else
		return TRUE;
}

static int msn_ns_command( gpointer data, char **cmd, int num_parts )
{
	struct im_connection *ic = data;
	struct msn_data *md = ic->proto_data;
	char buf[1024];
	
	if( num_parts == 0 )
	{
		/* Hrrm... Empty command...? Ignore? */
		return( 1 );
	}
	
	if( strcmp( cmd[0], "VER" ) == 0 )
	{
		if( cmd[2] && strncmp( cmd[2], "MSNP8", 5 ) != 0 )
		{
			imcb_error( ic, "Unsupported protocol" );
			imc_logout( ic, FALSE );
			return( 0 );
		}
		
		g_snprintf( buf, sizeof( buf ), "CVR %d 0x0409 mac 10.2.0 ppc macmsgs 3.5.1 macmsgs %s\r\n",
		                                ++md->trId, ic->acc->user );
		return( msn_write( ic, buf, strlen( buf ) ) );
	}
	else if( strcmp( cmd[0], "CVR" ) == 0 )
	{
		/* We don't give a damn about the information we just received */
		g_snprintf( buf, sizeof( buf ), "USR %d TWN I %s\r\n", ++md->trId, ic->acc->user );
		return( msn_write( ic, buf, strlen( buf ) ) );
	}
	else if( strcmp( cmd[0], "XFR" ) == 0 )
	{
		char *server;
		int port;
		
		if( num_parts == 6 && strcmp( cmd[2], "NS" ) == 0 )
		{
			b_event_remove( ic->inpa );
			ic->inpa = 0;
			closesocket( md->fd );
			
			server = strchr( cmd[3], ':' );
			if( !server )
			{
				imcb_error( ic, "Syntax error" );
				imc_logout( ic, TRUE );
				return( 0 );
			}
			*server = 0;
			port = atoi( server + 1 );
			server = cmd[3];
			
			imcb_log( ic, "Transferring to other server" );
			
			md->fd = proxy_connect( server, port, msn_ns_connected, ic );
		}
		else if( num_parts == 6 && strcmp( cmd[2], "SB" ) == 0 )
		{
			struct msn_switchboard *sb;
			
			server = strchr( cmd[3], ':' );
			if( !server )
			{
				imcb_error( ic, "Syntax error" );
				imc_logout( ic, TRUE );
				return( 0 );
			}
			*server = 0;
			port = atoi( server + 1 );
			server = cmd[3];
			
			if( strcmp( cmd[4], "CKI" ) != 0 )
			{
				imcb_error( ic, "Unknown authentication method for switchboard" );
				imc_logout( ic, TRUE );
				return( 0 );
			}
			
			debug( "Connecting to a new switchboard with key %s", cmd[5] );

			if( ( sb = msn_sb_create( ic, server, port, cmd[5], MSN_SB_NEW ) ) == NULL )
			{
				/* Although this isn't strictly fatal for the NS connection, it's
				   definitely something serious (we ran out of file descriptors?). */
				imcb_error( ic, "Could not create new switchboard" );
				imc_logout( ic, TRUE );
				return( 0 );
			}
			
			if( md->msgq )
			{
				struct msn_message *m = md->msgq->data;
				GSList *l;
				
				sb->who = g_strdup( m->who );
				
				/* Move all the messages to the first user in the message
				   queue to the switchboard message queue. */
				l = md->msgq;
				while( l )
				{
					m = l->data;
					l = l->next;
					if( strcmp( m->who, sb->who ) == 0 )
					{
						sb->msgq = g_slist_append( sb->msgq, m );
						md->msgq = g_slist_remove( md->msgq, m );
					}
				}
			}
		}
		else
		{
			imcb_error( ic, "Syntax error" );
			imc_logout( ic, TRUE );
			return( 0 );
		}
	}
	else if( strcmp( cmd[0], "USR" ) == 0 )
	{
		if( num_parts == 5 && strcmp( cmd[2], "TWN" ) == 0 && strcmp( cmd[3], "S" ) == 0 )
		{
			/* Time for some Passport black magic... */
			if( !passport_get_token( msn_auth_got_passport_token, ic, ic->acc->user, ic->acc->pass, cmd[4] ) )
			{
				imcb_error( ic, "Error while contacting Passport server" );
				imc_logout( ic, TRUE );
				return( 0 );
			}
		}
		else if( num_parts >= 7 && strcmp( cmd[2], "OK" ) == 0 )
		{
			set_t *s;
			
			if( num_parts == 7 )
			{
				http_decode( cmd[4] );
				
				strncpy( ic->displayname, cmd[4], sizeof( ic->displayname ) );
				ic->displayname[sizeof(ic->displayname)-1] = 0;
				
				if( ( s = set_find( &ic->acc->set, "display_name" ) ) )
				{
					g_free( s->value );
					s->value = g_strdup( cmd[4] );
				}
			}
			else
			{
				imcb_log( ic, "Warning: Friendly name in server response was corrupted" );
			}
			
			imcb_log( ic, "Authenticated, getting buddy list" );
			
			g_snprintf( buf, sizeof( buf ), "SYN %d 0\r\n", ++md->trId );
			return( msn_write( ic, buf, strlen( buf ) ) );
		}
		else
		{
			imcb_error( ic, "Unknown authentication type" );
			imc_logout( ic, FALSE );
			return( 0 );
		}
	}
	else if( strcmp( cmd[0], "MSG" ) == 0 )
	{
		if( num_parts != 4 )
		{
			imcb_error( ic, "Syntax error" );
			imc_logout( ic, TRUE );
			return( 0 );
		}
		
		md->handler->msglen = atoi( cmd[3] );
		
		if( md->handler->msglen <= 0 )
		{
			imcb_error( ic, "Syntax error" );
			imc_logout( ic, TRUE );
			return( 0 );
		}
	}
	else if( strcmp( cmd[0], "SYN" ) == 0 )
	{
		if( num_parts == 5 )
		{
			int i, groupcount;
			
			groupcount = atoi( cmd[4] );
			if( groupcount > 0 )
			{
				/* valgrind says this is leaking memory, I'm guessing
				   that this happens during server redirects. */
				if( md->grouplist )
				{
					for( i = 0; i < md->groupcount; i ++ )
						g_free( md->grouplist[i] );
					g_free( md->grouplist );
				}
				
				md->groupcount = groupcount;
				md->grouplist = g_new0( char *, md->groupcount );
			}
			
			md->buddycount = atoi( cmd[3] );
			if( !*cmd[3] || md->buddycount == 0 )
				msn_logged_in( ic );
		}
		else
		{
			/* Hrrm... This SYN reply doesn't really look like something we expected.
			   Let's assume everything is okay. */
			
			msn_logged_in( ic );
		}
	}
	else if( strcmp( cmd[0], "LST" ) == 0 )
	{
		int list;
		
		if( num_parts != 4 && num_parts != 5 )
		{
			imcb_error( ic, "Syntax error" );
			imc_logout( ic, TRUE );
			return( 0 );
		}
		
		http_decode( cmd[2] );
		list = atoi( cmd[3] );
		
		if( list & 1 ) /* FL */
		{
			char *group = NULL;
			int num;
			
			if( cmd[4] != NULL && sscanf( cmd[4], "%d", &num ) == 1 && num < md->groupcount )
				group = md->grouplist[num];
			
			imcb_add_buddy( ic, cmd[1], group );
			imcb_rename_buddy( ic, cmd[1], cmd[2] );
		}
		if( list & 2 ) /* AL */
		{
			ic->permit = g_slist_append( ic->permit, g_strdup( cmd[1] ) );
		}
		if( list & 4 ) /* BL */
		{
			ic->deny = g_slist_append( ic->deny, g_strdup( cmd[1] ) );
		}
		if( list & 8 ) /* RL */
		{
			if( ( list & 6 ) == 0 )
				msn_buddy_ask( ic, cmd[1], cmd[2] );
		}
		
		if( --md->buddycount == 0 )
		{
			if( ic->flags & OPT_LOGGED_IN )
			{
				imcb_log( ic, "Successfully transferred to different server" );
				g_snprintf( buf, sizeof( buf ), "CHG %d %s %d\r\n", ++md->trId, md->away_state->code, 0 );
				return( msn_write( ic, buf, strlen( buf ) ) );
			}
			else
			{
				msn_logged_in( ic );
			}
		}
	}
	else if( strcmp( cmd[0], "LSG" ) == 0 )
	{
		int num;
		
		if( num_parts != 4 )
		{
			imcb_error( ic, "Syntax error" );
			imc_logout( ic, TRUE );
			return( 0 );
		}
		
		http_decode( cmd[2] );
		num = atoi( cmd[1] );
		
		if( num < md->groupcount )
			md->grouplist[num] = g_strdup( cmd[2] );
	}
	else if( strcmp( cmd[0], "CHL" ) == 0 )
	{
		md5_state_t state;
		md5_byte_t digest[16];
		int i;
		
		if( num_parts != 3 )
		{
			imcb_error( ic, "Syntax error" );
			imc_logout( ic, TRUE );
			return( 0 );
		}
		
		md5_init( &state );
		md5_append( &state, (const md5_byte_t *) cmd[2], strlen( cmd[2] ) );
		md5_append( &state, (const md5_byte_t *) QRY_CODE, strlen( QRY_CODE ) );
		md5_finish( &state, digest );
		
		g_snprintf( buf, sizeof( buf ), "QRY %d %s %d\r\n", ++md->trId, QRY_NAME, 32 );
		for( i = 0; i < 16; i ++ )
			g_snprintf( buf + strlen( buf ), 3, "%02x", digest[i] );
		
		return( msn_write( ic, buf, strlen( buf ) ) );
	}
	else if( strcmp( cmd[0], "ILN" ) == 0 )
	{
		const struct msn_away_state *st;
		
		if( num_parts != 6 )
		{
			imcb_error( ic, "Syntax error" );
			imc_logout( ic, TRUE );
			return( 0 );
		}
		
		http_decode( cmd[4] );
		imcb_rename_buddy( ic, cmd[3], cmd[4] );
		
		st = msn_away_state_by_code( cmd[2] );
		if( !st )
		{
			/* FIXME: Warn/Bomb about unknown away state? */
			st = msn_away_state_list + 1;
		}
		
		imcb_buddy_status( ic, cmd[3], OPT_LOGGED_IN | 
		                   ( st != msn_away_state_list ? OPT_AWAY : 0 ),
		                   st->name, NULL );
	}
	else if( strcmp( cmd[0], "FLN" ) == 0 )
	{
		if( cmd[1] )
			imcb_buddy_status( ic, cmd[1], 0, NULL, NULL );
	}
	else if( strcmp( cmd[0], "NLN" ) == 0 )
	{
		const struct msn_away_state *st;
		
		if( num_parts != 5 )
		{
			imcb_error( ic, "Syntax error" );
			imc_logout( ic, TRUE );
			return( 0 );
		}
		
		http_decode( cmd[3] );
		imcb_rename_buddy( ic, cmd[2], cmd[3] );
		
		st = msn_away_state_by_code( cmd[1] );
		if( !st )
		{
			/* FIXME: Warn/Bomb about unknown away state? */
			st = msn_away_state_list + 1;
		}
		
		imcb_buddy_status( ic, cmd[2], OPT_LOGGED_IN | 
		                   ( st != msn_away_state_list ? OPT_AWAY : 0 ),
		                   st->name, NULL );
	}
	else if( strcmp( cmd[0], "RNG" ) == 0 )
	{
		struct msn_switchboard *sb;
		char *server;
		int session, port;
		
		if( num_parts != 7 )
		{
			imcb_error( ic, "Syntax error" );
			imc_logout( ic, TRUE );
			return( 0 );
		}
		
		session = atoi( cmd[1] );
		
		server = strchr( cmd[2], ':' );
		if( !server )
		{
			imcb_error( ic, "Syntax error" );
			imc_logout( ic, TRUE );
			return( 0 );
		}
		*server = 0;
		port = atoi( server + 1 );
		server = cmd[2];
		
		if( strcmp( cmd[3], "CKI" ) != 0 )
		{
			imcb_error( ic, "Unknown authentication method for switchboard" );
			imc_logout( ic, TRUE );
			return( 0 );
		}
		
		debug( "Got a call from %s (session %d). Key = %s", cmd[5], session, cmd[4] );
		
		if( ( sb = msn_sb_create( ic, server, port, cmd[4], session ) ) == NULL )
		{
			/* Although this isn't strictly fatal for the NS connection, it's
			   definitely something serious (we ran out of file descriptors?). */
			imcb_error( ic, "Could not create new switchboard" );
			imc_logout( ic, TRUE );
			return( 0 );
		}
		else
		{
			sb->who = g_strdup( cmd[5] );
		}
	}
	else if( strcmp( cmd[0], "ADD" ) == 0 )
	{
		if( num_parts == 6 && strcmp( cmd[2], "RL" ) == 0 )
		{
			GSList *l;
			
			http_decode( cmd[5] );
			
			if( strchr( cmd[4], '@' ) == NULL )
			{
				imcb_error( ic, "Syntax error" );
				imc_logout( ic, TRUE );
				return 0;
			}
			
			/* We got added by someone. If we don't have this
			   person in permit/deny yet, inform the user. */
			for( l = ic->permit; l; l = l->next )
				if( g_strcasecmp( l->data, cmd[4] ) == 0 )
					return 1;
			
			for( l = ic->deny; l; l = l->next )
				if( g_strcasecmp( l->data, cmd[4] ) == 0 )
					return 1;
			
			msn_buddy_ask( ic, cmd[4], cmd[5] );
		}
		else if( num_parts >= 6 && strcmp( cmd[2], "FL" ) == 0 )
		{
			http_decode( cmd[5] );
			imcb_add_buddy( ic, cmd[4], NULL );
			imcb_rename_buddy( ic, cmd[4], cmd[5] );
		}
	}
	else if( strcmp( cmd[0], "OUT" ) == 0 )
	{
		int allow_reconnect = TRUE;
		
		if( cmd[1] && strcmp( cmd[1], "OTH" ) == 0 )
		{
			imcb_error( ic, "Someone else logged in with your account" );
			allow_reconnect = FALSE;
		}
		else if( cmd[1] && strcmp( cmd[1], "SSD" ) == 0 )
		{
			imcb_error( ic, "Terminating session because of server shutdown" );
		}
		else
		{
			imcb_error( ic, "Session terminated by remote server (reason unknown)" );
		}
		
		imc_logout( ic, allow_reconnect );
		return( 0 );
	}
	else if( strcmp( cmd[0], "REA" ) == 0 )
	{
		if( num_parts != 5 )
		{
			imcb_error( ic, "Syntax error" );
			imc_logout( ic, TRUE );
			return( 0 );
		}
		
		if( g_strcasecmp( cmd[3], ic->acc->user ) == 0 )
		{
			set_t *s;
			
			http_decode( cmd[4] );
			strncpy( ic->displayname, cmd[4], sizeof( ic->displayname ) );
			ic->displayname[sizeof(ic->displayname)-1] = 0;
			
			if( ( s = set_find( &ic->acc->set, "display_name" ) ) )
			{
				g_free( s->value );
				s->value = g_strdup( cmd[4] );
			}
		}
		else
		{
			/* This is not supposed to happen, but let's handle it anyway... */
			http_decode( cmd[4] );
			imcb_rename_buddy( ic, cmd[3], cmd[4] );
		}
	}
	else if( strcmp( cmd[0], "IPG" ) == 0 )
	{
		imcb_error( ic, "Received IPG command, we don't handle them yet." );
		
		md->handler->msglen = atoi( cmd[1] );
		
		if( md->handler->msglen <= 0 )
		{
			imcb_error( ic, "Syntax error" );
			imc_logout( ic, TRUE );
			return( 0 );
		}
	}
	else if( isdigit( cmd[0][0] ) )
	{
		int num = atoi( cmd[0] );
		const struct msn_status_code *err = msn_status_by_number( num );
		
		imcb_error( ic, "Error reported by MSN server: %s", err->text );
		
		if( err->flags & STATUS_FATAL )
		{
			imc_logout( ic, TRUE );
			return( 0 );
		}
	}
	else
	{
		/* debug( "Received unknown command from main server: %s", cmd[0] ); */
	}
	
	return( 1 );
}

static int msn_ns_message( gpointer data, char *msg, int msglen, char **cmd, int num_parts )
{
	struct im_connection *ic = data;
	char *body;
	int blen = 0;
	
	if( !num_parts )
		return( 1 );
	
	if( ( body = strstr( msg, "\r\n\r\n" ) ) )
	{
		body += 4;
		blen = msglen - ( body - msg );
	}
	
	if( strcmp( cmd[0], "MSG" ) == 0 )
	{
		if( g_strcasecmp( cmd[1], "Hotmail" ) == 0 )
		{
			char *ct = msn_findheader( msg, "Content-Type:", msglen );
			
			if( !ct )
				return( 1 );
			
			if( g_strncasecmp( ct, "application/x-msmsgssystemmessage", 33 ) == 0 )
			{
				char *mtype;
				char *arg1;
				
				if( !body )
					return( 1 );
				
				mtype = msn_findheader( body, "Type:", blen );
				arg1 = msn_findheader( body, "Arg1:", blen );
				
				if( mtype && strcmp( mtype, "1" ) == 0 )
				{
					if( arg1 )
						imcb_log( ic, "The server is going down for maintenance in %s minutes.", arg1 );
				}
				
				g_free( arg1 );
				g_free( mtype );
			}
			else if( g_strncasecmp( ct, "text/x-msmsgsprofile", 20 ) == 0 )
			{
				/* We don't care about this profile for now... */
			}
			else if( g_strncasecmp( ct, "text/x-msmsgsinitialemailnotification", 37 ) == 0 )
			{
				if( set_getbool( &ic->acc->set, "mail_notifications" ) )
				{
					char *inbox = msn_findheader( body, "Inbox-Unread:", blen );
					char *folders = msn_findheader( body, "Folders-Unread:", blen );

					if( inbox && folders )
						imcb_log( ic, "INBOX contains %s new messages, plus %s messages in other folders.", inbox, folders );
					
					g_free( inbox );
					g_free( folders );
				}
			}
			else if( g_strncasecmp( ct, "text/x-msmsgsemailnotification", 30 ) == 0 )
			{
				if( set_getbool( &ic->acc->set, "mail_notifications" ) )
				{
					char *from = msn_findheader( body, "From-Addr:", blen );
					char *fromname = msn_findheader( body, "From:", blen );
					
					if( from && fromname )
						imcb_log( ic, "Received an e-mail message from %s <%s>.", fromname, from );

					g_free( from );
					g_free( fromname );
				}
			}
			else if( g_strncasecmp( ct, "text/x-msmsgsactivemailnotification", 35 ) == 0 )
			{
				/* Sorry, but this one really is *USELESS* */
			}
			else
			{
				debug( "Can't handle %s packet from notification server", ct );
			}
			
			g_free( ct );
		}
	}
	
	return( 1 );
}

static void msn_auth_got_passport_token( struct msn_auth_data *mad )
{
	struct im_connection *ic = mad->data;
	struct msn_data *md;
	
	/* Dead connection? */
	if( g_slist_find( msn_connections, ic ) == NULL )
		return;
	
	md = ic->proto_data;
	if( mad->token )
	{
		char buf[1024];
		
		g_snprintf( buf, sizeof( buf ), "USR %d TWN S %s\r\n", ++md->trId, mad->token );
		msn_write( ic, buf, strlen( buf ) );
	}
	else
	{
		imcb_error( ic, "Error during Passport authentication: %s", mad->error );
		imc_logout( ic, TRUE );
	}
}
