  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2004 Wilmer van der Gaast and others                *
  \********************************************************************/

/* Main file (Unix specific part)                                       */

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
#include "commands.h"
#include "crypting.h"
#include "protocols/nogaim.h"
#include "help.h"
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>

global_t global;	/* Against global namespace pollution */

static void sighandler( int signal );

int main( int argc, char *argv[] )
{
	int i = 0;
	struct sigaction sig, old;
	
	memset( &global, 0, sizeof( global_t ) );
	
	global.loop = g_main_new( FALSE );
	
	log_init( );
	nogaim_init( );
	
	CONF_FILE = g_strdup( CONF_FILE_DEF );
	
	global.helpfile = g_strdup( HELP_FILE );

	global.conf = conf_load( argc, argv );
	if( global.conf == NULL )
		return( 1 );


	if( global.conf->runmode == RUNMODE_INETD )
	{
		i = bitlbee_inetd_init();
		log_message( LOGLVL_INFO, "Bitlbee %s starting in inetd mode.", BITLBEE_VERSION );

	}
	else if( global.conf->runmode == RUNMODE_DAEMON )
	{
		i = bitlbee_daemon_init();
		log_message( LOGLVL_INFO, "Bitlbee %s starting in daemon mode.", BITLBEE_VERSION );
	}
	if( i != 0 )
		return( i );

	global.storage = storage_init( global.conf->primary_storage, 
								   global.conf->migrate_storage );
	if ( global.storage == NULL) {
		log_message( LOGLVL_ERROR, "Unable to load storage backend '%s'", global.conf->primary_storage );
		return( 1 );
	}
	
 	
	/* Catch some signals to tell the user what's happening before quitting */
	memset( &sig, 0, sizeof( sig ) );
	sig.sa_handler = sighandler;
	sigaction( SIGPIPE, &sig, &old );
	sig.sa_flags = SA_RESETHAND;
	sigaction( SIGINT,  &sig, &old );
	sigaction( SIGILL,  &sig, &old );
	sigaction( SIGBUS,  &sig, &old );
	sigaction( SIGFPE,  &sig, &old );
	sigaction( SIGSEGV, &sig, &old );
	sigaction( SIGTERM, &sig, &old );
	sigaction( SIGQUIT, &sig, &old );
	sigaction( SIGXCPU, &sig, &old );
	
	if( !getuid() || !geteuid() )
		log_message( LOGLVL_WARNING, "BitlBee is running with root privileges. Why?" );
	if( help_init( &(global.help) ) == NULL )
		log_message( LOGLVL_WARNING, "Error opening helpfile %s.", HELP_FILE );
	
	g_main_run( global.loop );
	
	return( 0 );
}

static void sighandler( int signal )
{
	/* FIXME: In fact, calling log_message() here can be dangerous. But well, let's take the risk for now. */
	
	if( signal == SIGTERM )
	{
		static int first = 1;
		
		if( first )
		{
			/* We don't know what we were doing when this signal came in. It's not safe to touch
			   the user data now (not to mention writing them to disk), so add a timer. */
			
			log_message( LOGLVL_ERROR, "SIGTERM received, cleaning up process." );
			g_timeout_add_full( G_PRIORITY_LOW, 1, (GSourceFunc) bitlbee_shutdown, NULL, NULL );
			
			first = 0;
		}
		else
		{
			/* Well, actually, for now we'll never need this part because this signal handler
			   will never be called more than once in a session for a non-SIGPIPE signal...
			   But just in case we decide to change that: */
			
			log_message( LOGLVL_ERROR, "SIGTERM received twice, so long for a clean shutdown." );
			raise( signal );
		}
	}
	else if( signal != SIGPIPE )
	{
		log_message( LOGLVL_ERROR, "Fatal signal received: %d. That's probably a bug.", signal );
		raise( signal );
	}
}

double gettime()
{
	struct timeval time[1];

	gettimeofday( time, 0 );
	return( (double) time->tv_sec + (double) time->tv_usec / 1000000 );
}
