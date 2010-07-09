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
#include "ipc.h"
#include <signal.h>
#include <stdio.h>
#include <errno.h>

static gboolean bitlbee_io_new_client( gpointer data, gint fd, b_input_condition condition );

static gboolean try_listen( struct addrinfo *res )
{
	int i;
	
	global.listen_socket = socket( res->ai_family, res->ai_socktype, res->ai_protocol );
	if( global.listen_socket < 0 )
	{
		log_error( "socket" );
		return FALSE;
	}

#ifdef IPV6_V6ONLY		
	if( res->ai_family == AF_INET6 )
	{
		i = 0;
		setsockopt( global.listen_socket, IPPROTO_IPV6, IPV6_V6ONLY,
		            (char *) &i, sizeof( i ) );
	}
#endif

	/* TIME_WAIT (?) sucks.. */
	i = 1;
	setsockopt( global.listen_socket, SOL_SOCKET, SO_REUSEADDR, &i, sizeof( i ) );

	i = bind( global.listen_socket, res->ai_addr, res->ai_addrlen );
	if( i == -1 )
	{
		closesocket( global.listen_socket );
		global.listen_socket = -1;
		
		log_error( "bind" );
		return FALSE;
	}
	
	return TRUE;
}

int bitlbee_daemon_init()
{
	struct addrinfo *res, hints, *addrinfo_bind;
	int i;
	FILE *fp;
	
	log_link( LOGLVL_ERROR, LOGOUTPUT_CONSOLE );
	log_link( LOGLVL_WARNING, LOGOUTPUT_CONSOLE );
	
	memset( &hints, 0, sizeof( hints ) );
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE
#ifdef AI_ADDRCONFIG
	               | AI_ADDRCONFIG
#endif
	;

	i = getaddrinfo( global.conf->iface_in, global.conf->port, &hints, &addrinfo_bind );
	if( i )
	{
		log_message( LOGLVL_ERROR, "Couldn't parse address `%s': %s",
		                           global.conf->iface_in, gai_strerror(i) );
		return -1;
	}

	global.listen_socket = -1;
	
	/* Try IPv6 first (which will become an IPv6+IPv4 socket). */
	for( res = addrinfo_bind; res; res = res->ai_next )
		if( res->ai_family == AF_INET6 && try_listen( res ) )
			break;
	
	/* The rest (so IPv4, I guess). */
	if( res == NULL )
		for( res = addrinfo_bind; res; res = res->ai_next )
			if( res->ai_family != AF_INET6 && try_listen( res ) )
				break;
	
	freeaddrinfo( addrinfo_bind );

	i = listen( global.listen_socket, 10 );
	if( i == -1 )
	{
		log_error( "listen" );
		return( -1 );
	}
	
	global.listen_watch_source_id = b_input_add( global.listen_socket, B_EV_IO_READ, bitlbee_io_new_client, NULL );
	
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
		
		setsid();
		chdir( "/" );
		
		if( getenv( "_BITLBEE_RESTART_STATE" ) == NULL )
			for( i = 0; i < 3; i ++ )
				if( close( i ) == 0 )
				{
					/* Keep something bogus on those fd's just in case. */
					open( "/dev/null", O_WRONLY );
				}
	}
#endif
	
	if( global.conf->runmode == RUNMODE_FORKDAEMON )
		ipc_master_load_state( getenv( "_BITLBEE_RESTART_STATE" ) );

	if( global.conf->runmode == RUNMODE_DAEMON || global.conf->runmode == RUNMODE_FORKDAEMON )
		ipc_master_listen_socket();
	
#ifndef _WIN32
	if( ( fp = fopen( global.conf->pidfile, "w" ) ) )
	{
		fprintf( fp, "%d\n", (int) getpid() );
		fclose( fp );
	}
	else
	{
		log_message( LOGLVL_WARNING, "Warning: Couldn't write PID to `%s'", global.conf->pidfile );
	}
#endif
	
	if( !global.conf->nofork )
	{
		log_link( LOGLVL_ERROR, LOGOUTPUT_SYSLOG );
		log_link( LOGLVL_WARNING, LOGOUTPUT_SYSLOG );
	}
	
	return( 0 );
}
 
int bitlbee_inetd_init()
{
	if( !irc_new( 0 ) )
		return( 1 );
	
	return( 0 );
}

gboolean bitlbee_io_current_client_read( gpointer data, gint fd, b_input_condition cond )
{
	irc_t *irc = data;
	char line[513];
	int st;
	
	st = read( irc->fd, line, sizeof( line ) - 1 );
	if( st == 0 )
	{
		irc_abort( irc, 1, "Connection reset by peer" );
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
			irc_abort( irc, 1, "Read error: %s", strerror( errno ) );
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
	
	irc_process( irc );
	
	/* Normally, irc_process() shouldn't call irc_free() but irc_abort(). Just in case: */
	if( !g_slist_find( irc_connection_list, irc ) )
	{
		log_message( LOGLVL_WARNING, "Abnormal termination of connection with fd %d.", fd );
		return FALSE;
	} 
	
	/* Very naughty, go read the RFCs! >:) */
	if( irc->readbuffer && ( strlen( irc->readbuffer ) > 1024 ) )
	{
		irc_abort( irc, 0, "Maximum line length exceeded" );
		return FALSE;
	}
	
	return TRUE;
}

gboolean bitlbee_io_current_client_write( gpointer data, gint fd, b_input_condition cond )
{
	irc_t *irc = data;
	int st, size;
	char *temp;

	if( irc->sendbuffer == NULL )
		return FALSE;
	
	size = strlen( irc->sendbuffer );
	st = write( irc->fd, irc->sendbuffer, size );
	
	if( st == 0 || ( st < 0 && !sockerr_again() ) )
	{
		irc_abort( irc, 1, "Write error: %s", strerror( errno ) );
		return FALSE;
	}
	else if( st < 0 ) /* && sockerr_again() */
	{
		return TRUE;
	}
	
	if( st == size )
	{
		if( irc->status & USTATUS_SHUTDOWN )
		{
			irc_free( irc );
		}
		else
		{
			g_free( irc->sendbuffer );
			irc->sendbuffer = NULL;
			irc->w_watch_source_id = 0;
		}
		
		return FALSE;
	}
	else
	{
		temp = g_strdup( irc->sendbuffer + st );
		g_free( irc->sendbuffer );
		irc->sendbuffer = temp;
		
		return TRUE;
	}
}

static gboolean bitlbee_io_new_client( gpointer data, gint fd, b_input_condition condition )
{
	socklen_t size = sizeof( struct sockaddr_in );
	struct sockaddr_in conn_info;
	int new_socket = accept( global.listen_socket, (struct sockaddr *) &conn_info, &size );
	
	if( new_socket == -1 )
	{
		log_message( LOGLVL_WARNING, "Could not accept new connection: %s", strerror( errno ) );
		return TRUE;
	}
	
#ifndef _WIN32
	if( global.conf->runmode == RUNMODE_FORKDAEMON )
	{
		pid_t client_pid = 0;
		int fds[2];
		
		if( socketpair( AF_UNIX, SOCK_STREAM, 0, fds ) == -1 )
		{
			log_message( LOGLVL_WARNING, "Could not create IPC socket for client: %s", strerror( errno ) );
			fds[0] = fds[1] = -1;
		}
		
		sock_make_nonblocking( fds[0] );
		sock_make_nonblocking( fds[1] );
		
		client_pid = fork();
		
		if( client_pid > 0 && fds[0] != -1 )
		{
			struct bitlbee_child *child;
			
			/* TODO: Stuff like this belongs in ipc.c. */
			child = g_new0( struct bitlbee_child, 1 );
			child->pid = client_pid;
			child->ipc_fd = fds[0];
			child->ipc_inpa = b_input_add( child->ipc_fd, B_EV_IO_READ, ipc_master_read, child );
			child->to_fd = -1;
			child_list = g_slist_append( child_list, child );
			
			log_message( LOGLVL_INFO, "Creating new subprocess with pid %d.", (int) client_pid );
			
			/* Close some things we don't need in the parent process. */
			close( new_socket );
			close( fds[1] );
		}
		else if( client_pid == 0 )
		{
			irc_t *irc;
			
			/* Since we're fork()ing here, let's make sure we won't
			   get the same random numbers as the parent/siblings. */
			srand( time( NULL ) ^ getpid() );
			
			b_main_init();
			
			/* Close the listening socket, we're a client. */
			close( global.listen_socket );
			b_event_remove( global.listen_watch_source_id );
			
			/* Make the connection. */
			irc = irc_new( new_socket );
			
			/* We can store the IPC fd there now. */
			global.listen_socket = fds[1];
			global.listen_watch_source_id = b_input_add( fds[1], B_EV_IO_READ, ipc_child_read, irc );
			
			close( fds[0] );
			
			ipc_master_free_all();
		}
	}
	else
#endif
	{
		log_message( LOGLVL_INFO, "Creating new connection with fd %d.", new_socket );
		irc_new( new_socket );
	}
	
	return TRUE;
}

gboolean bitlbee_shutdown( gpointer data, gint fd, b_input_condition cond )
{
	/* Try to save data for all active connections (if desired). */
	while( irc_connection_list != NULL )
		irc_abort( irc_connection_list->data, FALSE,
		           "BitlBee server shutting down" );
	
	/* We'll only reach this point when not running in inetd mode: */
	b_main_quit();
	
	return FALSE;
}
