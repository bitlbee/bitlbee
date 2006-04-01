  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2004 Wilmer van der Gaast and others                *
  \********************************************************************/

/* User manager (root) commands                                         */

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
#include "commands.h"
#include "crypting.h"
#include "bitlbee.h"
#include "help.h"

#include <string.h>

void root_command_string( irc_t *irc, user_t *u, char *command, int flags )
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
	
	root_command( irc, cmd );
}

void root_command( irc_t *irc, char *cmd[] )
{	
	int i;
	
	if( !cmd[0] )
		return;
	
	for( i = 0; commands[i].command; i++ )
		if( g_strcasecmp( commands[i].command, cmd[0] ) == 0 )
		{
			if( !cmd[commands[i].required_parameters] )
			{
				irc_usermsg( irc, "Not enough parameters given (need %d)", commands[i].required_parameters );
				return;
			}
			commands[i].execute( irc, cmd );
			return;
		}
	
	irc_usermsg( irc, "Unknown command: %s. Please use \x02help commands\x02 to get a list of available commands.", cmd[0] );
}

static void cmd_help( irc_t *irc, char **cmd )
{
	char param[80];
	int i;
	char *s;
	
	memset( param, 0, sizeof(param) );
	for ( i = 1; (cmd[i] != NULL && ( strlen(param) < (sizeof(param)-1) ) ); i++ ) {
		if ( i != 1 )	// prepend space except for the first parameter
			strcat(param, " ");
		strncat( param, cmd[i], sizeof(param) - strlen(param) - 1 );
	}

	s = help_get( &(global.help), param );
	if( !s ) s = help_get( &(global.help), "" );
	
	if( s )
	{
		irc_usermsg( irc, "%s", s );
		g_free( s );
	}
	else
	{
		irc_usermsg( irc, "Error opening helpfile." );
	}
}

static void cmd_identify( irc_t *irc, char **cmd )
{
	storage_status_t status = storage_load( irc->nick, cmd[1], irc );
	
	switch (status) {
	case STORAGE_INVALID_PASSWORD:
		irc_usermsg( irc, "Incorrect password" );
		break;
	case STORAGE_NO_SUCH_USER:
		irc_usermsg( irc, "The nick is (probably) not registered" );
		break;
	case STORAGE_OK:
		irc_usermsg( irc, "Password accepted" );
		irc_umode_set( irc, "+R", 1 );
		break;
	default:
		irc_usermsg( irc, "Something very weird happened" );
		break;
	}
}

static void cmd_register( irc_t *irc, char **cmd )
{
	if( global.conf->authmode == AUTHMODE_REGISTERED )
	{
		irc_usermsg( irc, "This server does not allow registering new accounts" );
		return;
	}

	irc_setpass( irc, cmd[1] );
	switch( storage_save( irc, FALSE )) {
		case STORAGE_ALREADY_EXISTS:
			irc_usermsg( irc, "Nick is already registered" );
			break;
			
		case STORAGE_OK:
			irc->status = USTATUS_IDENTIFIED;
			irc_umode_set( irc, "+R", 1 );
			break;

		default:
			irc_usermsg( irc, "Error registering" );
			break;
	}
}

static void cmd_drop( irc_t *irc, char **cmd )
{
	storage_status_t status;
	
	status = storage_remove (irc->nick, cmd[1]);
	switch (status) {
	case STORAGE_NO_SUCH_USER:
		irc_usermsg( irc, "That account does not exist" );
		break;
	case STORAGE_INVALID_PASSWORD:
		irc_usermsg( irc, "Password invalid" );
		break;
	case STORAGE_OK:
		irc_setpass( irc, NULL );
		irc->status = USTATUS_LOGGED_IN;
		irc_umode_set( irc, "-R", 1 );
		irc_usermsg( irc, "Account `%s' removed", irc->nick );
		break;
	default:
		irc_usermsg( irc, "Error: '%d'", status );
		break;
	}
}

static void cmd_account( irc_t *irc, char **cmd )
{
	account_t *a;
	
	if( global.conf->authmode == AUTHMODE_REGISTERED && irc->status < USTATUS_IDENTIFIED )
	{
		irc_usermsg( irc, "This server only accepts registered users" );
		return;
	}
	
	if( g_strcasecmp( cmd[1], "add" ) == 0 )
	{
		struct prpl *prpl;
		
		if( cmd[2] == NULL || cmd[3] == NULL || cmd[4] == NULL )
		{
			irc_usermsg( irc, "Not enough parameters" );
			return;
		}
		
		prpl = find_protocol(cmd[2]);
		
		if( prpl == NULL )
		{
			irc_usermsg( irc, "Unknown protocol" );
			return;
		}

		a = account_add( irc, prpl, cmd[3], cmd[4] );
		
		if( cmd[5] )
			a->server = g_strdup( cmd[5] );
		
		irc_usermsg( irc, "Account successfully added" );
	}
	else if( g_strcasecmp( cmd[1], "del" ) == 0 )
	{
		if( !cmd[2] )
		{
			irc_usermsg( irc, "Not enough parameters given (need %d)", 2 );
		}
		else if( !( a = account_get( irc, cmd[2] ) ) )
		{
			irc_usermsg( irc, "Invalid account" );
		}
		else if( a->gc )
		{
			irc_usermsg( irc, "Account is still logged in, can't delete" );
		}
		else
		{
			account_del( irc, a );
			irc_usermsg( irc, "Account deleted" );
		}
	}
	else if( g_strcasecmp( cmd[1], "list" ) == 0 )
	{
		int i = 0;
		
		if( strchr( irc->umode, 'b' ) )
			irc_usermsg( irc, "Account list:" );
		
		for( a = irc->accounts; a; a = a->next )
		{
			char *con;
			
			if( a->gc && ( a->gc->flags & OPT_LOGGED_IN ) )
				con = " (connected)";
			else if( a->gc )
				con = " (connecting)";
			else if( a->reconnect )
				con = " (awaiting reconnect)";
			else
				con = "";
			
			irc_usermsg( irc, "%2d. %s, %s%s", i, a->prpl->name, a->user, con );
			
			i ++;
		}
		irc_usermsg( irc, "End of account list" );
	}
	else if( g_strcasecmp( cmd[1], "on" ) == 0 )
	{
		if( cmd[2] )
		{
			if( ( a = account_get( irc, cmd[2] ) ) )
			{
				if( a->gc )
				{
					irc_usermsg( irc, "Account already online" );
					return;
				}
				else
				{
					account_on( irc, a );
				}
			}
			else
			{
				irc_usermsg( irc, "Invalid account" );
				return;
			}
		}
		else
		{
			if ( irc->accounts ) {
				irc_usermsg( irc, "Trying to get all accounts connected..." );
			
				for( a = irc->accounts; a; a = a->next )
					if( !a->gc )
						account_on( irc, a );
			} 
			else
			{
				irc_usermsg( irc, "No accounts known. Use 'account add' to add one." );
			}
		}
	}
	else if( g_strcasecmp( cmd[1], "off" ) == 0 )
	{
		if( !cmd[2] )
		{
			irc_usermsg( irc, "Deactivating all active (re)connections..." );
			
			for( a = irc->accounts; a; a = a->next )
			{
				if( a->gc )
					account_off( irc, a );
				else if( a->reconnect )
					cancel_auto_reconnect( a );
			}
		}
		else if( ( a = account_get( irc, cmd[2] ) ) )
		{
			if( a->gc )
			{
				account_off( irc, a );
			}
			else if( a->reconnect )
			{
				cancel_auto_reconnect( a );
				irc_usermsg( irc, "Reconnect cancelled" );
			}
			else
			{
				irc_usermsg( irc, "Account already offline" );
				return;
			}
		}
		else
		{
			irc_usermsg( irc, "Invalid account" );
			return;
		}
	}
	else
	{
		irc_usermsg( irc, "Unknown command: account %s. Please use \x02help commands\x02 to get a list of available commands.", cmd[1] );
	}
}

static void cmd_add( irc_t *irc, char **cmd )
{
	account_t *a;
	
	if( !( a = account_get( irc, cmd[1] ) ) )
	{
		irc_usermsg( irc, "Invalid account" );
		return;
	}
	else if( !( a->gc && ( a->gc->flags & OPT_LOGGED_IN ) ) )
	{
		irc_usermsg( irc, "That account is not on-line" );
		return;
	}
	
	if( cmd[3] )
	{
		if( !nick_ok( cmd[3] ) )
		{
			irc_usermsg( irc, "The requested nick `%s' is invalid", cmd[3] );
			return;
		}
		else if( user_find( irc, cmd[3] ) )
		{
			irc_usermsg( irc, "The requested nick `%s' already exists", cmd[3] );
			return;
		}
		else
		{
			nick_set( irc, cmd[2], a->gc->prpl, cmd[3] );
		}
	}
	a->gc->prpl->add_buddy( a->gc, cmd[2] );
	add_buddy( a->gc, NULL, cmd[2], cmd[2] );
	
	irc_usermsg( irc, "User `%s' added to your contact list as `%s'", cmd[2], user_findhandle( a->gc, cmd[2] )->nick );
}

static void cmd_info( irc_t *irc, char **cmd )
{
	struct gaim_connection *gc;
	account_t *a;
	
	if( !cmd[2] )
	{
		user_t *u = user_find( irc, cmd[1] );
		if( !u || !u->gc )
		{
			irc_usermsg( irc, "Nick `%s' does not exist", cmd[1] );
			return;
		}
		gc = u->gc;
		cmd[2] = u->handle;
	}
	else if( !( a = account_get( irc, cmd[1] ) ) )
	{
		irc_usermsg( irc, "Invalid account" );
		return;
	}
	else if( !( ( gc = a->gc ) && ( a->gc->flags & OPT_LOGGED_IN ) ) )
	{
		irc_usermsg( irc, "That account is not on-line" );
		return;
	}
	
	if( !gc->prpl->get_info )
	{
		irc_usermsg( irc, "Command `%s' not supported by this protocol", cmd[0] );
	}
	else
	{
		gc->prpl->get_info( gc, cmd[2] );
	}
}

static void cmd_rename( irc_t *irc, char **cmd )
{
	user_t *u;
	
	if( g_strcasecmp( cmd[1], irc->nick ) == 0 )
	{
		irc_usermsg( irc, "Nick `%s' can't be changed", cmd[1] );
	}
	else if( user_find( irc, cmd[2] ) && ( nick_cmp( cmd[1], cmd[2] ) != 0 ) )
	{
		irc_usermsg( irc, "Nick `%s' already exists", cmd[2] );
	}
	else if( !nick_ok( cmd[2] ) )
	{
		irc_usermsg( irc, "Nick `%s' is invalid", cmd[2] );
	}
	else if( !( u = user_find( irc, cmd[1] ) ) )
	{
		irc_usermsg( irc, "Nick `%s' does not exist", cmd[1] );
	}
	else
	{
		user_rename( irc, cmd[1], cmd[2] );
		irc_write( irc, ":%s!%s@%s NICK %s", cmd[1], u->user, u->host, cmd[2] );
		if( g_strcasecmp( cmd[1], irc->mynick ) == 0 )
		{
			g_free( irc->mynick );
			irc->mynick = g_strdup( cmd[2] );
		}
		else if( u->send_handler == buddy_send_handler )
		{
			nick_set( irc, u->handle, u->gc->prpl, cmd[2] );
		}
		
		irc_usermsg( irc, "Nick successfully changed" );
	}
}

static void cmd_remove( irc_t *irc, char **cmd )
{
	user_t *u;
	char *s;
	
	if( !( u = user_find( irc, cmd[1] ) ) || !u->gc )
	{
		irc_usermsg( irc, "Buddy `%s' not found", cmd[1] );
		return;
	}
	s = g_strdup( u->handle );
	
	u->gc->prpl->remove_buddy( u->gc, u->handle, NULL );
	user_del( irc, cmd[1] );
	nick_del( irc, cmd[1] );
	
	irc_usermsg( irc, "Buddy `%s' (nick %s) removed from contact list", s, cmd[1] );
	g_free( s );
	
	return;
}

static void cmd_block( irc_t *irc, char **cmd )
{
	struct gaim_connection *gc;
	account_t *a;
	
	if( !cmd[2] && ( a = account_get( irc, cmd[1] ) ) && a->gc )
	{
		char *format;
		GSList *l;
		
		if( strchr( irc->umode, 'b' ) != NULL )
			format = "%s\t%s";
		else
			format = "%-32.32  %-16.16s";
		
		irc_usermsg( irc, format, "Handle", "Nickname" );
		for( l = a->gc->deny; l; l = l->next )
		{
			user_t *u = user_findhandle( a->gc, l->data );
			irc_usermsg( irc, format, l->data, u ? u->nick : "(none)" );
		}
		irc_usermsg( irc, "End of list." );
		
		return;
	}
	else if( !cmd[2] )
	{
		user_t *u = user_find( irc, cmd[1] );
		if( !u || !u->gc )
		{
			irc_usermsg( irc, "Nick `%s' does not exist", cmd[1] );
			return;
		}
		gc = u->gc;
		cmd[2] = u->handle;
	}
	else if( !( a = account_get( irc, cmd[1] ) ) )
	{
		irc_usermsg( irc, "Invalid account" );
		return;
	}
	else if( !( ( gc = a->gc ) && ( a->gc->flags & OPT_LOGGED_IN ) ) )
	{
		irc_usermsg( irc, "That account is not on-line" );
		return;
	}
	
	if( !gc->prpl->add_deny || !gc->prpl->rem_permit )
	{
		irc_usermsg( irc, "Command `%s' not supported by this protocol", cmd[0] );
	}
	else
	{
		gc->prpl->rem_permit( gc, cmd[2] );
		gc->prpl->add_deny( gc, cmd[2] );
		irc_usermsg( irc, "Buddy `%s' moved from your permit- to your deny-list", cmd[2] );
	}
}

static void cmd_allow( irc_t *irc, char **cmd )
{
	struct gaim_connection *gc;
	account_t *a;
	
	if( !cmd[2] && ( a = account_get( irc, cmd[1] ) ) && a->gc )
	{
		char *format;
		GSList *l;
		
		if( strchr( irc->umode, 'b' ) != NULL )
			format = "%s\t%s";
		else
			format = "%-32.32  %-16.16s";
		
		irc_usermsg( irc, format, "Handle", "Nickname" );
		for( l = a->gc->deny; l; l = l->next )
		{
			user_t *u = user_findhandle( a->gc, l->data );
			irc_usermsg( irc, format, l->data, u ? u->nick : "(none)" );
		}
		irc_usermsg( irc, "End of list." );
		
		return;
	}
	else if( !cmd[2] )
	{
		user_t *u = user_find( irc, cmd[1] );
		if( !u || !u->gc )
		{
			irc_usermsg( irc, "Nick `%s' does not exist", cmd[1] );
			return;
		}
		gc = u->gc;
		cmd[2] = u->handle;
	}
	else if( !( a = account_get( irc, cmd[1] ) ) )
	{
		irc_usermsg( irc, "Invalid account" );
		return;
	}
	else if( !( ( gc = a->gc ) && ( a->gc->flags & OPT_LOGGED_IN ) ) )
	{
		irc_usermsg( irc, "That account is not on-line" );
		return;
	}
	
	if( !gc->prpl->rem_deny || !gc->prpl->add_permit )
	{
		irc_usermsg( irc, "Command `%s' not supported by this protocol", cmd[0] );
	}
	else
	{
		gc->prpl->rem_deny( gc, cmd[2] );
		gc->prpl->add_permit( gc, cmd[2] );
		
		irc_usermsg( irc, "Buddy `%s' moved from your deny- to your permit-list", cmd[2] );
	}
}

static void cmd_yesno( irc_t *irc, char **cmd )
{
	query_t *q = NULL;
	int numq = 0;
	
	if( irc->queries == NULL )
	{
		irc_usermsg( irc, "Did I ask you something?" );
		return;
	}
	
	/* If there's an argument, the user seems to want to answer another question than the
	   first/last (depending on the query_order setting) one. */
	if( cmd[1] )
	{
		if( sscanf( cmd[1], "%d", &numq ) != 1 )
		{
			irc_usermsg( irc, "Invalid query number" );
			return;
		}
		
		for( q = irc->queries; q; q = q->next, numq -- )
			if( numq == 0 )
				break;
		
		if( !q )
		{
			irc_usermsg( irc, "Uhm, I never asked you something like that..." );
			return;
		}
	}
	
	if( g_strcasecmp( cmd[0], "yes" ) == 0 )
		query_answer( irc, q, 1 );
	else if( g_strcasecmp( cmd[0], "no" ) == 0 )
		query_answer( irc, q, 0 );
}

static void cmd_set( irc_t *irc, char **cmd )
{
	if( cmd[1] && cmd[2] )
	{
		set_setstr( irc, cmd[1], cmd[2] );
		
		if( ( strcmp( cmd[2], "=" ) ) == 0 && cmd[3] )
			irc_usermsg( irc, "Warning: Correct syntax: \002set <variable> <value>\002 (without =)" );
	}
	if( cmd[1] ) /* else 'forgotten' on purpose.. Must show new value after changing */
	{
		char *s = set_getstr( irc, cmd[1] );
		if( s )
			irc_usermsg( irc, "%s = `%s'", cmd[1], s );
	}
	else
	{
		set_t *s = irc->set;
		while( s )
		{
			if( s->value || s->def )
				irc_usermsg( irc, "%s = `%s'", s->key, s->value?s->value:s->def );
			s = s->next;
		}
	}
}

static void cmd_save( irc_t *irc, char **cmd )
{
	if( storage_save( irc, TRUE ) == STORAGE_OK )
		irc_usermsg( irc, "Configuration saved" );
	else
		irc_usermsg( irc, "Configuration could not be saved!" );
}

static void cmd_blist( irc_t *irc, char **cmd )
{
	int online = 0, away = 0, offline = 0;
	user_t *u;
	char s[256];
	char *format;
	int n_online = 0, n_away = 0, n_offline = 0;
	
	if( cmd[1] && g_strcasecmp( cmd[1], "all" ) == 0 )
		online = offline = away = 1;
	else if( cmd[1] && g_strcasecmp( cmd[1], "offline" ) == 0 )
		offline = 1;
	else if( cmd[1] && g_strcasecmp( cmd[1], "away" ) == 0 )
		away = 1;
	else if( cmd[1] && g_strcasecmp( cmd[1], "online" ) == 0 )
		online = 1;
	else
		online =  away = 1;
	
	if( strchr( irc->umode, 'b' ) != NULL )
		format = "%s\t%s\t%s";
	else
		format = "%-16.16s  %-40.40s  %s";
	
	irc_usermsg( irc, format, "Nick", "User/Host/Network", "Status" );
	
	for( u = irc->users; u; u = u->next ) if( u->gc && u->online && !u->away )
	{
		if( online == 1 )
		{
			g_snprintf( s, sizeof( s ) - 1, "%s@%s (%s)", u->user, u->host, u->gc->user->prpl->name );
			irc_usermsg( irc, format, u->nick, s, "Online" );
		}
		
		n_online ++;
	}

	for( u = irc->users; u; u = u->next ) if( u->gc && u->online && u->away )
	{
		if( away == 1 )
		{
			g_snprintf( s, sizeof( s ) - 1, "%s@%s (%s)", u->user, u->host, u->gc->user->prpl->name );
			irc_usermsg( irc, format, u->nick, s, u->away );
		}
		n_away ++;
	}
	
	for( u = irc->users; u; u = u->next ) if( u->gc && !u->online )
	{
		if( offline == 1 )
		{
			g_snprintf( s, sizeof( s ) - 1, "%s@%s (%s)", u->user, u->host, u->gc->user->prpl->name );
			irc_usermsg( irc, format, u->nick, s, "Offline" );
		}
		n_offline ++;
	}
	
	irc_usermsg( irc, "%d buddies (%d available, %d away, %d offline)", n_online + n_away + n_offline, n_online, n_away, n_offline );
}

static void cmd_nick( irc_t *irc, char **cmd ) 
{
	account_t *a;

	if( !cmd[1] || !( a = account_get( irc, cmd[1] ) ) )
	{
		irc_usermsg( irc, "Invalid account");
	}
	else if( !( a->gc && ( a->gc->flags & OPT_LOGGED_IN ) ) )
	{
		irc_usermsg( irc, "That account is not on-line" );
	}
	else if ( !cmd[2] ) 
	{
		irc_usermsg( irc, "Your name is `%s'" , a->gc->displayname ? a->gc->displayname : "NULL" );
	}
	else if ( !a->gc->prpl->set_info ) 
	{
		irc_usermsg( irc, "Command `%s' not supported by this protocol", cmd[0] );
	}
	else
	{
		irc_usermsg( irc, "Setting your name to `%s'", cmd[2] );
		
		a->gc->prpl->set_info( a->gc, cmd[2] );
	}
}

static void cmd_qlist( irc_t *irc, char **cmd )
{
	query_t *q = irc->queries;
	int num;
	
	if( !q )
	{
		irc_usermsg( irc, "There are no pending questions." );
		return;
	}
	
	irc_usermsg( irc, "Pending queries:" );
	
	for( num = 0; q; q = q->next, num ++ )
		if( q->gc ) /* Not necessary yet, but it might come later */
			irc_usermsg( irc, "%d, %s(%s): %s", num, q->gc->prpl->name, q->gc->username, q->question );
		else
			irc_usermsg( irc, "%d, BitlBee: %s", num, q->question );
}

static void cmd_import_buddies( irc_t *irc, char **cmd )
{
	struct gaim_connection *gc;
	account_t *a;
	nick_t *n;
	
	if( !( a = account_get( irc, cmd[1] ) ) )
	{
		irc_usermsg( irc, "Invalid account" );
		return;
	}
	else if( !( ( gc = a->gc ) && ( a->gc->flags & OPT_LOGGED_IN ) ) )
	{
		irc_usermsg( irc, "That account is not on-line" );
		return;
	}
	
	if( cmd[2] )
	{
		if( g_strcasecmp( cmd[2], "clear" ) == 0 )
		{
			user_t *u;
			
			for( u = irc->users; u; u = u->next )
				if( u->gc == gc )
				{
					u->gc->prpl->remove_buddy( u->gc, u->handle, NULL );
					user_del( irc, u->nick );
				}
			
			irc_usermsg( irc, "Old buddy list cleared." );
		}
		else
		{
			irc_usermsg( irc, "Invalid argument: %s", cmd[2] );
			return;
		}
	}
	
	for( n = gc->irc->nicks; n; n = n->next )
	{
		if( n->proto == gc->prpl && !user_findhandle( gc, n->handle ) )
		{
	                gc->prpl->add_buddy( gc, n->handle );
	                add_buddy( gc, NULL, n->handle, NULL );
		}
	}
	
	irc_usermsg( irc, "Sent all add requests. Please wait for a while, the server needs some time to handle all the adds." );
}

const command_t commands[] = {
	{ "help",           0, cmd_help,           0 }, 
	{ "identify",       1, cmd_identify,       0 },
	{ "register",       1, cmd_register,       0 },
	{ "drop",           1, cmd_drop,           0 },
	{ "account",        1, cmd_account,        0 },
	{ "add",            2, cmd_add,            0 },
	{ "info",           1, cmd_info,           0 },
	{ "rename",         2, cmd_rename,         0 },
	{ "remove",         1, cmd_remove,         0 },
	{ "block",          1, cmd_block,          0 },
	{ "allow",          1, cmd_allow,          0 },
	{ "save",           0, cmd_save,           0 },
	{ "set",            0, cmd_set,            0 },
	{ "yes",            0, cmd_yesno,          0 },
	{ "no",             0, cmd_yesno,          0 },
	{ "blist",          0, cmd_blist,          0 },
	{ "nick",           1, cmd_nick,           0 },
	{ "import_buddies", 1, cmd_import_buddies, 0 },
	{ "qlist",          0, cmd_qlist,          0 },
	{ NULL }
};
