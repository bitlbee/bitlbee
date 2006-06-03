/****************************************************************\
*                                                                *
*  bitlbeed.c                                                    *
*                                                                *
*  A tiny daemon to allow you to run The Bee as a non-root user  *
*  (without access to /etc/inetd.conf or whatever)               *
*                                                                *
*  Copyright 2002-2004 Wilmer van der Gaast <wilmer@gaast.net>   *
*                                                                *
*  Licensed under the GNU General Public License                 *
*                                                                *
*  Modified by M. Dennis, 20040627                               *
\****************************************************************/

/* 
   ChangeLog:
   
   2004-06-27: Added support for AF_LOCAL (UNIX domain) sockets
               Renamed log to do_log to fix conflict warning
               Changed protocol to 0 (6 is not supported?)
               Added error check for socket()
               Added a no-fork (debug) mode
   2004-05-15: Added rate limiting
   2003-12-26: Added the SO_REUSEADDR sockopt, logging and CPU-time limiting
               for clients using setrlimit(), fixed the execv() call
   2002-11-29: Added the timeout so old child processes clean up faster
   2002-11-28: First version
*/

#define SELECT_TIMEOUT 2
#define MAX_LOG_LEN 128

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdarg.h>
#include <time.h>

#include <sys/wait.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <sys/stat.h>

#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>

typedef struct settings
{
	char local;
	char debug;
	char *interface;
	signed int port;
	
	unsigned char max_conn;
	int seconds;
	
	int rate_seconds;
	int rate_times;
	int rate_ignore;
	
	char **call;
} settings_t;

typedef struct ipstats
{
	unsigned int ip;
	
	time_t rate_start;
	int rate_times;
	time_t rate_ignore;
	
	struct ipstats *next;
} ipstats_t;

FILE *logfile;
ipstats_t *ipstats;

settings_t *set_load( int argc, char *argv[] );
void do_log( char *fmt, ... );
ipstats_t *ip_get( char *ip_txt );

int main( int argc, char *argv[] )
{
	const int rebind_on = 1;
	settings_t *set;
	
	int serv_fd, serv_len;
	struct sockaddr_in serv_addr;
	struct sockaddr_un local_addr;
	
	pid_t st;
	
	if( !( set = set_load( argc, argv ) ) )
		return( 1 );
	
	if( !logfile )
		if( !( logfile = fopen( "/dev/null", "w" ) ) )
		{
			perror( "fopen" );
			return( 1 );
		}
	
	fcntl( fileno( logfile ), F_SETFD, FD_CLOEXEC );
	
	if( set->local )
		serv_fd = socket( PF_LOCAL, SOCK_STREAM, 0 );
	else
		serv_fd = socket( PF_INET, SOCK_STREAM, 0 );
	if( serv_fd < 0 )
	{
		perror( "socket" );
		return( 1 );
	}
	setsockopt( serv_fd, SOL_SOCKET, SO_REUSEADDR, &rebind_on, sizeof( rebind_on ) );
	fcntl( serv_fd, F_SETFD, FD_CLOEXEC );
	if (set->local) {
		local_addr.sun_family = AF_LOCAL;
		strncpy( local_addr.sun_path, set->interface, sizeof( local_addr.sun_path ) - 1 );
		local_addr.sun_path[sizeof( local_addr.sun_path ) - 1] = '\0';
		
		/* warning - don't let untrusted users run this program if it
		   is setuid/setgid! Arbitrary file deletion risk! */
		unlink( set->interface );
		if( bind( serv_fd, (struct sockaddr *) &local_addr, SUN_LEN( &local_addr ) ) != 0 )
		{
			perror( "bind" );
			return( 1 );
		}
		chmod( set->interface, S_IRWXO|S_IRWXG|S_IRWXU );

	} else {
		serv_addr.sin_family = AF_INET;
		serv_addr.sin_addr.s_addr = inet_addr( set->interface );
		serv_addr.sin_port = htons( set->port );
		serv_len = sizeof( serv_addr );
	
		if( bind( serv_fd, (struct sockaddr *) &serv_addr, serv_len ) != 0 )
		{
			perror( "bind" );
			return( 1 );
		}
	}
	
	if( listen( serv_fd, set->max_conn ) != 0 )
	{
		perror( "listen" );
		return( 1 );
	}
	
	if ( ! set->debug ) {
		st = fork();
		if( st < 0 )
		{
			perror( "fork" );
			return( 1 );
		}
		else if( st > 0 )
		{
			return( 0 );
		}
		
		setsid();
		close( 0 );
		close( 1 );
		close( 2 );
	}
	
	do_log( "bitlbeed running" );
	
	/* The Daemon */
	while( 1 )
	{
		int cli_fd, cli_len, i, st;
		struct sockaddr_in cli_addr;
		struct sockaddr_un cli_local;
		ipstats_t *ip;
		char *cli_txt;
		pid_t child;
		
		static int running = 0;
		
		fd_set rd;
		struct timeval tm;
		
		/* accept() only returns after someone connects. To clean up old
		   processes (by running waitpid()) it's better to use select()
		   with a timeout. */
		FD_ZERO( &rd );
		FD_SET( serv_fd, &rd );
		tm.tv_sec = SELECT_TIMEOUT;
		tm.tv_usec = 0;
		if( select( serv_fd + 1, &rd, NULL, NULL, &tm ) > 0 )
		{
			if (set->local) {
				cli_len = SUN_LEN( &cli_local );
				cli_fd = accept( serv_fd, (struct sockaddr *) &cli_local, &cli_len );
				cli_txt = "127.0.0.1";
			} else {
				cli_len = sizeof( cli_addr );
				cli_fd = accept( serv_fd, (struct sockaddr *) &cli_addr, &cli_len );
				cli_txt = inet_ntoa( cli_addr.sin_addr );
			}
			
			ip = ip_get( cli_txt );
			
			if( set->rate_times == 0 || time( NULL ) > ip->rate_ignore )
			{
				/* We want this socket on stdout and stderr too! */
				dup( cli_fd ); dup( cli_fd );
				
				if( ( child = fork() ) == 0 )
				{
					if( set->seconds )
					{
						struct rlimit li;
						
						li.rlim_cur = (rlim_t) set->seconds;
						li.rlim_max = (rlim_t) set->seconds + 1;
						setrlimit( RLIMIT_CPU, &li );
					}
					execv( set->call[0], set->call );
					do_log( "Error while executing %s!", set->call[0] );
					return( 1 );
				}
				
				running ++;
				close( 0 );
				close( 1 );
				close( 2 );
				
				do_log( "Started child process for client %s (PID=%d), got %d clients now", cli_txt, child, running );
				
				if( time( NULL ) < ( ip->rate_start + set->rate_seconds ) )
				{
					ip->rate_times ++;
					if( ip->rate_times >= set->rate_times )
					{
						do_log( "Client %s crossed the limit; ignoring for the next %d seconds", cli_txt, set->rate_ignore );
						ip->rate_ignore = time( NULL ) + set->rate_ignore;
						ip->rate_start = 0;
					}
				}
				else
				{
					ip->rate_start = time( NULL );
					ip->rate_times = 1;
				}
			}
			else
			{
				do_log( "Ignoring connection from %s", cli_txt );
				close( cli_fd );
			}
		}
		
		/* If the max. number of connection is reached, don't accept
		   new connections until one expires -> Not always WNOHANG
		   
		   Cleaning up child processes is a good idea anyway... :-) */
		while( ( i = waitpid( 0, &st, ( ( running < set->max_conn ) || ( set->max_conn == 0 ) ) ? WNOHANG : 0 ) ) > 0 )
		{
			running --;
			if( WIFEXITED( st ) )
			{
				do_log( "Child process (PID=%d) exited normally with status %d. %d Clients left now",
				     i, WEXITSTATUS( st ), running );
			}
			else if( WIFSIGNALED( st ) )
			{
				do_log( "Child process (PID=%d) killed by signal %d. %d Clients left now",
				     i, WTERMSIG( st ), running );
			}
			else
			{
				/* Should not happen AFAIK... */
				do_log( "Child process (PID=%d) stopped for unknown reason, %d clients left now",
				     i, running );
			}
		}
	}
	
	return( 0 );
}

settings_t *set_load( int argc, char *argv[] )
{
	settings_t *set;
	int opt, i;
	
	set = malloc( sizeof( settings_t ) );
	memset( set, 0, sizeof( settings_t ) );
	set->interface = NULL;		/* will be filled in later */
	set->port = 6667;
	set->local = 0;
	set->debug = 0;
	
	set->rate_seconds = 600;
	set->rate_times = 5;
	set->rate_ignore = 900;
	
	while( ( opt = getopt( argc, argv, "i:p:n:t:l:r:hud" ) ) >= 0 )
	{
		if( opt == 'i' )
		{
			set->interface = strdup( optarg );
		}
		else if( opt == 'p' )
		{
			if( ( sscanf( optarg, "%d", &i ) != 1 ) || ( i <= 0 ) || ( i > 65535 ) )
			{
				fprintf( stderr, "Invalid port number: %s\n", optarg );
				return( NULL );
			}
			set->port = i;
		}
		else if( opt == 'n' )
		{
			if( ( sscanf( optarg, "%d", &i ) != 1 ) || ( i < 0 ) )
			{
				fprintf( stderr, "Invalid number of connections: %s\n", optarg );
				return( NULL );
			}
			set->max_conn = i;
		}
		else if( opt == 't' )
		{
			if( ( sscanf( optarg, "%d", &i ) != 1 ) || ( i < 0 ) || ( i > 600 ) )
			{
				fprintf( stderr, "Invalid number of seconds: %s\n", optarg );
				return( NULL );
			}
			set->seconds = i;
		}
		else if( opt == 'l' )
		{
			if( !( logfile = fopen( optarg, "a" ) ) )
			{
				perror( "fopen" );
				fprintf( stderr, "Error opening logfile, giving up.\n" );
				return( NULL );
			}
			setbuf( logfile, NULL );
		}
		else if( opt == 'r' )
		{
			if( sscanf( optarg, "%d,%d,%d", &set->rate_seconds, &set->rate_times, &set->rate_ignore ) != 3 )
			{
				fprintf( stderr, "Invalid argument to -r.\n" );
				return( NULL );
			}
		}
		else if( opt == 'u' )
			set->local = 1;
		else if( opt == 'd' )
			set->debug = 1;
		else if( opt == 'h' )
		{
			printf( "Usage: %s [-i <interface>] [-p <port>] [-n <num>] [-r x,y,z] ...\n"
			        "          ... <command> <args...>\n"
			        "A simple inetd-like daemon to have a program listening on a TCP socket without\n"
			        "needing root access to the machine\n"
			        "\n"
			        "  -i  Specify the interface (by IP address) to listen on.\n"
			        "      (Default: 0.0.0.0 (any interface))\n"
			        "  -p  Port number to listen on. (Default: 6667)\n"
			        "  -n  Maximum number of connections. (Default: 0 (unlimited))\n"
			        "  -t  Specify the maximum number of CPU seconds per process.\n"
			        "      (Default: 0 (unlimited))\n"
			        "  -l  Specify a logfile. (Default: none)\n"
			        "  -r  Rate limiting: Ignore a host for z seconds when it connects for more\n"
			        "      than y times in x seconds. (Default: 600,5,900. Disable: 0,0,0)\n"
				"  -u  Use a local socket, by default /tmp/bitlbee (override with -i <filename>)\n"
				"  -d  Don't fork for listening (for debugging purposes)\n"
			        "  -h  This information\n", argv[0] );
			return( NULL );
		}
	}
	
	if( set->interface == NULL )
		set->interface = (set->local) ? "/tmp/bitlbee" : "0.0.0.0";
	
	if( optind == argc )
	{
		fprintf( stderr, "Missing program parameter!\n" );
		return( NULL );
	}
	
	/* The remaining arguments are the executable and its arguments */
	set->call = malloc( ( argc - optind + 1 ) * sizeof( char* ) );
	memcpy( set->call, argv + optind, sizeof( char* ) * ( argc - optind ) );
	set->call[argc-optind] = NULL;
	
	return( set );
}

void do_log( char *fmt, ... )
{
	va_list params;
	char line[MAX_LOG_LEN];
	time_t tm;
	int l;
	
	memset( line, 0, MAX_LOG_LEN );
	
	tm = time( NULL );
	strcpy( line, ctime( &tm ) );
	l = strlen( line );
	line[l-1] = ' ';
	
	va_start( params, fmt );
	vsnprintf( line + l, MAX_LOG_LEN - l - 2, fmt, params );
	va_end( params );
	strcat( line, "\n" );
	
	fprintf( logfile, "%s", line );
}

ipstats_t *ip_get( char *ip_txt )
{
	unsigned int ip;
	ipstats_t *l;
	int p[4];
	
	sscanf( ip_txt, "%d.%d.%d.%d", p + 0, p + 1, p + 2, p + 3 );
	ip = ( p[0] << 24 ) | ( p[1] << 16 ) | ( p[2] << 8 ) | ( p[3] );
	
	for( l = ipstats; l; l = l->next )
	{
		if( l->ip == ip )
			return( l );
	}
	
	if( ipstats )
	{
		for( l = ipstats; l->next; l = l->next );
		
		l->next = malloc( sizeof( ipstats_t ) );
		l = l->next;
	}
	else
	{
		l = malloc( sizeof( ipstats_t ) );
		ipstats = l;
	}
	memset( l, 0, sizeof( ipstats_t ) );
	
	l->ip = ip;
	
	return( l );
}
