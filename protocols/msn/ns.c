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

static void msn_auth_got_passport_id( struct passport_reply *rep );

gboolean msn_ns_connected( gpointer data, gint source, b_input_condition cond )
{
	struct gaim_connection *gc = data;
	struct msn_data *md;
	char s[1024];
	
	if( !g_slist_find( msn_connections, gc ) )
		return FALSE;
	
	if( source == -1 )
	{
		hide_login_progress( gc, "Could not connect to server" );
		signoff( gc );
		return FALSE;
	}
	
	md = gc->proto_data;
	
	if( !md->handler )
	{
		md->handler = g_new0( struct msn_handler_data, 1 );
		md->handler->data = gc;
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
	if( msn_write( gc, s, strlen( s ) ) )
	{
		gc->inpa = b_input_add( md->fd, GAIM_INPUT_READ, msn_ns_callback, gc );
		set_login_progress( gc, 1, "Connected to server, waiting for reply" );
	}
	
	return FALSE;
}

static gboolean msn_ns_callback( gpointer data, gint source, b_input_condition cond )
{
	struct gaim_connection *gc = data;
	struct msn_data *md = gc->proto_data;
	
	if( msn_handler( md->handler ) == -1 ) /* Don't do this on ret == 0, it's already done then. */
	{
		hide_login_progress( gc, "Error while reading from server" );
		signoff( gc );
		
		return FALSE;
	}
	else
		return TRUE;
}

static int msn_ns_command( gpointer data, char **cmd, int num_parts )
{
	struct gaim_connection *gc = data;
	struct msn_data *md = gc->proto_data;
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
			hide_login_progress( gc, "Unsupported protocol" );
			signoff( gc );
			return( 0 );
		}
		
		g_snprintf( buf, sizeof( buf ), "CVR %d 0x0409 mac 10.2.0 ppc macmsgs 3.5.1 macmsgs %s\r\n",
		                                ++md->trId, gc->username );
		return( msn_write( gc, buf, strlen( buf ) ) );
	}
	else if( strcmp( cmd[0], "CVR" ) == 0 )
	{
		/* We don't give a damn about the information we just received */
		g_snprintf( buf, sizeof( buf ), "USR %d TWN I %s\r\n", ++md->trId, gc->username );
		return( msn_write( gc, buf, strlen( buf ) ) );
	}
	else if( strcmp( cmd[0], "XFR" ) == 0 )
	{
		char *server;
		int port;
		
		if( num_parts == 6 && strcmp( cmd[2], "NS" ) == 0 )
		{
			b_event_remove( gc->inpa );
			gc->inpa = 0;
			closesocket( md->fd );
			
			server = strchr( cmd[3], ':' );
			if( !server )
			{
				hide_login_progress_error( gc, "Syntax error" );
				signoff( gc );
				return( 0 );
			}
			*server = 0;
			port = atoi( server + 1 );
			server = cmd[3];
			
			set_login_progress( gc, 1, "Transferring to other server" );
			
			md->fd = proxy_connect( server, port, msn_ns_connected, gc );
		}
		else if( num_parts == 6 && strcmp( cmd[2], "SB" ) == 0 )
		{
			struct msn_switchboard *sb;
			
			server = strchr( cmd[3], ':' );
			if( !server )
			{
				hide_login_progress_error( gc, "Syntax error" );
				signoff( gc );
				return( 0 );
			}
			*server = 0;
			port = atoi( server + 1 );
			server = cmd[3];
			
			if( strcmp( cmd[4], "CKI" ) != 0 )
			{
				hide_login_progress_error( gc, "Unknown authentication method for switchboard" );
				signoff( gc );
				return( 0 );
			}
			
			debug( "Connecting to a new switchboard with key %s", cmd[5] );
			sb = msn_sb_create( gc, server, port, cmd[5], MSN_SB_NEW );
			
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
			hide_login_progress_error( gc, "Syntax error" );
			signoff( gc );
			return( 0 );
		}
	}
	else if( strcmp( cmd[0], "USR" ) == 0 )
	{
		if( num_parts == 5 && strcmp( cmd[2], "TWN" ) == 0 && strcmp( cmd[3], "S" ) == 0 )
		{
			/* Time for some Passport black magic... */
			if( !passport_get_id( msn_auth_got_passport_id, gc, gc->username, gc->password, cmd[4] ) )
			{
				hide_login_progress_error( gc, "Error while contacting Passport server" );
				signoff( gc );
				return( 0 );
			}
		}
		else if( num_parts == 7 && strcmp( cmd[2], "OK" ) == 0 )
		{
			set_t *s;
			
			http_decode( cmd[4] );
			
			strncpy( gc->displayname, cmd[4], sizeof( gc->displayname ) );
			gc->displayname[sizeof(gc->displayname)-1] = 0;
			
			if( ( s = set_find( &gc->acc->set, "display_name" ) ) )
			{
				g_free( s->value );
				s->value = g_strdup( cmd[4] );
			}
			
			set_login_progress( gc, 1, "Authenticated, getting buddy list" );
			
			g_snprintf( buf, sizeof( buf ), "SYN %d 0\r\n", ++md->trId );
			return( msn_write( gc, buf, strlen( buf ) ) );
		}
		else
		{
			hide_login_progress( gc, "Unknown authentication type" );
			signoff( gc );
			return( 0 );
		}
	}
	else if( strcmp( cmd[0], "MSG" ) == 0 )
	{
		if( num_parts != 4 )
		{
			hide_login_progress_error( gc, "Syntax error" );
			signoff( gc );
			return( 0 );
		}
		
		md->handler->msglen = atoi( cmd[3] );
		
		if( md->handler->msglen <= 0 )
		{
			hide_login_progress_error( gc, "Syntax error" );
			signoff( gc );
			return( 0 );
		}
	}
	else if( strcmp( cmd[0], "SYN" ) == 0 )
	{
		if( num_parts == 5 )
		{
			md->buddycount = atoi( cmd[3] );
			md->groupcount = atoi( cmd[4] );
			if( md->groupcount > 0 )
				md->grouplist = g_new0( char *, md->groupcount );
			
			if( !*cmd[3] || md->buddycount == 0 )
				msn_logged_in( gc );
		}
		else
		{
			/* Hrrm... This SYN reply doesn't really look like something we expected.
			   Let's assume everything is okay. */
			
			msn_logged_in( gc );
		}
	}
	else if( strcmp( cmd[0], "LST" ) == 0 )
	{
		int list;
		
		if( num_parts != 4 && num_parts != 5 )
		{
			hide_login_progress( gc, "Syntax error" );
			signoff( gc );
			return( 0 );
		}
		
		http_decode( cmd[2] );
		list = atoi( cmd[3] );
		
		if( list & 1 ) /* FL */
		{
			char *group = NULL;
			int num;
			
			if( cmd[4] != NULL && sscanf( cmd[4], "%d", &num ) == 1 )
				group = md->grouplist[num];
			
			add_buddy( gc, group, cmd[1], cmd[2] );
		}
		if( list & 2 ) /* AL */
		{
			gc->permit = g_slist_append( gc->permit, g_strdup( cmd[1] ) );
		}
		if( list & 4 ) /* BL */
		{
			gc->deny = g_slist_append( gc->deny, g_strdup( cmd[1] ) );
		}
		if( list & 8 ) /* RL */
		{
			if( ( list & 6 ) == 0 )
				msn_buddy_ask( gc, cmd[1], cmd[2] );
		}
		
		if( --md->buddycount == 0 )
		{
			if( gc->flags & OPT_LOGGED_IN )
			{
				serv_got_crap( gc, "Successfully transferred to different server" );
				g_snprintf( buf, sizeof( buf ), "CHG %d %s %d\r\n", ++md->trId, md->away_state->code, 0 );
				return( msn_write( gc, buf, strlen( buf ) ) );
			}
			else
			{
				msn_logged_in( gc );
			}
		}
	}
	else if( strcmp( cmd[0], "LSG" ) == 0 )
	{
		int num;
		
		if( num_parts != 4 )
		{
			hide_login_progress_error( gc, "Syntax error" );
			signoff( gc );
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
			hide_login_progress_error( gc, "Syntax error" );
			signoff( gc );
			return( 0 );
		}
		
		md5_init( &state );
		md5_append( &state, (const md5_byte_t *) cmd[2], strlen( cmd[2] ) );
		md5_append( &state, (const md5_byte_t *) QRY_CODE, strlen( QRY_CODE ) );
		md5_finish( &state, digest );
		
		g_snprintf( buf, sizeof( buf ), "QRY %d %s %d\r\n", ++md->trId, QRY_NAME, 32 );
		for( i = 0; i < 16; i ++ )
			g_snprintf( buf + strlen( buf ), 3, "%02x", digest[i] );
		
		return( msn_write( gc, buf, strlen( buf ) ) );
	}
	else if( strcmp( cmd[0], "ILN" ) == 0 )
	{
		const struct msn_away_state *st;
		
		if( num_parts != 6 )
		{
			hide_login_progress_error( gc, "Syntax error" );
			signoff( gc );
			return( 0 );
		}
		
		http_decode( cmd[4] );
		serv_buddy_rename( gc, cmd[3], cmd[4] );
		
		st = msn_away_state_by_code( cmd[2] );
		if( !st )
		{
			/* FIXME: Warn/Bomb about unknown away state? */
			st = msn_away_state_list;
		}
		
		serv_got_update( gc, cmd[3], 1, 0, 0, 0, st->number, 0 );
	}
	else if( strcmp( cmd[0], "FLN" ) == 0 )
	{
		if( cmd[1] )
			serv_got_update( gc, cmd[1], 0, 0, 0, 0, 0, 0 );
	}
	else if( strcmp( cmd[0], "NLN" ) == 0 )
	{
		const struct msn_away_state *st;
		
		if( num_parts != 5 )
		{
			hide_login_progress_error( gc, "Syntax error" );
			signoff( gc );
			return( 0 );
		}
		
		http_decode( cmd[3] );
		serv_buddy_rename( gc, cmd[2], cmd[3] );
		
		st = msn_away_state_by_code( cmd[1] );
		if( !st )
		{
			/* FIXME: Warn/Bomb about unknown away state? */
			st = msn_away_state_list;
		}
		
		serv_got_update( gc, cmd[2], 1, 0, 0, 0, st->number, 0 );
	}
	else if( strcmp( cmd[0], "RNG" ) == 0 )
	{
		struct msn_switchboard *sb;
		char *server;
		int session, port;
		
		if( num_parts != 7 )
		{
			hide_login_progress_error( gc, "Syntax error" );
			signoff( gc );
			return( 0 );
		}
		
		session = atoi( cmd[1] );
		
		server = strchr( cmd[2], ':' );
		if( !server )
		{
			hide_login_progress_error( gc, "Syntax error" );
			signoff( gc );
			return( 0 );
		}
		*server = 0;
		port = atoi( server + 1 );
		server = cmd[2];
		
		if( strcmp( cmd[3], "CKI" ) != 0 )
		{
			hide_login_progress_error( gc, "Unknown authentication method for switchboard" );
			signoff( gc );
			return( 0 );
		}
		
		debug( "Got a call from %s (session %d). Key = %s", cmd[5], session, cmd[4] );
		
		sb = msn_sb_create( gc, server, port, cmd[4], session );
		sb->who = g_strdup( cmd[5] );
	}
	else if( strcmp( cmd[0], "ADD" ) == 0 )
	{
		if( num_parts == 6 && strcmp( cmd[2], "RL" ) == 0 )
		{
			GSList *l;
			
			http_decode( cmd[5] );
			
			if( strchr( cmd[4], '@' ) == NULL )
			{
				hide_login_progress_error( gc, "Syntax error" );
				signoff( gc );
				return( 0 );
			}
			
			/* We got added by someone. If we don't have this person in permit/deny yet, inform the user. */
			for( l = gc->permit; l; l = l->next )
				if( g_strcasecmp( l->data, cmd[4] ) == 0 )
					return( 1 );
			
			for( l = gc->deny; l; l = l->next )
				if( g_strcasecmp( l->data, cmd[4] ) == 0 )
					return( 1 );
			
			msn_buddy_ask( gc, cmd[4], cmd[5] );
		}
	}
	else if( strcmp( cmd[0], "OUT" ) == 0 )
	{
		if( cmd[1] && strcmp( cmd[1], "OTH" ) == 0 )
		{
			hide_login_progress_error( gc, "Someone else logged in with your account" );
			gc->wants_to_die = 1;
		}
		else if( cmd[1] && strcmp( cmd[1], "SSD" ) == 0 )
		{
			hide_login_progress_error( gc, "Terminating session because of server shutdown" );
		}
		else
		{
			hide_login_progress_error( gc, "Session terminated by remote server (reason unknown)" );
		}
		
		signoff( gc );
		return( 0 );
	}
	else if( strcmp( cmd[0], "REA" ) == 0 )
	{
		if( num_parts != 5 )
		{
			hide_login_progress_error( gc, "Syntax error" );
			signoff( gc );
			return( 0 );
		}
		
		if( g_strcasecmp( cmd[3], gc->username ) == 0 )
		{
			set_t *s;
			
			http_decode( cmd[4] );
			strncpy( gc->displayname, cmd[4], sizeof( gc->displayname ) );
			gc->displayname[sizeof(gc->displayname)-1] = 0;
			
			if( ( s = set_find( &gc->acc->set, "display_name" ) ) )
			{
				g_free( s->value );
				s->value = g_strdup( cmd[4] );
			}
		}
		else
		{
			/* This is not supposed to happen, but let's handle it anyway... */
			http_decode( cmd[4] );
			serv_buddy_rename( gc, cmd[3], cmd[4] );
		}
	}
	else if( strcmp( cmd[0], "IPG" ) == 0 )
	{
		do_error_dialog( gc, "Received IPG command, we don't handle them yet.", "MSN" );
		
		md->handler->msglen = atoi( cmd[1] );
		
		if( md->handler->msglen <= 0 )
		{
			hide_login_progress_error( gc, "Syntax error" );
			signoff( gc );
			return( 0 );
		}
	}
	else if( isdigit( cmd[0][0] ) )
	{
		int num = atoi( cmd[0] );
		const struct msn_status_code *err = msn_status_by_number( num );
		
		g_snprintf( buf, sizeof( buf ), "Error reported by MSN server: %s", err->text );
		do_error_dialog( gc, buf, "MSN" );
		
		if( err->flags & STATUS_FATAL )
		{
			signoff( gc );
			return( 0 );
		}
	}
	else
	{
		debug( "Received unknown command from main server: %s", cmd[0] );
	}
	
	return( 1 );
}

static int msn_ns_message( gpointer data, char *msg, int msglen, char **cmd, int num_parts )
{
	struct gaim_connection *gc = data;
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
						serv_got_crap( gc, "The server is going down for maintenance in %s minutes.", arg1 );
				}
				
				if( arg1 ) g_free( arg1 );
				if( mtype ) g_free( mtype );
			}
			else if( g_strncasecmp( ct, "text/x-msmsgsprofile", 20 ) == 0 )
			{
				/* We don't care about this profile for now... */
			}
			else if( g_strncasecmp( ct, "text/x-msmsgsinitialemailnotification", 37 ) == 0 )
			{
				char *inbox = msn_findheader( body, "Inbox-Unread:", blen );
				char *folders = msn_findheader( body, "Folders-Unread:", blen );
				
				if( inbox && folders )
				{
					serv_got_crap( gc, "INBOX contains %s new messages, plus %s messages in other folders.", inbox, folders );
				}
			}
			else if( g_strncasecmp( ct, "text/x-msmsgsemailnotification", 30 ) == 0 )
			{
				char *from = msn_findheader( body, "From-Addr:", blen );
				char *fromname = msn_findheader( body, "From:", blen );
				
				if( from && fromname )
				{
					serv_got_crap( gc, "Received an e-mail message from %s <%s>.", fromname, from );
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

static void msn_auth_got_passport_id( struct passport_reply *rep )
{
	struct gaim_connection *gc = rep->data;
	struct msn_data *md = gc->proto_data;
	char *key = rep->result;
	char buf[1024];
	
	if( key == NULL )
	{
		char *err;
		
		err = g_strdup_printf( "Error during Passport authentication (%s)",
		                       rep->error_string ? rep->error_string : "Unknown error" );
		
		hide_login_progress( gc, err );
		signoff( gc );
		
		g_free( err );
	}
	else
	{
		g_snprintf( buf, sizeof( buf ), "USR %d TWN S %s\r\n", ++md->trId, key );
		msn_write( gc, buf, strlen( buf ) );
	}
}
