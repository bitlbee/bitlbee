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
	int new_socket = accept( global.listen_socket, (struct sockaddr *) &conn_info, &size );
	pid_t client_pid = 0;
	
	if( global.conf->runmode == RUNMODE_FORKDAEMON )
		client_pid = fork();
	
	if( client_pid == 0 )
	{
		log_message( LOGLVL_INFO, "Creating new connection with fd %d.", new_socket );
		irc_new( new_socket );
		
		if( global.conf->runmode == RUNMODE_FORKDAEMON )
		{
			/* Close the listening socket, we're a client. */
			close( global.listen_socket );
			g_source_remove( global.listen_watch_source_id );
		}
	}
	else
	{
		/* We don't need this one, only the client does. */
		close( new_socket );
	}
	
	return TRUE;
}
 


int bitlbee_daemon_init()
{
#ifdef IPV6
	struct sockaddr_in6 listen_addr;
#else
	struct sockaddr_in listen_addr;
#endif
	int i;
	GIOChannel *ch;
	
	log_link( LOGLVL_ERROR, LOGOUTPUT_SYSLOG );
	log_link( LOGLVL_WARNING, LOGOUTPUT_SYSLOG );
	
	global.listen_socket = socket( AF_INETx, SOCK_STREAM, 0 );
	if( global.listen_socket == -1 )
	{
		log_error( "socket" );
		return( -1 );
	}
	
#ifdef IPV6
	listen_addr.sin6_family = AF_INETx;
	listen_addr.sin6_port = htons( global.conf->port );
	i = inet_pton( AF_INETx, ipv6_wrap( global.conf->iface ), &listen_addr.sin6_addr );
#else
	listen_addr.sin_family = AF_INETx;
	listen_addr.sin_port = htons( global.conf->port );
	i = inet_pton( AF_INETx, global.conf->iface, &listen_addr.sin_addr );
#endif
	
	if( i != 1 )
	{
		log_message( LOGLVL_ERROR, "Couldn't parse address `%s'", global.conf->iface );
		return( -1 );
	}
	
	i = bind( global.listen_socket, (struct sockaddr *) &listen_addr, sizeof( listen_addr ) );
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
	global.listen_watch_source_id = g_io_add_watch( ch, G_IO_IN, bitlbee_io_new_client, NULL );
	
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
	
	log_link( LOGLVL_ERROR, LOGOUTPUT_IRC );
	log_link( LOGLVL_WARNING, LOGOUTPUT_IRC );
	
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
