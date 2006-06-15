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

int bitlbee_daemon_init()
{
#ifdef IPV6
	struct sockaddr_in6 listen_addr;
#else
	struct sockaddr_in listen_addr;
#endif
	int i;
	FILE *fp;
	
	log_link( LOGLVL_ERROR, LOGOUTPUT_SYSLOG );
	log_link( LOGLVL_WARNING, LOGOUTPUT_SYSLOG );
	
	global.listen_socket = socket( AF_INETx, SOCK_STREAM, 0 );
	if( global.listen_socket == -1 )
	{
		log_error( "socket" );
		return( -1 );
	}
	
	/* TIME_WAIT (?) sucks.. */
	i = 1;
	setsockopt( global.listen_socket, SOL_SOCKET, SO_REUSEADDR, &i, sizeof( i ) );
	
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
	
	global.listen_watch_source_id = b_input_add( global.listen_socket, GAIM_INPUT_READ, bitlbee_io_new_client, NULL );
	
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
		
		chdir( "/" );
		
		/* Sometimes std* are already closed (for example when we're in a RESTARTed
		   BitlBee process. So let's only close TTY-fds. */
		if( isatty( 0 ) ) close( 0 );
		if( isatty( 1 ) ) close( 1 );
		if( isatty( 2 ) ) close( 2 );
	}
#endif
	
	if( global.conf->runmode == RUNMODE_FORKDAEMON )
		ipc_master_load_state();

	if( global.conf->runmode == RUNMODE_DAEMON || 
		global.conf->runmode == RUNMODE_FORKDAEMON )
		ipc_master_listen_socket();
	
	if( ( fp = fopen( global.conf->pidfile, "w" ) ) )
	{
		fprintf( fp, "%d\n", (int) getpid() );
		fclose( fp );
	}
	else
	{
		log_message( LOGLVL_WARNING, "Warning: Couldn't write PID to `%s'", global.conf->pidfile );
	}
	
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
		g_free( irc->sendbuffer );
		irc->sendbuffer = NULL;
		irc->w_watch_source_id = 0;
		
		if( irc->status & USTATUS_SHUTDOWN )
			irc_free( irc );
		
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
	pid_t client_pid = 0;
	
	if( new_socket == -1 )
	{
		log_message( LOGLVL_WARNING, "Could not accept new connection: %s", strerror( errno ) );
		return TRUE;
	}
	
	if( global.conf->runmode == RUNMODE_FORKDAEMON )
	{
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
			
			child = g_new0( struct bitlbee_child, 1 );
			child->pid = client_pid;
			child->ipc_fd = fds[0];
			child->ipc_inpa = b_input_add( child->ipc_fd, GAIM_INPUT_READ, ipc_master_read, child );
			child_list = g_slist_append( child_list, child );
			
			log_message( LOGLVL_INFO, "Creating new subprocess with pid %d.", client_pid );
			
			/* Close some things we don't need in the parent process. */
			close( new_socket );
			close( fds[1] );
		}
		else if( client_pid == 0 )
		{
			irc_t *irc;
			
			/* Close the listening socket, we're a client. */
			close( global.listen_socket );
			b_event_remove( global.listen_watch_source_id );
			
			/* Make the connection. */
			irc = irc_new( new_socket );
			
			/* We can store the IPC fd there now. */
			global.listen_socket = fds[1];
			global.listen_watch_source_id = b_input_add( fds[1], GAIM_INPUT_READ, ipc_child_read, irc );
			
			close( fds[0] );
			
			ipc_master_free_all();
		}
	}
	else
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
		irc_free( irc_connection_list->data );
	
	/* We'll only reach this point when not running in inetd mode: */
	b_main_quit();
	
	return FALSE;
}
