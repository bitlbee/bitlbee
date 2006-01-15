  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2004 Wilmer van der Gaast and others                *
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

GSList *child_list = NULL;

static int ipc_master_cmd_die( irc_t *data, char **cmd )
{
	/* This shouldn't really be the final implementation... */
	exit( 0 );
}

static int ipc_master_cmd_wallops( irc_t *data, char **cmd )
{
	GSList *l;
	char msg_buf[513];
	int msg_len;
	
	if( ( msg_len = g_snprintf( msg_buf, sizeof( msg_buf ) - 1, "%s :%s\r\n", cmd[0], cmd[1] ) ) > ( sizeof( msg_buf ) - 1 ) )
		return 0;
	
	for( l = child_list; l; l = l->next )
	{
		struct bitlbee_child *c = l->data;
		write( c->ipc_fd, msg_buf, msg_len );
	}
	
	return 1;
}

static const command_t ipc_master_commands[] = {
	{ "die",        0, ipc_master_cmd_die,        0 },
	{ "wallops",    1, ipc_master_cmd_wallops,    1 },
	{ "lilo",       1, ipc_master_cmd_wallops,    1 },
	{ NULL }
};

static int ipc_child_cmd_wallops( irc_t *data, char **cmd )
{
	irc_t *irc = data;
	
	if( strchr( irc->umode, 'w' ) )
		irc_write( irc, ":%s WALLOPS :%s", irc->myhost, cmd[1] );
	
	return 1;
}

static int ipc_child_cmd_lilo( irc_t *data, char **cmd )
{
	irc_t *irc = data;
	
	irc_write( irc, ":%s NOTICE %s :%s", irc->myhost, irc->nick, cmd[1] );
	
	return 1;
}

static const command_t ipc_child_commands[] = {
	{ "wallops",    1, ipc_child_cmd_wallops,     1 },
	{ "lilo",       1, ipc_child_cmd_lilo,        1 },
	{ NULL }
};

static void ipc_command_exec( void *data, char **cmd, const command_t *commands )
{
	int i;
	
	if( !cmd[0] )
		return;
	
	for( i = 0; commands[i].command; i ++ )
		if( g_strcasecmp( commands[i].command, cmd[0] ) == 0 )
		{
			commands[i].execute( data, cmd );
			return;
		}
}

static char *ipc_readline( int fd )
{
	char *buf, *eol;
	int size;
	
	buf = g_new0( char, 513 );
	
	/* Because this is internal communication, it should be pretty safe
	   to just peek at the message, find its length (by searching for the
	   end-of-line) and then just read that message. With internal
	   sockets and limites message length, messages should always be
	   complete. Saves us quite a lot of code and buffering. */
	size = recv( fd, buf, 512, MSG_PEEK );
	if( size == 0 || ( size < 0 && !sockerr_again() ) )
		return NULL;
	else
		buf[size] = 0;
	
	eol = strstr( buf, "\r\n" );
	if( eol == NULL )
		return NULL;
	else
		size = eol - buf + 2;
	
	g_free( buf );
	buf = g_new0( char, size + 1 );
	
	if( recv( fd, buf, size, 0 ) != size )
		return NULL;
	else
		buf[size-2] = 0;
	
	return buf;
}

void ipc_master_read( gpointer data, gint source, GaimInputCondition cond )
{
	char *buf, **cmd;
	
	if( ( buf = ipc_readline( source ) ) )
	{
		cmd = irc_parse_line( buf );
		if( cmd )
			ipc_command_exec( data, cmd, ipc_master_commands );
	}
	else
	{
		GSList *l;
		struct bitlbee_child *c;
		
		for( l = child_list; l; l = l->next )
		{
			c = l->data;
			if( c->ipc_fd == source )
			{
				close( c->ipc_fd );
				gaim_input_remove( c->ipc_inpa );
				g_free( c );
				
				child_list = g_slist_remove( child_list, l );
				
				break;
			}
		}
	}
}

void ipc_child_read( gpointer data, gint source, GaimInputCondition cond )
{
	char *buf, **cmd;
	
	if( ( buf = ipc_readline( source ) ) )
	{
		cmd = irc_parse_line( buf );
		if( cmd )
			ipc_command_exec( data, cmd, ipc_child_commands );
	}
	else
	{
		gaim_input_remove( global.listen_watch_source_id );
		close( global.listen_socket );
		
		global.listen_socket = -1;
	}
}

void ipc_to_master( char **cmd )
{
	if( global.conf->runmode == RUNMODE_FORKDAEMON )
	{
		int i, len;
		char *s;
		
		len = 1;
		for( i = 0; cmd[i]; i ++ )
			len += strlen( cmd[i] ) + 1;
		
		if( strchr( cmd[i-1], ' ' ) != NULL )
			len ++;
		
		s = g_new0( char, len + 1 );
		for( i = 0; cmd[i]; i ++ )
		{
			if( cmd[i+1] == NULL && strchr( cmd[i], ' ' ) != NULL )
				strcat( s, ":" );
			
			strcat( s, cmd[i] );
			
			if( cmd[i+1] )
				strcat( s, " " );
		}
		strcat( s, "\r\n" );
		
		write( global.listen_socket, s, len );
	}
}
