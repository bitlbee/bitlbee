  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2012 Wilmer van der Gaast and others                *
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
#include "md5.h"
#include "soap.h"
#include "invitation.h"

static gboolean msn_sb_callback( gpointer data, gint source, b_input_condition cond );
static int msn_sb_command( struct msn_handler_data *handler, char **cmd, int num_parts );
static int msn_sb_message( struct msn_handler_data *handler, char *msg, int msglen, char **cmd, int num_parts );

int msn_sb_write( struct msn_switchboard *sb, const char *fmt, ... )
{
	va_list params;
	char *out;
	size_t len;
	int st;
	
	va_start( params, fmt );
	out = g_strdup_vprintf( fmt, params );
	va_end( params );
	
	if( getenv( "BITLBEE_DEBUG" ) )
		fprintf( stderr, "->SB%d:%s\n", sb->fd, out );
	
	len = strlen( out );
	st = write( sb->fd, out, len );
	g_free( out );
	if( st != len )
	{
		msn_sb_destroy( sb );
		return 0;
	}
	
	return 1;
}

int msn_sb_write_msg( struct im_connection *ic, struct msn_message *m )
{
	struct msn_data *md = ic->proto_data;
	struct msn_switchboard *sb;

	/* FIXME: *CHECK* the reliability of using spare sb's! */
	if( ( sb = msn_sb_spare( ic ) ) )
	{
		debug( "Trying to use a spare switchboard to message %s", m->who );
		
		sb->who = g_strdup( m->who );
		if( msn_sb_write( sb, "CAL %d %s\r\n", ++sb->trId, m->who ) )
		{
			/* He/She should join the switchboard soon, let's queue the message. */
			sb->msgq = g_slist_append( sb->msgq, m );
			return( 1 );
		}
	}
	
	debug( "Creating a new switchboard to message %s", m->who );
	
	/* If we reach this line, there was no spare switchboard, so let's make one. */
	if( !msn_ns_write( ic, -1, "XFR %d SB\r\n", ++md->trId ) )
	{
		g_free( m->who );
		g_free( m->text );
		g_free( m );
		
		return( 0 );
	}
	
	/* And queue the message to md. We'll pick it up when the switchboard comes up. */
	md->msgq = g_slist_append( md->msgq, m );
	
	/* FIXME: If the switchboard creation fails, the message will not be sent. */
	
	return( 1 );
}

struct msn_switchboard *msn_sb_create( struct im_connection *ic, char *host, int port, char *key, int session )
{
	struct msn_data *md = ic->proto_data;
	struct msn_switchboard *sb = g_new0( struct msn_switchboard, 1 );
	
	sb->fd = proxy_connect( host, port, msn_sb_connected, sb );
	if( sb->fd < 0 )
	{
		g_free( sb );
		return( NULL );
	}
	
	sb->ic = ic;
	sb->key = g_strdup( key );
	sb->session = session;
	
	msn_switchboards = g_slist_append( msn_switchboards, sb );
	md->switchboards = g_slist_append( md->switchboards, sb );
	
	return( sb );
}

struct msn_switchboard *msn_sb_by_handle( struct im_connection *ic, const char *handle )
{
	struct msn_data *md = ic->proto_data;
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

struct msn_switchboard *msn_sb_by_chat( struct groupchat *c )
{
	struct msn_data *md = c->ic->proto_data;
	struct msn_switchboard *sb;
	GSList *l;
	
	for( l = md->switchboards; l; l = l->next )
	{
		sb = l->data;
		if( sb->chat == c )
			return( sb );
	}
	
	return( NULL );
}

struct msn_switchboard *msn_sb_spare( struct im_connection *ic )
{
	struct msn_data *md = ic->proto_data;
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
		char *buf;
		int i, j;
		
		/* Build the message. Convert LF to CR-LF for normal messages. */
		if( strcmp( text, TYPING_NOTIFICATION_MESSAGE ) == 0 )
		{
			i = strlen( MSN_TYPING_HEADERS ) + strlen( sb->ic->acc->user );
			buf = g_new0( char, i );
			i = g_snprintf( buf, i, MSN_TYPING_HEADERS, sb->ic->acc->user );
		}
		else if( strcmp( text, NUDGE_MESSAGE ) == 0 )
		{
			buf = g_strdup( MSN_NUDGE_HEADERS );
			i = strlen( buf );
		}
		else if( strcmp( text, SB_KEEPALIVE_MESSAGE ) == 0 )
		{
			buf = g_strdup( MSN_SB_KEEPALIVE_HEADERS );
			i = strlen( buf );
		}
		else if( strncmp( text, MSN_INVITE_HEADERS, sizeof( MSN_INVITE_HEADERS ) - 1 ) == 0 ) 
		{
			buf = g_strdup( text );
			i = strlen( buf );
		}
		else
		{
			buf = g_new0( char, sizeof( MSN_MESSAGE_HEADERS ) + strlen( text ) * 2 + 1 );
			i = strlen( MSN_MESSAGE_HEADERS );
			
			strcpy( buf, MSN_MESSAGE_HEADERS );
			for( j = 0; text[j]; j ++ )
			{
				if( text[j] == '\n' )
					buf[i++] = '\r';
				
				buf[i++] = text[j];
			}
		}
		
		/* Build the final packet (MSG command + the message). */
		if( msn_sb_write( sb, "MSG %d N %d\r\n%s", ++sb->trId, i, buf ) )
		{
			g_free( buf );
			return 1;
		}
		else
		{
			g_free( buf );
			return 0;
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

struct groupchat *msn_sb_to_chat( struct msn_switchboard *sb )
{
	struct im_connection *ic = sb->ic;
	struct groupchat *c = NULL;
	char buf[1024];
	
	/* Create the groupchat structure. */
	g_snprintf( buf, sizeof( buf ), "MSN groupchat session %d", sb->session );
	if( sb->who )
		c = bee_chat_by_title( ic->bee, ic, sb->who );
	if( c && !msn_sb_by_chat( c ) )
		sb->chat = c;
	else
		sb->chat = imcb_chat_new( ic, buf );
	
	/* Populate the channel. */
	if( sb->who ) imcb_chat_add_buddy( sb->chat, sb->who );
	imcb_chat_add_buddy( sb->chat, ic->acc->user );
	
	/* And make sure the switchboard doesn't look like a regular chat anymore. */
	if( sb->who )
	{
		g_free( sb->who );
		sb->who = NULL;
	}
	
	return sb->chat;
}

void msn_sb_destroy( struct msn_switchboard *sb )
{
	struct im_connection *ic = sb->ic;
	struct msn_data *md = ic->proto_data;
	
	debug( "Destroying switchboard: %s", sb->who ? sb->who : sb->key ? sb->key : "" );
	
	msn_msgq_purge( ic, &sb->msgq );
	msn_sb_stop_keepalives( sb );
	
	if( sb->key ) g_free( sb->key );
	if( sb->who ) g_free( sb->who );
	
	if( sb->chat )
	{
		imcb_chat_free( sb->chat );
	}
	
	if( sb->handler )
	{
		if( sb->handler->rxq ) g_free( sb->handler->rxq );
		if( sb->handler->cmd_text ) g_free( sb->handler->cmd_text );
		g_free( sb->handler );
	}
	
	if( sb->inp ) b_event_remove( sb->inp );
	closesocket( sb->fd );
	
	msn_switchboards = g_slist_remove( msn_switchboards, sb );
	md->switchboards = g_slist_remove( md->switchboards, sb );
	g_free( sb );
}

gboolean msn_sb_connected( gpointer data, gint source, b_input_condition cond )
{
	struct msn_switchboard *sb = data;
	struct im_connection *ic;
	struct msn_data *md;
	char buf[1024];
	
	/* Are we still alive? */
	if( !g_slist_find( msn_switchboards, sb ) )
		return FALSE;
	
	ic = sb->ic;
	md = ic->proto_data;
	
	if( source != sb->fd )
	{
		debug( "Error %d while connecting to switchboard server", 1 );
		msn_sb_destroy( sb );
		return FALSE;
	}
	
	/* Prepare the callback */
	sb->handler = g_new0( struct msn_handler_data, 1 );
	sb->handler->fd = sb->fd;
	sb->handler->rxq = g_new0( char, 1 );
	sb->handler->data = sb;
	sb->handler->exec_command = msn_sb_command;
	sb->handler->exec_message = msn_sb_message;
	
	if( sb->session == MSN_SB_NEW )
		g_snprintf( buf, sizeof( buf ), "USR %d %s;{%s} %s\r\n", ++sb->trId, ic->acc->user, md->uuid, sb->key );
	else
		g_snprintf( buf, sizeof( buf ), "ANS %d %s;{%s} %s %d\r\n", ++sb->trId, ic->acc->user, md->uuid, sb->key, sb->session );
	
	if( msn_sb_write( sb, "%s", buf ) )
		sb->inp = b_input_add( sb->fd, B_EV_IO_READ, msn_sb_callback, sb );
	else
		debug( "Error %d while connecting to switchboard server", 2 );
	
	return FALSE;
}

static gboolean msn_sb_callback( gpointer data, gint source, b_input_condition cond )
{
	struct msn_switchboard *sb = data;
	struct im_connection *ic = sb->ic;
	struct msn_data *md = ic->proto_data;
	
	if( msn_handler( sb->handler ) != -1 )
		return TRUE;
	
	if( sb->msgq != NULL )
	{
		time_t now = time( NULL );
		
		if( now - md->first_sb_failure > 600 )
		{
			/* It's not really the first one, but the start of this "series".
			   With this, the warning below will be shown only if this happens
			   at least three times in ten minutes. This algorithm isn't
			   perfect, but for this purpose it will do. */
			md->first_sb_failure = now;
			md->sb_failures = 0;
		}
		
		debug( "Error: Switchboard died" );
		if( ++ md->sb_failures >= 3 )
			imcb_log( ic, "Warning: Many switchboard failures on MSN connection. "
			              "There might be problems delivering your messages." );
		
		if( md->msgq == NULL )
		{
			md->msgq = sb->msgq;
		}
		else
		{
			GSList *l;
			
			for( l = md->msgq; l->next; l = l->next );
			l->next = sb->msgq;
		}
		sb->msgq = NULL;
		
		debug( "Moved queued messages back to the main queue, "
		       "creating a new switchboard to retry." );
		if( !msn_ns_write( ic, -1, "XFR %d SB\r\n", ++md->trId ) )
			return FALSE;
	}
	
	msn_sb_destroy( sb );
	return FALSE;
}

static int msn_sb_command( struct msn_handler_data *handler, char **cmd, int num_parts )
{
	struct msn_switchboard *sb = handler->data;
	struct im_connection *ic = sb->ic;
	
	if( !num_parts )
	{
		/* Hrrm... Empty command...? Ignore? */
		return( 1 );
	}
	
	if( strcmp( cmd[0], "XFR" ) == 0 )
	{
		imcb_error( ic, "Received an XFR from a switchboard server, unable to comply! This is likely to be a bug, please report it!" );
		imc_logout( ic, TRUE );
		return( 0 );
	}
	else if( strcmp( cmd[0], "USR" ) == 0 )
	{
		if( num_parts < 5 )
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
			return msn_sb_write( sb, "CAL %d %s\r\n", ++sb->trId, sb->who );
		else
			debug( "Just created a switchboard, but I don't know what to do with it." );
	}
	else if( strcmp( cmd[0], "IRO" ) == 0 )
	{
		int num, tot;
		
		if( num_parts < 6 )
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
			
			/* For as much as I understand this MPOP stuff now, a
			   switchboard has two (or more) roster entries per
			   participant. One "bare JID" and one JID;UUID. Ignore
			   the latter. */
			if( !strchr( cmd[4], ';' ) )
			{
				/* HACK: Since even 1:1 chats now have >2 participants
				   (ourselves included) it gets hard to tell them apart
				   from rooms. Let's hope this is enough: */
				if( sb->chat == NULL && num != tot )
				{
					g_snprintf( buf, sizeof( buf ), "MSN groupchat session %d", sb->session );
					sb->chat = imcb_chat_new( ic, buf );
					
					g_free( sb->who );
					sb->who = NULL;
				}
				
				if( sb->chat )
					imcb_chat_add_buddy( sb->chat, cmd[4] );
			}
			
			/* We have the full roster, start showing the channel to
			   the user. */
			if( num == tot && sb->chat )
			{
				imcb_chat_add_buddy( sb->chat, ic->acc->user );
			}
		}
	}
	else if( strcmp( cmd[0], "ANS" ) == 0 )
	{
		if( num_parts < 3 )
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
		
		msn_sb_start_keepalives( sb, FALSE );
	}
	else if( strcmp( cmd[0], "CAL" ) == 0 )
	{
		if( num_parts < 4 || !isdigit( cmd[3][0] ) )
		{
			msn_sb_destroy( sb );
			return( 0 );
		}
		
		sb->session = atoi( cmd[3] );
	}
	else if( strcmp( cmd[0], "JOI" ) == 0 )
	{
		if( num_parts < 3 )
		{
			msn_sb_destroy( sb );
			return( 0 );
		}
		
		/* See IRO above. Handle "bare JIDs" only. */
		if( strchr( cmd[1], ';' ) )
			return 1;
		
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
			
			msn_sb_start_keepalives( sb, FALSE );
			
			return( st );
		}
		else if( strcmp( cmd[1], ic->acc->user ) == 0 )
		{
			/* Well, gee thanks. Thanks for letting me know I've arrived.. */
		}
		else if( sb->who )
		{
			debug( "Converting chat with %s to a groupchat because %s joined the session.", sb->who, cmd[1] );
			
			/* This SB is a one-to-one chat right now, but someone else is joining. */
			msn_sb_to_chat( sb );
			
			imcb_chat_add_buddy( sb->chat, cmd[1] );
		}
		else if( sb->chat )
		{
			imcb_chat_add_buddy( sb->chat, cmd[1] );
			sb->ready = 1;
		}
		else
		{
			/* PANIC! */
		}
	}
	else if( strcmp( cmd[0], "MSG" ) == 0 )
	{
		if( num_parts < 4 )
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
	else if( strcmp( cmd[0], "NAK" ) == 0 )
	{
		if( sb->who )
		{
			imcb_log( ic, "The MSN servers could not deliver one of your messages to %s.", sb->who );
		}
		else
		{
			imcb_log( ic, "The MSN servers could not deliver one of your groupchat messages to all participants." );
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
			msn_sb_stop_keepalives( sb );
			
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
			imcb_chat_remove_buddy( sb->chat, cmd[1], "" );
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
		
		/* If the person is offline, send an offline message instead,
		   and don't report an error. */
		if( num == 217 )
			msn_ns_oim_send_queue( ic, &sb->msgq );
		else
			imcb_error( ic, "Error reported by switchboard server: %s", err->text );
		
		if( err->flags & STATUS_SB_FATAL )
		{
			msn_sb_destroy( sb );
			return 0;
		}
		else if( err->flags & STATUS_FATAL )
		{
			imc_logout( ic, TRUE );
			return 0;
		}
		else if( err->flags & STATUS_SB_IM_SPARE )
		{
			if( sb->who )
			{
				/* Apparently some invitation failed. We might want to use this
				   board later, so keep it as a spare. */
				g_free( sb->who );
				sb->who = NULL;
				
				/* Also clear the msgq, otherwise someone else might get them. */
				msn_msgq_purge( ic, &sb->msgq );
			}
			
			/* Do NOT return 0 here, we want to keep this sb. */
		}
	}
	else
	{
		/* debug( "Received unknown command from switchboard server: %s", cmd[0] ); */
	}
	
	return( 1 );
}

static int msn_sb_message( struct msn_handler_data *handler, char *msg, int msglen, char **cmd, int num_parts )
{
	struct msn_switchboard *sb = handler->data;
	struct im_connection *ic = sb->ic;
	char *body;
	
	if( !num_parts )
		return( 1 );
	
	if( ( body = strstr( msg, "\r\n\r\n" ) ) )
		body += 4;
	
	if( strcmp( cmd[0], "MSG" ) == 0 )
	{
		char *ct = get_rfc822_header( msg, "Content-Type:", msglen );
		
		if( !ct )
			return( 1 );
		
		if( g_strncasecmp( ct, "text/plain", 10 ) == 0 )
		{
			g_free( ct );
			
			if( !body )
				return( 1 );
			
			if( sb->who )
			{
				imcb_buddy_msg( ic, cmd[1], body, 0, 0 );
			}
			else if( sb->chat )
			{
				imcb_chat_msg( sb->chat, cmd[1], body, 0, 0 );
			}
			else
			{
				/* PANIC! */
			}
		}
#if 0
		// Disable MSN ft support for now.
		else if( g_strncasecmp( ct, "text/x-msmsgsinvite", 19 ) == 0 )
		{
			char *command = get_rfc822_header( body, "Invitation-Command:", blen );
			char *cookie = get_rfc822_header( body, "Invitation-Cookie:", blen );
			unsigned int icookie;
			
			g_free( ct );
			
			/* Every invite should have both a Command and Cookie header */
			if( !command || !cookie ) {
				g_free( command );
				g_free( cookie );
				imcb_log( ic, "Warning: No command or cookie from %s", sb->who );
				return 1;
			}
			
			icookie = strtoul( cookie, NULL, 10 );
			g_free( cookie );
			
			if( g_strncasecmp( command, "INVITE", 6 ) == 0 ) {
				msn_invitation_invite( sb, cmd[1], icookie, body, blen );
			} else if( g_strncasecmp( command, "ACCEPT", 6 ) == 0 ) {
				msn_invitation_accept( sb, cmd[1], icookie, body, blen );
			} else if( g_strncasecmp( command, "CANCEL", 6 ) == 0 ) {
				msn_invitation_cancel( sb, cmd[1], icookie, body, blen );
			} else {
				imcb_log( ic, "Warning: Received invalid invitation with "
						"command %s from %s", command, sb->who );
			}
			
			g_free( command );
		}
#endif
		else if( g_strncasecmp( ct, "application/x-msnmsgrp2p", 24 ) == 0 ) 
		{
			/* Not currently implemented. Don't warn about it since
			   this seems to be used for avatars now. */
			g_free( ct );
		}
		else if( g_strncasecmp( ct, "text/x-msmsgscontrol", 20 ) == 0 )
		{
			char *who = get_rfc822_header( msg, "TypingUser:", msglen );
			
			if( who )
			{
				imcb_buddy_typing( ic, who, OPT_TYPING );
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

static gboolean msn_sb_keepalive( gpointer data, gint source, b_input_condition cond )
{
	struct msn_switchboard *sb = data;
	return sb->ready && msn_sb_sendmessage( sb, SB_KEEPALIVE_MESSAGE );
}

void msn_sb_start_keepalives( struct msn_switchboard *sb, gboolean initial )
{
	bee_user_t *bu;
	
	if( sb && sb->who && sb->keepalive == 0 &&
	    ( bu = bee_user_by_handle( sb->ic->bee, sb->ic, sb->who ) ) &&
	    !( bu->flags & BEE_USER_ONLINE ) &&
	    set_getbool( &sb->ic->acc->set, "switchboard_keepalives" ) )
	{
		if( initial )
			msn_sb_keepalive( sb, 0, 0 );
		
		sb->keepalive = b_timeout_add( 20000, msn_sb_keepalive, sb );
	}
}

void msn_sb_stop_keepalives( struct msn_switchboard *sb )
{
	if( sb && sb->keepalive > 0 )
	{
		b_event_remove( sb->keepalive );
		sb->keepalive = 0;
	}
}
