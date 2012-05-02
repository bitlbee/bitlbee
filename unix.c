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

#include "arc.h"
#include "base64.h"
#include "commands.h"
#include "protocols/nogaim.h"
#include "help.h"
#include "ipc.h"
#include "lib/ssl_client.h"
#include "md5.h"
#include "misc.h"
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <pwd.h>
#include <locale.h>
#include <grp.h>

#if defined(OTR_BI) || defined(OTR_PI)
#include "otr.h"
#endif

global_t global;	/* Against global namespace pollution */

static void sighandler( int signal );

static int crypt_main( int argc, char *argv[] );

int main( int argc, char *argv[] )
{
	int i = 0;
	char *old_cwd = NULL;
	struct sigaction sig, old;
	
	/* Required to make iconv to ASCII//TRANSLIT work. This makes BitlBee
	   system-locale-sensitive. :-( */
	setlocale( LC_CTYPE, "" );
	
	if( argc > 1 && strcmp( argv[1], "-x" ) == 0 )
		return crypt_main( argc, argv );
	
	log_init();
	
	global.conf_file = g_strdup( CONF_FILE_DEF );
	global.conf = conf_load( argc, argv );
	if( global.conf == NULL )
		return( 1 );
	
	b_main_init();
	
	/* libpurple doesn't like fork()s after initializing itself, so if
	   we use it, do this init a little later (in case we're running in
	   ForkDaemon mode). */
#ifndef WITH_PURPLE
	nogaim_init();
#endif
	
 	/* Ugly Note: libotr and gnutls both use libgcrypt. libgcrypt
 	   has a process-global config state whose initialization happpens
 	   twice if libotr and gnutls are used together. libotr installs custom
 	   memory management functions for libgcrypt while our gnutls module
 	   uses the defaults. Therefore we initialize OTR after SSL. *sigh* */
 	ssl_init();
#ifdef OTR_BI
 	otr_init();
#endif
	/* And in case OTR is loaded as a plugin, it'll also get loaded after
	   this point. */
	
	srand( time( NULL ) ^ getpid() );
	
	global.helpfile = g_strdup( HELP_FILE );
	if( help_init( &global.help, global.helpfile ) == NULL )
		log_message( LOGLVL_WARNING, "Error opening helpfile %s.", HELP_FILE );

	global.storage = storage_init( global.conf->primary_storage, global.conf->migrate_storage );
	if( global.storage == NULL )
	{
		log_message( LOGLVL_ERROR, "Unable to load storage backend '%s'", global.conf->primary_storage );
		return( 1 );
	}
	
	if( global.conf->runmode == RUNMODE_INETD )
	{
		log_link( LOGLVL_ERROR, LOGOUTPUT_IRC );
		log_link( LOGLVL_WARNING, LOGOUTPUT_IRC );
	
		i = bitlbee_inetd_init();
		log_message( LOGLVL_INFO, "%s %s starting in inetd mode.", PACKAGE, BITLBEE_VERSION );

	}
	else if( global.conf->runmode == RUNMODE_DAEMON )
	{
		log_link( LOGLVL_ERROR, LOGOUTPUT_CONSOLE );
		log_link( LOGLVL_WARNING, LOGOUTPUT_CONSOLE );

		i = bitlbee_daemon_init();
		log_message( LOGLVL_INFO, "%s %s starting in daemon mode.", PACKAGE, BITLBEE_VERSION );
	}
	else if( global.conf->runmode == RUNMODE_FORKDAEMON )
	{
		log_link( LOGLVL_ERROR, LOGOUTPUT_CONSOLE );
		log_link( LOGLVL_WARNING, LOGOUTPUT_CONSOLE );

		/* In case the operator requests a restart, we need this. */
		old_cwd = g_malloc( 256 );
		if( getcwd( old_cwd, 255 ) == NULL )
		{
			log_message( LOGLVL_WARNING, "Could not save current directory: %s", strerror( errno ) );
			g_free( old_cwd );
			old_cwd = NULL;
		}
		
		i = bitlbee_daemon_init();
		log_message( LOGLVL_INFO, "%s %s starting in forking daemon mode.", PACKAGE, BITLBEE_VERSION );
	}
	if( i != 0 )
		return( i );
	
	if( ( global.conf->user && *global.conf->user ) &&
	    ( global.conf->runmode == RUNMODE_DAEMON || 
	      global.conf->runmode == RUNMODE_FORKDAEMON ) &&
	    ( !getuid() || !geteuid() ) )
	{
		struct passwd *pw = NULL;
		pw = getpwnam( global.conf->user );
		if( pw )
		{
			initgroups( global.conf->user, pw->pw_gid );
			setgid( pw->pw_gid );
			setuid( pw->pw_uid );
		}
		else
		{
			log_message( LOGLVL_WARNING, "Failed to look up user %s.", global.conf->user );
		}
	}
 	
	/* Catch some signals to tell the user what's happening before quitting */
	memset( &sig, 0, sizeof( sig ) );
	sig.sa_handler = sighandler;
	sigaction( SIGCHLD, &sig, &old );
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
	
	b_main_run();
	
	/* Mainly good for restarting, to make sure we close the help.txt fd. */
	help_free( &global.help );
	
	if( global.restart )
	{
		char *fn = ipc_master_save_state();
		char *env;
		
		env = g_strdup_printf( "_BITLBEE_RESTART_STATE=%s", fn );
		putenv( env );
		g_free( fn );
		/* Looks like env should *not* be freed here as putenv
		   doesn't make a copy. Odd. */
		
		i = chdir( old_cwd );
		close( global.listen_socket );
		
		if( execv( argv[0], argv ) == -1 )
			/* Apparently the execve() failed, so let's just
			   jump back into our own/current main(). */
			/* Need more cleanup code to make this work. */
			return 1; /* main( argc, argv ); */
	}
	
	return( 0 );
}

static int crypt_main( int argc, char *argv[] )
{
	int pass_len;
	unsigned char *pass_cr, *pass_cl;
	
	if( argc < 4 || ( strcmp( argv[2], "hash" ) != 0 &&
	                  strcmp( argv[2], "unhash" ) != 0 && argc < 5 ) )
	{
		printf( "Supported:\n"
		        "  %s -x enc <key> <cleartext password>\n"
		        "  %s -x dec <key> <encrypted password>\n"
		        "  %s -x hash <cleartext password>\n"
		        "  %s -x unhash <hashed password>\n"
		        "  %s -x chkhash <hashed password> <cleartext password>\n",
		        argv[0], argv[0], argv[0], argv[0], argv[0] );
	}
	else if( strcmp( argv[2], "enc" ) == 0 )
	{
		pass_len = arc_encode( argv[4], strlen( argv[4] ), (unsigned char**) &pass_cr, argv[3], 12 );
		printf( "%s\n", base64_encode( pass_cr, pass_len ) );
	}
	else if( strcmp( argv[2], "dec" ) == 0 )
	{
		pass_len = base64_decode( argv[4], (unsigned char**) &pass_cr );
		arc_decode( pass_cr, pass_len, (char**) &pass_cl, argv[3] );
		printf( "%s\n", pass_cl );
	}
	else if( strcmp( argv[2], "hash" ) == 0 )
	{
		md5_byte_t pass_md5[21];
		md5_state_t md5_state;
		
		random_bytes( pass_md5 + 16, 5 );
		md5_init( &md5_state );
		md5_append( &md5_state, (md5_byte_t*) argv[3], strlen( argv[3] ) );
		md5_append( &md5_state, pass_md5 + 16, 5 ); /* Add the salt. */
		md5_finish( &md5_state, pass_md5 );
		
		printf( "%s\n", base64_encode( pass_md5, 21 ) );
	}
	else if( strcmp( argv[2], "unhash" ) == 0 )
	{
		printf( "Hash %s submitted to a massive Beowulf cluster of\n"
		        "overclocked 486s. Expect your answer next year somewhere around this time. :-)\n", argv[3] );
	}
	else if( strcmp( argv[2], "chkhash" ) == 0 )
	{
		char *hash = strncmp( argv[3], "md5:", 4 ) == 0 ? argv[3] + 4 : argv[3];
		int st = md5_verify_password( argv[4], hash );
		
		printf( "Hash %s given password.\n", st == 0 ? "matches" : "does not match" );
		
		return st;
	}
	
	return 0;
}

static void sighandler( int signal )
{
	/* FIXME: Calling log_message() here is not a very good idea! */
	
	if( signal == SIGTERM || signal == SIGQUIT || signal == SIGINT )
	{
		static int first = 1;
		
		if( first )
		{
			/* We don't know what we were doing when this signal came in. It's not safe to touch
			   the user data now (not to mention writing them to disk), so add a timer. */
			
			log_message( LOGLVL_ERROR, "SIGTERM received, cleaning up process." );
			b_timeout_add( 1, (b_event_handler) bitlbee_shutdown, NULL );
			
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
	else if( signal == SIGCHLD )
	{
		pid_t pid;
		int st;
		
		while( ( pid = waitpid( 0, &st, WNOHANG ) ) > 0 )
		{
			if( WIFSIGNALED( st ) )
				log_message( LOGLVL_INFO, "Client %d terminated normally. (status = %d)", (int) pid, WEXITSTATUS( st ) );
			else if( WIFEXITED( st ) )
				log_message( LOGLVL_INFO, "Client %d killed by signal %d.", (int) pid, WTERMSIG( st ) );
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
