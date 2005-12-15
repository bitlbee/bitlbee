  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2004 Wilmer van der Gaast and others                *
  \********************************************************************/

/* Main file                                                            */

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
#include "commands.h"
#include "protocols/nogaim.h"
#include "help.h"
#include <signal.h>
#include <stdio.h>
#include <errno.h>

gboolean bitlbee_io_new_client( GIOChannel *source, GIOCondition condition, gpointer data )
{
	size_t size = sizeof( struct sockaddr_in );
	struct sockaddr_in conn_info;
	int new_socket = accept( global.listen_socket, (struct sockaddr *) &conn_info, 
		                     &size );
	
	log_message( LOGLVL_INFO, "Creating new connection with fd %d.", new_socket );
	irc_new( new_socket );

	return TRUE;
}
 


int bitlbee_daemon_init()
{
	struct sockaddr_in listen_addr;
	int i;
	GIOChannel *ch;
	
	global.listen_socket = socket( AF_INET, SOCK_STREAM, 0 );
	if( global.listen_socket == -1 )
	{
		log_error( "socket" );
		return( -1 );
	}
	listen_addr.sin_family = AF_INET;
	listen_addr.sin_port = htons( global.conf->port );
	listen_addr.sin_addr.s_addr = inet_addr( global.conf->iface );

	i = bind( global.listen_socket, (struct sockaddr *) &listen_addr, sizeof( struct sockaddr ) );
	if( i == -1 )
	{
		log_error( "bind" );
		return( -1 );
	}

	i = listen( global.listen_socket, 10 );
	if( i == -1 )
	{
		log_error( "listen" );
		return( -1 );
	}

	ch = g_io_channel_unix_new( global.listen_socket );
	g_io_add_watch( ch, G_IO_IN, bitlbee_io_new_client, NULL );

#ifndef _WIN32
	if( !global.conf->nofork )
	{
		i = fork();
		if( i == -1 )
		{
			log_error( "fork" );
			return( -1 );
		}
		else if( i != 0 ) 
			exit( 0 );
		close( 0 );
		close( 1 );
		close( 2 );
		chdir( "/" );
	}
#endif
	
	return( 0 );
}
 
int bitlbee_inetd_init()
{
	if( !irc_new( 0 ) )
		return( 1 );
	
	return( 0 );
}

gboolean bitlbee_io_current_client_read( GIOChannel *source, GIOCondition condition, gpointer data )
{
	irc_t *irc = data;
	char line[513];
	int st;
	
	if( condition & G_IO_ERR || condition & G_IO_HUP )
	{
		irc_free( irc );
		return FALSE;
	}
	
	st = read( irc->fd, line, sizeof( line ) - 1 );
	if( st == 0 )
	{
		irc_free( irc );
		return FALSE;
	}
	else if( st < 0 )
	{
		if( sockerr_again() )
		{
			return TRUE;
		}
		else
		{
			irc_free( irc );
			return FALSE;
		}
	}
	
	line[st] = '\0';
	if( irc->readbuffer == NULL ) 
	{
		irc->readbuffer = g_strdup( line );
	}
	else 
	{
		irc->readbuffer = g_renew( char, irc->readbuffer, strlen( irc->readbuffer ) + strlen ( line ) + 1 );
		strcpy( ( irc->readbuffer + strlen( irc->readbuffer ) ), line );
	}
	
	if( !irc_process( irc ) )
	{
		log_message( LOGLVL_INFO, "Destroying connection with fd %d.", irc->fd );
		irc_free( irc );
		return FALSE;
	} 
	
	/* Very naughty, go read the RFCs! >:) */
	if( irc->readbuffer && ( strlen( irc->readbuffer ) > 1024 ) )
	{
		log_message( LOGLVL_ERROR, "Maximum line length exceeded." );
		irc_free( irc );
		return FALSE;
	}
	
	return TRUE;
}

gboolean bitlbee_io_current_client_write( GIOChannel *source, GIOCondition condition, gpointer data )
{
	irc_t *irc = data;
	int st, size;
	char *temp;
#ifdef FLOOD_SEND
	time_t newtime;
#endif

#ifdef FLOOD_SEND	
	newtime = time( NULL );
	if( ( newtime - irc->oldtime ) > FLOOD_SEND_INTERVAL )
	{
		irc->sentbytes = 0;
		irc->oldtime = newtime;
	}
#endif
	
	if( irc->sendbuffer == NULL )
		return( FALSE );
	
	size = strlen( irc->sendbuffer );
	
#ifdef FLOOD_SEND
	if( ( FLOOD_SEND_BYTES - irc->sentbytes ) > size )
		st = write( irc->fd, irc->sendbuffer, size );
	else
		st = write( irc->fd, irc->sendbuffer, ( FLOOD_SEND_BYTES - irc->sentbytes ) );
#else
	st = write( irc->fd, irc->sendbuffer, size );
#endif
	
	if( st <= 0 )
	{
		if( sockerr_again() )
		{
			return TRUE;
		}
		else
		{
			irc_free( irc );
			return FALSE;
		}
	}
	
#ifdef FLOOD_SEND
	irc->sentbytes += st;
#endif		
	
	if( st == size )
	{
		g_free( irc->sendbuffer );
		irc->sendbuffer = NULL;
		
		irc->w_watch_source_id = 0;
		return( FALSE );
	}
	else
	{
		temp = g_strdup( irc->sendbuffer + st );
		g_free( irc->sendbuffer );
		irc->sendbuffer = temp;
		
		return( TRUE );
	}
}

void bitlbee_shutdown( gpointer data )
{
	/* Try to save data for all active connections (if desired). */
	while( irc_connection_list != NULL )
		irc_free( irc_connection_list->data );
	
	/* We'll only reach this point when not running in inetd mode: */
	g_main_quit( global.loop );
}

int root_command_string( irc_t *irc, user_t *u, char *command, int flags )
{
	char *cmd[IRC_MAX_ARGS];
	char *s;
	int k;
	char q = 0;
	
	memset( cmd, 0, sizeof( cmd ) );
	cmd[0] = command;
	k = 1;
	for( s = command; *s && k < ( IRC_MAX_ARGS - 1 ); s ++ )
		if( *s == ' ' && !q )
		{
			*s = 0;
			while( *++s == ' ' );
			if( *s == '"' || *s == '\'' )
			{
				q = *s;
				s ++;
			}
			if( *s )
			{
				cmd[k++] = s;
				s --;
			}
		}
		else if( *s == q )
		{
			q = *s = 0;
		}
	cmd[k] = NULL;
	
	return( root_command( irc, cmd ) );
}

int root_command( irc_t *irc, char *cmd[] )
{	
	int i;
	
	if( !cmd[0] )
		return( 0 );
	
	for( i = 0; commands[i].command; i++ )
		if( g_strcasecmp( commands[i].command, cmd[0] ) == 0 )
		{
			if( !cmd[commands[i].required_parameters] )
			{
				irc_usermsg( irc, "Not enough parameters given (need %d)", commands[i].required_parameters );
				return( 0 );
			}
			commands[i].execute( irc, cmd );
			return( 1 );
		}
	
	irc_usermsg( irc, "Unknown command: %s. Please use \x02help commands\x02 to get a list of available commands.", cmd[0] );
	
	return( 1 );
}

/* Decode%20a%20file%20name						*/
void http_decode( char *s )
{
	char *t;
	int i, j, k;
	
	t = g_new( char, strlen( s ) + 1 );
	
	for( i = j = 0; s[i]; i ++, j ++ )
	{
		if( s[i] == '%' )
		{
			if( sscanf( s + i + 1, "%2x", &k ) )
			{
				t[j] = k;
				i += 2;
			}
			else
			{
				*t = 0;
				break;
			}
		}
		else
		{
			t[j] = s[i];
		}
	}
	t[j] = 0;
	
	strcpy( s, t );
	g_free( t );
}

/* Warning: This one explodes the string. Worst-cases can make the string 3x its original size! */
/* This fuction is safe, but make sure you call it safely as well! */
void http_encode( char *s )
{
	char *t;
	int i, j;
	
	t = g_strdup( s );
	
	for( i = j = 0; t[i]; i ++, j ++ )
	{
		if( t[i] <= ' ' || ((unsigned char *)t)[i] >= 128 || t[i] == '%' )
		{
			sprintf( s + j, "%%%02X", ((unsigned char*)t)[i] );
			j += 2;
		}
		else
		{
			s[j] = t[i];
		}
	}
	s[j] = 0;
	
	g_free( t );
}

/* Strip newlines from a string. Modifies the string passed to it. */ 
char *strip_newlines( char *source )
{
	int i;	

	for( i = 0; source[i] != '\0'; i ++ )
		if( source[i] == '\n' || source[i] == '\r' )
			source[i] = 32;
	
	return source;
}
