  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2005 Wilmer van der Gaast and others                *
  \********************************************************************/

/* MSN module - Switchboard server callbacks and utilities              */

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

static void msn_sb_callback( gpointer data, gint source, GaimInputCondition cond );
static int msn_sb_command( gpointer data, char **cmd, int num_parts );
static int msn_sb_message( gpointer data, char *msg, int msglen, char **cmd, int num_parts );

int msn_sb_write( struct msn_switchboard *sb, char *s, int len )
{
	int st;
	
	st = write( sb->fd, s, len );
	if( st != len )
	{
		msn_sb_destroy( sb );
		return( 0 );
	}
	
	return( 1 );
}

struct msn_switchboard *msn_sb_create( struct gaim_connection *gc, char *host, int port, char *key, int session )
{
	struct msn_data *md = gc->proto_data;
	struct msn_switchboard *sb = g_new0( struct msn_switchboard, 1 );
	
	sb->fd = proxy_connect( host, port, msn_sb_connected, sb );
	if( sb->fd < 0 )
	{
		g_free( sb );
		return( NULL );
	}
	
	sb->gc = gc;
	sb->key = g_strdup( key );
	sb->session = session;
	
	msn_switchboards = g_slist_append( msn_switchboards, sb );
	md->switchboards = g_slist_append( md->switchboards, sb );
	
	return( sb );
}

struct msn_switchboard *msn_sb_by_handle( struct gaim_connection *gc, char *handle )
{
	struct msn_data *md = gc->proto_data;
	struct msn_switchboard *sb;
	GSList *l;
	
	for( l = md->switchboards; l; l = l->next )
	{
		sb = l->data;
		if( sb->who && strcmp( sb->who, handle ) == 0 )
			return( sb );
	}
	
	return( NULL );
}

struct msn_switchboard *msn_sb_by_id( struct gaim_connection *gc, int id )
{
	struct msn_data *md = gc->proto_data;
	struct msn_switchboard *sb;
	GSList *l;
	
	for( l = md->switchboards; l; l = l->next )
	{
		sb = l->data;
		if( sb->chat && sb->chat->id == id )
			return( sb );
	}
	
	return( NULL );
}

struct msn_switchboard *msn_sb_spare( struct gaim_connection *gc )
{
	struct msn_data *md = gc->proto_data;
	struct msn_switchboard *sb;
	GSList *l;
	
	for( l = md->switchboards; l; l = l->next )
	{
		sb = l->data;
		if( !sb->who && !sb->chat )
			return( sb );
	}
	
	return( NULL );
}

int msn_sb_sendmessage( struct msn_switchboard *sb, char *text )
{
	if( sb->ready )
	{
		char cmd[1024], *buf;
		int i, j;
		
		if( strcmp( text, TYPING_NOTIFICATION_MESSAGE ) != 0 )
		{
			buf = g_new0( char, sizeof( MSN_MESSAGE_HEADERS ) + strlen( text ) * 2 );
			i = strlen( MSN_MESSAGE_HEADERS );
			
			strcpy( buf, MSN_MESSAGE_HEADERS );
			for( j = 0; text[j]; j ++ )
			{
				if( text[j] == '\n' )
					buf[i++] = '\r';
				
				buf[i++] = text[j];
			}
		}
		else
		{
			i = strlen( MSN_TYPING_HEADERS ) + strlen( sb->gc->username );
			buf = g_new0( char, strlen( MSN_TYPING_HEADERS ) + strlen( sb->gc->username ) );
			i = g_snprintf( buf, i, MSN_TYPING_HEADERS, sb->gc->username );
		}
		
		g_snprintf( cmd, sizeof( cmd ), "MSG %d N %d\r\n", ++sb->trId, i );
		if( msn_sb_write( sb, cmd, strlen( cmd ) ) && msn_sb_write( sb, buf, i ) )
		{
			g_free( buf );
			return( 1 );
		}
		else
		{
			g_free( buf );
			return( 0 );
		}
	}
	else if( sb->who )
	{
		struct msn_message *m = g_new0( struct msn_message, 1 );
		
		m->who = g_strdup( "" );
		m->text = g_strdup( text );
		sb->msgq = g_slist_append( sb->msgq, m );
		
		return( 1 );
	}
	else
	{
		return( 0 );
	}
}

void msn_sb_to_chat( struct msn_switchboard *sb )
{
	struct gaim_connection *gc = sb->gc;
	char buf[1024];
	
	/* Create the groupchat structure. */
	g_snprintf( buf, sizeof( buf ), "MSN groupchat session %d", sb->session );
	sb->chat = serv_got_joined_chat( gc, ++msn_chat_id, buf );
	
	/* Populate the channel. */
	if( sb->who ) add_chat_buddy( sb->chat, sb->who );
	add_chat_buddy( sb->chat, gc->username );
	
	/* And make sure the switchboard doesn't look like a regular chat anymore. */
	if( sb->who )
	{
		g_free( sb->who );
		sb->who = NULL;
	}
}

void msn_sb_destroy( struct msn_switchboard *sb )
{
	struct gaim_connection *gc = sb->gc;
	struct msn_data *md = gc->proto_data;
	
	debug( "Destroying switchboard: %s", sb->who ? sb->who : sb->key ? sb->key : "" );
	
	if( sb->key ) g_free( sb->key );
	if( sb->who ) g_free( sb->who );
	
	if( sb->msgq )
	{
		struct msn_message *m;
		GSList *l;
		
		for( l = sb->msgq; l; l = l->next )
		{
			m = l->data;

			g_free( m->who );
			g_free( m->text );
			g_free( m );
		}
		g_slist_free( sb->msgq );
		
		serv_got_crap( gc, "Warning: Closing down MSN switchboard connection with "
		                   "unsent message to %s, you'll have to resend it.",
		                   m->who ? m->who : "(unknown)" );
	}
	
	if( sb->chat )
	{
		serv_got_chat_left( gc, sb->chat->id );
	}
	
	if( sb->handler )
	{
		if( sb->handler->rxq ) g_free( sb->handler->rxq );
		if( sb->handler->cmd_text ) g_free( sb->handler->cmd_text );
		g_free( sb->handler );
	}
	
	if( sb->inp ) gaim_input_remove( sb->inp );
	closesocket( sb->fd );
	
	msn_switchboards = g_slist_remove( msn_switchboards, sb );
	md->switchboards = g_slist_remove( md->switchboards, sb );
	g_free( sb );
}

void msn_sb_connected( gpointer data, gint source, GaimInputCondition cond )
{
	struct msn_switchboard *sb = data;
	struct gaim_connection *gc;
	struct msn_data *md;
	char buf[1024];
	
	/* Are we still alive? */
	if( !g_slist_find( msn_switchboards, sb ) )
		return;
	
	gc = sb->gc;
	md = gc->proto_data;
	
	if( source != sb->fd )
	{
		debug( "ERROR %d while connecting to switchboard server", 1 );
		msn_sb_destroy( sb );
		return;
	}
	
	/* Prepare the callback */
	sb->handler = g_new0( struct msn_handler_data, 1 );
	sb->handler->fd = sb->fd;
	sb->handler->rxq = g_new0( char, 1 );
	sb->handler->data = sb;
	sb->handler->exec_command = msn_sb_command;
	sb->handler->exec_message = msn_sb_message;
	
	if( sb->session == MSN_SB_NEW )
		g_snprintf( buf, sizeof( buf ), "USR %d %s %s\r\n", ++sb->trId, gc->username, sb->key );
	else
		g_snprintf( buf, sizeof( buf ), "ANS %d %s %s %d\r\n", ++sb->trId, gc->username, sb->key, sb->session );
	
	if( msn_sb_write( sb, buf, strlen( buf ) ) )
		sb->inp = gaim_input_add( sb->fd, GAIM_INPUT_READ, msn_sb_callback, sb );
	else
		debug( "ERROR %d while connecting to switchboard server", 2 );
}

static void msn_sb_callback( gpointer data, gint source, GaimInputCondition cond )
{
	struct msn_switchboard *sb = data;
	
	if( msn_handler( sb->handler ) == -1 )
	{
		debug( "ERROR: Switchboard died" );
		msn_sb_destroy( sb );
	}
}

static int msn_sb_command( gpointer data, char **cmd, int num_parts )
{
	struct msn_switchboard *sb = data;
	struct gaim_connection *gc = sb->gc;
	char buf[1024];
	
	if( !num_parts )
	{
		/* Hrrm... Empty command...? Ignore? */
		return( 1 );
	}
	
	if( strcmp( cmd[0], "XFR" ) == 0 )
	{
		hide_login_progress_error( gc, "Received an XFR from a switchboard server, unable to comply! This is likely to be a bug, please report it!" );
		signoff( gc );
		return( 0 );
	}
	else if( strcmp( cmd[0], "USR" ) == 0 )
	{
		if( num_parts != 5 )
		{
			msn_sb_destroy( sb );
			return( 0 );
		}
		
		if( strcmp( cmd[2], "OK" ) != 0 )
		{
			msn_sb_destroy( sb );
			return( 0 );
		}
		
		if( sb->who )
		{
			g_snprintf( buf, sizeof( buf ), "CAL %d %s\r\n", ++sb->trId, sb->who );
			return( msn_sb_write( sb, buf, strlen( buf ) ) );
		}
		else
		{
			debug( "Just created a switchboard, but I don't know what to do with it." );
		}
	}
	else if( strcmp( cmd[0], "IRO" ) == 0 )
	{
		int num, tot;
		
		if( num_parts != 6 )
		{
			msn_sb_destroy( sb );
			return( 0 );
		}
		
		num = atoi( cmd[2] );
		tot = atoi( cmd[3] );
		
		if( tot <= 0 )
		{
			msn_sb_destroy( sb );
			return( 0 );
		}
		else if( tot > 1 )
		{
			char buf[1024];
			
			if( num == 1 )
			{
				g_snprintf( buf, sizeof( buf ), "MSN groupchat session %d", sb->session );
				sb->chat = serv_got_joined_chat( gc, ++msn_chat_id, buf );
				
				g_free( sb->who );
				sb->who = NULL;
			}
			
			add_chat_buddy( sb->chat, cmd[4] );
			
			if( num == tot )
			{
				add_chat_buddy( sb->chat, gc->username );
			}
		}
	}
	else if( strcmp( cmd[0], "ANS" ) == 0 )
	{
		if( num_parts != 3 )
		{
			msn_sb_destroy( sb );
			return( 0 );
		}
		
		if( strcmp( cmd[2], "OK" ) != 0 )
		{
			debug( "Switchboard server sent a negative ANS reply" );
			msn_sb_destroy( sb );
			return( 0 );
		}
		
		sb->ready = 1;
	}
	else if( strcmp( cmd[0], "CAL" ) == 0 )
	{
		if( num_parts != 4 || !isdigit( cmd[3][0] ) )
		{
			msn_sb_destroy( sb );
			return( 0 );
		}
		
		sb->session = atoi( cmd[3] );
	}
	else if( strcmp( cmd[0], "JOI" ) == 0 )
	{
		if( num_parts != 3 )
		{
			msn_sb_destroy( sb );
			return( 0 );
		}
		
		if( sb->who && g_strcasecmp( cmd[1], sb->who ) == 0 )
		{
			/* The user we wanted to talk to is finally there, let's send the queued messages then. */
			struct msn_message *m;
			GSList *l;
			int st = 1;
			
			debug( "%s arrived in the switchboard session, now sending queued message(s)", cmd[1] );
			
			/* Without this, sendmessage() will put everything back on the queue... */
			sb->ready = 1;
			
			while( ( l = sb->msgq ) )
			{
				m = l->data;
				if( st )
				{
					/* This hack is meant to convert a regular new chat into a groupchat */
					if( strcmp( m->text, GROUPCHAT_SWITCHBOARD_MESSAGE ) == 0 )
						msn_sb_to_chat( sb );
					else
						st = msn_sb_sendmessage( sb, m->text );
				}
				g_free( m->text );
				g_free( m->who );
				g_free( m );
				
				sb->msgq = g_slist_remove( sb->msgq, m );
			}
			
			return( st );
		}
		else if( sb->who )
		{
			debug( "Converting chat with %s to a groupchat because %s joined the session.", sb->who, cmd[1] );
			
			/* This SB is a one-to-one chat right now, but someone else is joining. */
			msn_sb_to_chat( sb );
			
			add_chat_buddy( sb->chat, cmd[1] );
		}
		else if( sb->chat )
		{
			add_chat_buddy( sb->chat, cmd[1] );
			sb->ready = 1;
		}
		else
		{
			/* PANIC! */
		}
	}
	else if( strcmp( cmd[0], "MSG" ) == 0 )
	{
		if( num_parts != 4 )
		{
			msn_sb_destroy( sb );
			return( 0 );
		}
		
		sb->handler->msglen = atoi( cmd[3] );
		
		if( sb->handler->msglen <= 0 )
		{
			debug( "Received a corrupted message on the switchboard, the switchboard will be closed" );
			msn_sb_destroy( sb );
			return( 0 );
		}
	}
	else if( strcmp( cmd[0], "BYE" ) == 0 )
	{
		if( num_parts < 2 )
		{
			msn_sb_destroy( sb );
			return( 0 );
		}
		
		/* if( cmd[2] && *cmd[2] == '1' ) -=> Chat is being cleaned up because of idleness */
		
		if( sb->who )
		{
			/* This is a single-person chat, and the other person is leaving. */
			g_free( sb->who );
			sb->who = NULL;
			sb->ready = 0;
			
			debug( "Person %s left the one-to-one switchboard connection. Keeping it around as a spare...", cmd[1] );
			
			/* We could clean up the switchboard now, but keeping it around
			   as a spare for a next conversation sounds more sane to me.
			   The server will clean it up when it's idle for too long. */
		}
		else if( sb->chat )
		{
			remove_chat_buddy( sb->chat, cmd[1], "" );
		}
		else
		{
			/* PANIC! */
		}
	}
	else if( isdigit( cmd[0][0] ) )
	{
		int num = atoi( cmd[0] );
		const struct msn_status_code *err = msn_status_by_number( num );
		
		g_snprintf( buf, sizeof( buf ), "Error reported by switchboard server: %s", err->text );
		do_error_dialog( gc, buf, "MSN" );
		
		if( err->flags & STATUS_SB_FATAL )
		{
			msn_sb_destroy( sb );
			return( 0 );
		}
		if( err->flags & STATUS_FATAL )
		{
			signoff( gc );
			return( 0 );
		}
		if( err->flags & STATUS_SB_IM_SPARE )
		{
			if( sb->who )
			{
				struct msn_message *m;
				GSList *l;
				
				/* Apparently some invitation failed. We might want to use this
				   board later, so keep it as a spare. */
				g_free( sb->who );
				sb->who = NULL;
				
				/* Also clear the msgq, otherwise someone else might get them. */
				for( l = sb->msgq; l; l = l->next )
				{
					m = l->data;
					g_free( m->who );
					g_free( m->text );
					g_free( m );
				}
				g_slist_free( sb->msgq );
				sb->msgq = NULL;
			}
		}
	}
	else
	{
		debug( "Received unknown command from switchboard server: %s", cmd[0] );
	}
	
	return( 1 );
}

static int msn_sb_message( gpointer data, char *msg, int msglen, char **cmd, int num_parts )
{
	struct msn_switchboard *sb = data;
	struct gaim_connection *gc = sb->gc;
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
		char *ct = msn_findheader( msg, "Content-Type:", msglen );
		
		if( !ct )
			return( 1 );
		
		if( g_strncasecmp( ct, "text/plain", 10 ) == 0 )
		{
			g_free( ct );
			
			if( !body )
				return( 1 );
			
			if( sb->who )
			{
				serv_got_im( gc, cmd[1], body, 0, 0, blen );
			}
			else if( sb->chat )
			{
				serv_got_chat_in( gc, sb->chat->id, cmd[1], 0, body, 0 );
			}
			else
			{
				/* PANIC! */
			}
		}
		else if( g_strncasecmp( ct, "text/x-msmsgsinvite", 19 ) == 0 )
		{
			char *itype = msn_findheader( body, "Application-GUID:", blen );
			char buf[1024];
			
			g_free( ct );
			
			*buf = 0;
			
			if( !itype )
				return( 1 );
			
			/* File transfer. */
			if( strcmp( itype, "{5D3E02AB-6190-11d3-BBBB-00C04F795683}" ) == 0 )
			{
				char *name = msn_findheader( body, "Application-File:", blen );
				char *size = msn_findheader( body, "Application-FileSize:", blen );
				
				if( name && size )
				{
					g_snprintf( buf, sizeof( buf ), "<< \x02""BitlBee\x02"" - Filetransfer: `%s', %s bytes >>\n"
					            "Filetransfers are not supported by BitlBee for now...", name, size );
				}
				else
				{
					strcpy( buf, "<< \x02""BitlBee\x02"" - Corrupted MSN filetransfer invitation message >>" );
				}
				
				if( name ) g_free( name );
				if( size ) g_free( size );
			}
			else
			{
				char *iname = msn_findheader( body, "Application-Name:", blen );
				
				g_snprintf( buf, sizeof( buf ), "<< \x02""BitlBee\x02"" - Unknown MSN invitation - %s (%s) >>",
				                                itype, iname ? iname : "no name" );
				
				if( iname ) g_free( iname );
			}
			
			g_free( itype );
			
			if( !*buf )
				return( 1 );
			
			if( sb->who )
			{
				serv_got_im( gc, cmd[1], buf, 0, 0, strlen( buf ) );
			}
			else if( sb->chat )
			{
				serv_got_chat_in( gc, sb->chat->id, cmd[1], 0, buf, 0 );
			}
			else
			{
				/* PANIC! */
			}
		}
		else if( g_strncasecmp( ct, "text/x-msmsgscontrol", 20 ) == 0 )
		{
			char *who = msn_findheader( msg, "TypingUser:", msglen );
			
			if( who )
			{
				serv_got_typing( gc, who, 5, 1 );
				g_free( who );
			}
			
			g_free( ct );
		}
		else
		{
			g_free( ct );
		}
	}
	
	return( 1 );
}
