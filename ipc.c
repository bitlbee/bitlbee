  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2006 Wilmer van der Gaast and others                *
  \********************************************************************/

/* IPC - communication between BitlBee processes                        */

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
#include "ipc.h"
#include "commands.h"
#ifndef _WIN32
#include <sys/un.h>
#endif

GSList *child_list = NULL;
static int ipc_child_recv_fd = -1;

static void ipc_master_cmd_client( irc_t *data, char **cmd )
{
	/* Normally data points at an irc_t block, but for the IPC master
	   this is different. We think this scary cast is better than
	   creating a new command_t structure, just to make the compiler
	   happy. */
	struct bitlbee_child *child = (void*) data;
	
	if( child && cmd[1] )
	{
		child->host = g_strdup( cmd[1] );
		child->nick = g_strdup( cmd[2] );
		child->realname = g_strdup( cmd[3] );
	}
	
	if( g_strcasecmp( cmd[0], "CLIENT" ) == 0 )
		ipc_to_children_str( "OPERMSG :Client connecting (PID=%d): %s@%s (%s)\r\n",
		                     (int) ( child ? child->pid : -1 ), cmd[2], cmd[1], cmd[3] );
}

static void ipc_master_cmd_die( irc_t *data, char **cmd )
{
	if( global.conf->runmode == RUNMODE_FORKDAEMON )
		ipc_to_children_str( "DIE\r\n" );
	
	bitlbee_shutdown( NULL, -1, 0 );
}

static void ipc_master_cmd_deaf( irc_t *data, char **cmd )
{
	if( global.conf->runmode == RUNMODE_DAEMON )
	{
		b_event_remove( global.listen_watch_source_id );
		close( global.listen_socket );
		
		global.listen_socket = global.listen_watch_source_id = -1;
	
		ipc_to_children_str( "OPERMSG :Closed listening socket, waiting "
		                     "for all users to disconnect." );
	}
	else
	{
		ipc_to_children_str( "OPERMSG :The DEAF command only works in "
		                     "normal daemon mode. Try DIE instead." );
	}
}

void ipc_master_cmd_rehash( irc_t *data, char **cmd )
{
	runmode_t oldmode;
	
	oldmode = global.conf->runmode;
	
	g_free( global.conf );
	global.conf = conf_load( 0, NULL );
	
	if( global.conf->runmode != oldmode )
	{
		log_message( LOGLVL_WARNING, "Can't change RunMode setting at runtime, restoring original setting" );
		global.conf->runmode = oldmode;
	}
	
	if( global.conf->runmode == RUNMODE_FORKDAEMON )
		ipc_to_children( cmd );
}

void ipc_master_cmd_restart( irc_t *data, char **cmd )
{
	if( global.conf->runmode != RUNMODE_FORKDAEMON )
	{
		/* Tell child that this is unsupported. */
		return;
	}
	
	global.restart = -1;
	bitlbee_shutdown( NULL, -1, 0 );
}

void ipc_master_cmd_identify( irc_t *data, char **cmd )
{
	struct bitlbee_child *child = (void*) data, *old = NULL;
	char *resp;
	GSList *l;
	
	if( strcmp( child->nick, cmd[1] ) != 0 )
		return;
	
	g_free( child->password );
	child->password = g_strdup( cmd[2] );
	
	for( l = child_list; l; l = l->next )
	{
		old = l->data;
		if( nick_cmp( old->nick, child->nick ) == 0 && child != old &&
		    old->password && strcmp( old->password, child->password ) == 0 )
			break;
	}
	
	child->to_child = old;
	
	if( l )
	{
		resp = "TAKEOVER INIT\r\n";
	}
	else
	{
		/* Won't need the fd since we can't send it anywhere. */
		close( child->to_fd );
		child->to_fd = -1;
		resp = "TAKEOVER NO\r\n";
	}
	
	if( write( child->ipc_fd, resp, strlen( resp ) ) != strlen( resp ) )
	{
		ipc_master_free_one( child );
		child_list = g_slist_remove( child_list, child );
	}
}

static gboolean ipc_send_fd( int fd, int send_fd );

void ipc_master_cmd_takeover( irc_t *data, char **cmd )
{
	struct bitlbee_child *child = (void*) data;
	
	/* TODO: Check if child->to_child is still valid, etc. */
	if( strcmp( cmd[1], "AUTH" ) == 0 )
	{
		if( child->to_child &&
		    child->nick && child->to_child->nick && cmd[2] &&
		    child->password && child->to_child->password && cmd[3] &&
		    strcmp( child->nick, child->to_child->nick ) == 0 &&
		    strcmp( child->nick, cmd[2] ) == 0 &&
		    strcmp( child->password, child->to_child->password ) == 0 &&
		    strcmp( child->password, cmd[3] ) == 0 )
		{
			char *s;
			
			ipc_send_fd( child->to_child->ipc_fd, child->to_fd );
			
			s = irc_build_line( cmd );
			if( write( child->to_child->ipc_fd, s, strlen( s ) ) != strlen( s ) )
			{
				ipc_master_free_one( child );
				child_list = g_slist_remove( child_list, child );
			}
			g_free( s );
		}
	}
}

static const command_t ipc_master_commands[] = {
	{ "client",     3, ipc_master_cmd_client,     0 },
	{ "hello",      0, ipc_master_cmd_client,     0 },
	{ "die",        0, ipc_master_cmd_die,        0 },
	{ "deaf",       0, ipc_master_cmd_deaf,       0 },
	{ "wallops",    1, NULL,                      IPC_CMD_TO_CHILDREN },
	{ "wall",       1, NULL,                      IPC_CMD_TO_CHILDREN },
	{ "opermsg",    1, NULL,                      IPC_CMD_TO_CHILDREN },
	{ "rehash",     0, ipc_master_cmd_rehash,     0 },
	{ "kill",       2, NULL,                      IPC_CMD_TO_CHILDREN },
	{ "restart",    0, ipc_master_cmd_restart,    0 },
	{ "identify",   2, ipc_master_cmd_identify,   0 },
	{ "takeover",   1, ipc_master_cmd_takeover,   0 },
	{ NULL }
};


static void ipc_child_cmd_die( irc_t *irc, char **cmd )
{
	irc_abort( irc, 0, "Shutdown requested by operator" );
}

static void ipc_child_cmd_wallops( irc_t *irc, char **cmd )
{
	if( !( irc->status & USTATUS_LOGGED_IN ) )
		return;
	
	if( strchr( irc->umode, 'w' ) )
		irc_write( irc, ":%s WALLOPS :%s", irc->root->host, cmd[1] );
}

static void ipc_child_cmd_wall( irc_t *irc, char **cmd )
{
	if( !( irc->status & USTATUS_LOGGED_IN ) )
		return;
	
	if( strchr( irc->umode, 's' ) )
		irc_write( irc, ":%s NOTICE %s :%s", irc->root->host, irc->user->nick, cmd[1] );
}

static void ipc_child_cmd_opermsg( irc_t *irc, char **cmd )
{
	if( !( irc->status & USTATUS_LOGGED_IN ) )
		return;
	
	if( strchr( irc->umode, 'o' ) )
		irc_write( irc, ":%s NOTICE %s :*** OperMsg *** %s", irc->root->host, irc->user->nick, cmd[1] );
}

static void ipc_child_cmd_rehash( irc_t *irc, char **cmd )
{
	runmode_t oldmode;
	
	oldmode = global.conf->runmode;
	
	g_free( global.conf );
	global.conf = conf_load( 0, NULL );
	
	global.conf->runmode = oldmode;
}

static void ipc_child_cmd_kill( irc_t *irc, char **cmd )
{
	if( !( irc->status & USTATUS_LOGGED_IN ) )
		return;
	
	if( nick_cmp( cmd[1], irc->user->nick ) != 0 )
		return;		/* It's not for us. */
	
	irc_write( irc, ":%s!%s@%s KILL %s :%s", irc->root->nick, irc->root->nick, irc->root->host, irc->user->nick, cmd[2] );
	irc_abort( irc, 0, "Killed by operator: %s", cmd[2] );
}

static void ipc_child_cmd_hello( irc_t *irc, char **cmd )
{
	if( !( irc->status & USTATUS_LOGGED_IN ) )
		ipc_to_master_str( "HELLO\r\n" );
	else
		ipc_to_master_str( "HELLO %s %s :%s\r\n", irc->user->host, irc->user->nick, irc->user->fullname );
}

static void ipc_child_cmd_takeover( irc_t *irc, char **cmd )
{
	if( strcmp( cmd[1], "NO" ) == 0 )
	{
		/* No takeover, finish the login. */
	}
	else if( strcmp( cmd[1], "INIT" ) == 0 )
	{
		ipc_to_master_str( "TAKEOVER AUTH %s :%s\r\n",
		                   irc->user->nick, irc->password );
		
		/* Drop credentials, we'll shut down soon and shouldn't overwrite
		   any settings. */
		/* TODO: irc_setpass() should do all of this. */
		irc_usermsg( irc, "Trying to take over existing session" );
		/** NOT YET
		irc_setpass( irc, NULL );
		irc->status &= ~USTATUS_IDENTIFIED;
		irc_umode_set( irc, "-R", 1 );
		*/
	}
	else if( strcmp( cmd[1], "AUTH" ) == 0 )
	{
		if( irc->password && cmd[2] && cmd[3] &&
		    ipc_child_recv_fd != -1 &&
		    strcmp( irc->user->nick, cmd[2] ) == 0 &&
		    strcmp( irc->password, cmd[3] ) == 0 )
		{
			fprintf( stderr, "TO\n" );
			b_event_remove( irc->r_watch_source_id );
			closesocket( irc->fd );
			irc->fd = ipc_child_recv_fd;
			irc->r_watch_source_id = b_input_add( irc->fd, B_EV_IO_READ, bitlbee_io_current_client_read, irc );
			ipc_child_recv_fd = -1;
		}
		fprintf( stderr, "%s %s %s\n", irc->password, cmd[2], cmd[3] );
		fprintf( stderr, "%d %s %s\n", ipc_child_recv_fd, irc->user->nick, irc->password );
	}
}

static const command_t ipc_child_commands[] = {
	{ "die",        0, ipc_child_cmd_die,         0 },
	{ "wallops",    1, ipc_child_cmd_wallops,     0 },
	{ "wall",       1, ipc_child_cmd_wall,        0 },
	{ "opermsg",    1, ipc_child_cmd_opermsg,     0 },
	{ "rehash",     0, ipc_child_cmd_rehash,      0 },
	{ "kill",       2, ipc_child_cmd_kill,        0 },
	{ "hello",      0, ipc_child_cmd_hello,       0 },
	{ "takeover",   1, ipc_child_cmd_takeover,    0 },
	{ NULL }
};

gboolean ipc_child_identify( irc_t *irc )
{
	if( global.conf->runmode == RUNMODE_FORKDAEMON )
	{
		if( !ipc_send_fd( global.listen_socket, irc->fd ) )
			ipc_child_disable();
	
		ipc_to_master_str( "IDENTIFY %s :%s\r\n", irc->user->nick, irc->password );
		
		return TRUE;
	}
	else
		return FALSE;
}

static void ipc_command_exec( void *data, char **cmd, const command_t *commands )
{
	int i, j;
	
	if( !cmd[0] )
		return;
	
	for( i = 0; commands[i].command; i ++ )
		if( g_strcasecmp( commands[i].command, cmd[0] ) == 0 )
		{
			/* There is no typo in this line: */
			for( j = 1; cmd[j]; j ++ ); j --;
			
			if( j < commands[i].required_parameters )
				break;
			
			if( commands[i].flags & IPC_CMD_TO_CHILDREN )
				ipc_to_children( cmd );
			else
				commands[i].execute( data, cmd );
			
			break;
		}
}

/* Return just one line. Returns NULL if something broke, an empty string
   on temporary "errors" (EAGAIN and friends). */
static char *ipc_readline( int fd, int *recv_fd )
{
	struct msghdr msg;
	struct iovec iov;
	char ccmsg[CMSG_SPACE(sizeof(recv_fd))];
	struct cmsghdr *cmsg;
	char buf[513], *eol;
	int size;
	
	/* Because this is internal communication, it should be pretty safe
	   to just peek at the message, find its length (by searching for the
	   end-of-line) and then just read that message. With internal
	   sockets and limites message length, messages should always be
	   complete. Saves us quite a lot of code and buffering. */
	size = recv( fd, buf, sizeof( buf ) - 1, MSG_PEEK );
	if( size == 0 || ( size < 0 && !sockerr_again() ) )
		return NULL;
	else if( size < 0 ) /* && sockerr_again() */
		return( g_strdup( "" ) );
	else
		buf[size] = 0;
	
	if( ( eol = strstr( buf, "\r\n" ) ) == NULL )
		return NULL;
	else
		size = eol - buf + 2;
	
	iov.iov_base = buf;
	iov.iov_len = size;
	
	memset( &msg, 0, sizeof( msg ) );
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = ccmsg;
	msg.msg_controllen = sizeof( ccmsg );
	
	if( recvmsg( fd, &msg, 0 ) != size )
		return NULL;
	
	if( recv_fd )
		for( cmsg = CMSG_FIRSTHDR( &msg ); cmsg; cmsg = CMSG_NXTHDR( &msg, cmsg ) )
			if( cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS )
			{
				/* Getting more than one shouldn't happen but if it does,
				   make sure we don't leave them around. */
				if( *recv_fd != -1 )
					close( *recv_fd );
				
				*recv_fd = *(int*) CMSG_DATA( cmsg );
				fprintf( stderr, "pid %d received fd %d\n", (int) getpid(), *recv_fd );
			}
	
	fprintf( stderr, "pid %d received: %s", (int) getpid(), buf );
	return g_strndup( buf, size - 2 );
}

gboolean ipc_master_read( gpointer data, gint source, b_input_condition cond )
{
	struct bitlbee_child *child = data;
	char *buf, **cmd;
	
	if( ( buf = ipc_readline( source, &child->to_fd ) ) )
	{
		cmd = irc_parse_line( buf );
		if( cmd )
		{
			ipc_command_exec( child, cmd, ipc_master_commands );
			g_free( cmd );
		}
		g_free( buf );
	}
	else
	{
		ipc_master_free_fd( source );
	}
	
	return TRUE;
}

gboolean ipc_child_read( gpointer data, gint source, b_input_condition cond )
{
	char *buf, **cmd;
	
	if( ( buf = ipc_readline( source, &ipc_child_recv_fd ) ) )
	{
		cmd = irc_parse_line( buf );
		if( cmd )
		{
			ipc_command_exec( data, cmd, ipc_child_commands );
			g_free( cmd );
		}
		g_free( buf );
	}
	else
	{
		ipc_child_disable();
	}
	
	return TRUE;
}

void ipc_to_master( char **cmd )
{
	if( global.conf->runmode == RUNMODE_FORKDAEMON )
	{
		char *s = irc_build_line( cmd );
		ipc_to_master_str( "%s", s );
		g_free( s );
	}
	else if( global.conf->runmode == RUNMODE_DAEMON )
	{
		ipc_command_exec( NULL, cmd, ipc_master_commands );
	}
}

void ipc_to_master_str( char *format, ... )
{
	char *msg_buf;
	va_list params;

	va_start( params, format );
	msg_buf = g_strdup_vprintf( format, params );
	va_end( params );
	
	if( strlen( msg_buf ) > 512 )
	{
		/* Don't send it, it's too long... */
	}
	else if( global.conf->runmode == RUNMODE_FORKDAEMON )
	{
		if( global.listen_socket >= 0 )
			if( write( global.listen_socket, msg_buf, strlen( msg_buf ) ) <= 0 )
				ipc_child_disable();
	}
	else if( global.conf->runmode == RUNMODE_DAEMON )
	{
		char **cmd, *s;
		
		if( ( s = strchr( msg_buf, '\r' ) ) )
			*s = 0;
		
		cmd = irc_parse_line( msg_buf );
		ipc_command_exec( NULL, cmd, ipc_master_commands );
		g_free( cmd );
	}
	
	g_free( msg_buf );
}

void ipc_to_children( char **cmd )
{
	if( global.conf->runmode == RUNMODE_FORKDAEMON )
	{
		char *msg_buf = irc_build_line( cmd );
		ipc_to_children_str( "%s", msg_buf );
		g_free( msg_buf );
	}
	else if( global.conf->runmode == RUNMODE_DAEMON )
	{
		GSList *l;
		
		for( l = irc_connection_list; l; l = l->next )
			ipc_command_exec( l->data, cmd, ipc_child_commands );
	}
}

void ipc_to_children_str( char *format, ... )
{
	char *msg_buf;
	va_list params;

	va_start( params, format );
	msg_buf = g_strdup_vprintf( format, params );
	va_end( params );
	
	if( strlen( msg_buf ) > 512 )
	{
		/* Don't send it, it's too long... */
	}
	else if( global.conf->runmode == RUNMODE_FORKDAEMON )
	{
		int msg_len = strlen( msg_buf );
		GSList *l, *next;
		
		for( l = child_list; l; l = next )
		{
			struct bitlbee_child *c = l->data;
			
			next = l->next;
			if( write( c->ipc_fd, msg_buf, msg_len ) <= 0 )
			{
				ipc_master_free_one( c );
				child_list = g_slist_remove( child_list, c );
			}
		}
	}
	else if( global.conf->runmode == RUNMODE_DAEMON )
	{
		char **cmd, *s;
		
		if( ( s = strchr( msg_buf, '\r' ) ) )
			*s = 0;
		
		cmd = irc_parse_line( msg_buf );
		ipc_to_children( cmd );
		g_free( cmd );
	}
	
	g_free( msg_buf );
}

static gboolean ipc_send_fd( int fd, int send_fd )
{
	struct msghdr msg;
	struct iovec iov;
	char ccmsg[CMSG_SPACE(sizeof(fd))];
	struct cmsghdr *cmsg;
	
	memset( &msg, 0, sizeof( msg ) );
	iov.iov_base = "0x90\r\n";
	iov.iov_len = 6;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	
	msg.msg_control = ccmsg;
	msg.msg_controllen = sizeof( ccmsg );
	cmsg = CMSG_FIRSTHDR( &msg );
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	cmsg->cmsg_len = CMSG_LEN( sizeof( send_fd ) );
	*(int*)CMSG_DATA( cmsg ) = send_fd;
	msg.msg_controllen = cmsg->cmsg_len;
	
	return sendmsg( fd, &msg, 0 ) == 6;
}

void ipc_master_free_one( struct bitlbee_child *c )
{
	b_event_remove( c->ipc_inpa );
	closesocket( c->ipc_fd );
	
	if( c->to_fd != -1 )
		close( c->to_fd );
	
	g_free( c->host );
	g_free( c->nick );
	g_free( c->realname );
	g_free( c->password );
	g_free( c );
}

void ipc_master_free_fd( int fd )
{
	GSList *l;
	struct bitlbee_child *c;
	
	for( l = child_list; l; l = l->next )
	{
		c = l->data;
		if( c->ipc_fd == fd )
		{
			ipc_master_free_one( c );
			child_list = g_slist_remove( child_list, c );
			break;
		}
	}
}

void ipc_master_free_all()
{
	GSList *l;
	
	for( l = child_list; l; l = l->next )
		ipc_master_free_one( l->data );
	
	g_slist_free( child_list );
	child_list = NULL;
}

void ipc_child_disable()
{
	b_event_remove( global.listen_watch_source_id );
	close( global.listen_socket );
	
	global.listen_socket = -1;
}

#ifndef _WIN32
char *ipc_master_save_state()
{
	char *fn = g_strdup( "/tmp/bee-restart.XXXXXX" );
	int fd = mkstemp( fn );
	GSList *l;
	FILE *fp;
	int i;
	
	if( fd == -1 )
	{
		log_message( LOGLVL_ERROR, "Could not create temporary file: %s", strerror( errno ) );
		g_free( fn );
		return NULL;
	}
	
	/* This is more convenient now. */
	fp = fdopen( fd, "w" );
	
	for( l = child_list, i = 0; l; l = l->next )
		i ++;
	
	/* Number of client processes. */
	fprintf( fp, "%d\n", i );
	
	for( l = child_list; l; l = l->next )
		fprintf( fp, "%d %d\n", (int) ((struct bitlbee_child*)l->data)->pid,
		                        ((struct bitlbee_child*)l->data)->ipc_fd );
	
	if( fclose( fp ) == 0 )
	{
		return fn;
	}
	else
	{
		unlink( fn );
		g_free( fn );
		return NULL;
	}
}


static gboolean new_ipc_client( gpointer data, gint serversock, b_input_condition cond )
{
	struct bitlbee_child *child = g_new0( struct bitlbee_child, 1 );
	
	child->to_fd = -1;
	child->ipc_fd = accept( serversock, NULL, 0 );
	if( child->ipc_fd == -1 )
	{
		log_message( LOGLVL_WARNING, "Unable to accept connection on UNIX domain socket: %s", strerror(errno) );
		return TRUE;
	}
		
	child->ipc_inpa = b_input_add( child->ipc_fd, B_EV_IO_READ, ipc_master_read, child );
	
	child_list = g_slist_prepend( child_list, child );
	
	return TRUE;
}

int ipc_master_listen_socket()
{
	struct sockaddr_un un_addr;
	int serversock;

	/* Clean up old socket files that were hanging around.. */
	if (unlink(IPCSOCKET) == -1 && errno != ENOENT) {
		log_message( LOGLVL_ERROR, "Could not remove old IPC socket at %s: %s", IPCSOCKET, strerror(errno) );
		return 0;
	}

	un_addr.sun_family = AF_UNIX;
	strcpy(un_addr.sun_path, IPCSOCKET);

	serversock = socket(AF_UNIX, SOCK_STREAM, PF_UNIX);

	if (serversock == -1) {
		log_message( LOGLVL_WARNING, "Unable to create UNIX socket: %s", strerror(errno) );
		return 0;
	}

	if (bind(serversock, (struct sockaddr *)&un_addr, sizeof(un_addr)) == -1) {
		log_message( LOGLVL_WARNING, "Unable to bind UNIX socket to %s: %s", IPCSOCKET, strerror(errno) );
		return 0;
	}

	if (listen(serversock, 5) == -1) {
		log_message( LOGLVL_WARNING, "Unable to listen on UNIX socket: %s", strerror(errno) );
		return 0;
	}
	
	b_input_add( serversock, B_EV_IO_READ, new_ipc_client, NULL );
	
	return 1;
}
#else
int ipc_master_listen_socket()
{
	/* FIXME: Open named pipe \\.\BITLBEE */
	return 0;
}
#endif

int ipc_master_load_state( char *statefile )
{
	struct bitlbee_child *child;
	FILE *fp;
	int i, n;
	
	if( statefile == NULL )
		return 0;
	
	fp = fopen( statefile, "r" );
	unlink( statefile );	/* Why do it later? :-) */
	if( fp == NULL )
		return 0;
	
	if( fscanf( fp, "%d", &n ) != 1 )
	{
		log_message( LOGLVL_WARNING, "Could not import state information for child processes." );
		fclose( fp );
		return 0;
	}
	
	log_message( LOGLVL_INFO, "Importing information for %d child processes.", n );
	for( i = 0; i < n; i ++ )
	{
		child = g_new0( struct bitlbee_child, 1 );
		
		if( fscanf( fp, "%d %d", (int *) &child->pid, &child->ipc_fd ) != 2 )
		{
			log_message( LOGLVL_WARNING, "Unexpected end of file: Only processed %d clients.", i );
			g_free( child );
			fclose( fp );
			return 0;
		}
		child->ipc_inpa = b_input_add( child->ipc_fd, B_EV_IO_READ, ipc_master_read, child );
		child->to_fd = -1;
		
		child_list = g_slist_prepend( child_list, child );
	}
	
	ipc_to_children_str( "HELLO\r\n" );
	ipc_to_children_str( "OPERMSG :New BitlBee master process started (version " BITLBEE_VERSION ")\r\n" );
	
	fclose( fp );
	return 1;
}
