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

command_t commands[] = {
	{ "help",           0, cmd_help }, 
	{ "identify",       1, cmd_identify },
	{ "register",       1, cmd_register },
	{ "drop",           1, cmd_drop },
	{ "account",        1, cmd_account },
	{ "add",            2, cmd_add },
	{ "info",           1, cmd_info },
	{ "rename",         2, cmd_rename },
	{ "remove",         1, cmd_remove },
	{ "block",          1, cmd_block },
	{ "allow",          1, cmd_allow },
	{ "save",           0, cmd_save },
	{ "set",            0, cmd_set },
	{ "yes",            0, cmd_yesno },
	{ "no",             0, cmd_yesno },
	{ "blist",          0, cmd_blist },
	{ "nick",           1, cmd_nick },
	{ "import_buddies", 1, cmd_import_buddies },
	{ "qlist",          0, cmd_qlist },
	{ NULL }
};

int cmd_help( irc_t *irc, char **cmd )
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
		return( 1 );
	}
	else
	{
		irc_usermsg( irc, "Error opening helpfile." );
		return( 0 );
	}
}

int cmd_identify( irc_t *irc, char **cmd )
{
	int checkie = bitlbee_load( irc, cmd[1] );
	
	if( checkie == -1 )
	{
		irc_usermsg( irc, "Incorrect password" );
	}
	else if( checkie == 0 )
	{
		irc_usermsg( irc, "The nick is (probably) not registered" );
	}
	else if( checkie == 1 )
	{
		irc_usermsg( irc, "Password accepted" );
	}
	else
	{
		irc_usermsg( irc, "Something very weird happened" );
	}
	
	return( 0 );
}

int cmd_register( irc_t *irc, char **cmd )
{
	int checkie;
	char path[512];
	
	if( global.conf->authmode == AUTHMODE_REGISTERED )
	{
		irc_usermsg( irc, "This server does not allow registering new accounts" );
		return( 0 );
	}
	
	g_snprintf( path, 511, "%s%s%s", global.conf->configdir, irc->nick, ".accounts" );
	checkie = access( path, F_OK );
	
	g_snprintf( path, 511, "%s%s%s", global.conf->configdir, irc->nick, ".nicks" );
	checkie += access( path, F_OK );
	
	if( checkie == -2 )
	{
		setpassnc( irc, cmd[1] );
		root_command_string( irc, user_find( irc, irc->mynick ), "save", 0 );
		irc->status = USTATUS_IDENTIFIED;
	}
	else
	{
		irc_usermsg( irc, "Nick is already registered" );
	}
	
	return( 0 );
}

int cmd_drop( irc_t *irc, char **cmd )
{
	char s[512];
	FILE *fp;
	
	g_snprintf( s, 511, "%s%s%s", global.conf->configdir, irc->nick, ".accounts" );
	fp = fopen( s, "r" );
	if( !fp )
	{
		irc_usermsg( irc, "That account does not exist" );
		return( 0 );
	}
	
	fscanf( fp, "%32[^\n]s", s );
	fclose( fp );
	if( setpass( irc, cmd[1], s ) < 0 )
	{
		irc_usermsg( irc, "Incorrect password" );
		return( 0 );
	}
	
	g_snprintf( s, 511, "%s%s%s", global.conf->configdir, irc->nick, ".accounts" );
	unlink( s );
	
	g_snprintf( s, 511, "%s%s%s", global.conf->configdir, irc->nick, ".nicks" );
	unlink( s );
	
	setpassnc( irc, NULL );
	irc_usermsg( irc, "Files belonging to account `%s' removed", irc->nick );
	
	return( 0 );
}

int cmd_account( irc_t *irc, char **cmd )
{
	account_t *a;
	
	if( global.conf->authmode == AUTHMODE_REGISTERED && irc->status < USTATUS_IDENTIFIED )
	{
		irc_usermsg( irc, "This server only accepts registered users" );
		return( 0 );
	}
	
	if( g_strcasecmp( cmd[1], "add" ) == 0 )
	{
		struct prpl *prpl;
		
		if( cmd[2] == NULL || cmd[3] == NULL || cmd[4] == NULL )
		{
			irc_usermsg( irc, "Not enough parameters" );
			return( 0 );
		}
		
		prpl = find_protocol(cmd[2]);
		
		if( prpl == NULL )
		{
			irc_usermsg( irc, "Unknown protocol" );
			return( 0 );
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
					return( 0 );
				}
				else
				{
					account_on( irc, a );
				}
			}
			else
			{
				irc_usermsg( irc, "Invalid account" );
				return( 0 );
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
				return( 0 );
			}
		}
		else
		{
			irc_usermsg( irc, "Invalid account" );
			return( 0 );
		}
	}
	else
	{
		irc_usermsg( irc, "Unknown command: account %s. Please use \x02help commands\x02 to get a list of available commands.", cmd[1] );
	}
	
	return( 1 );
}

int cmd_add( irc_t *irc, char **cmd )
{
	account_t *a;
	
	if( !( a = account_get( irc, cmd[1] ) ) )
	{
		irc_usermsg( irc, "Invalid account" );
		return( 1 );
	}
	else if( !( a->gc && ( a->gc->flags & OPT_LOGGED_IN ) ) )
	{
		irc_usermsg( irc, "That account is not on-line" );
		return( 1 );
	}
	
	if( cmd[3] )
	{
		if( !nick_ok( cmd[3] ) )
		{
			irc_usermsg( irc, "The requested nick `%s' is invalid", cmd[3] );
			return( 0 );
		}
		else if( user_find( irc, cmd[3] ) )
		{
			irc_usermsg( irc, "The requested nick `%s' already exists", cmd[3] );
			return( 0 );
		}
		else
		{
			nick_set( irc, cmd[2], a->gc->prpl, cmd[3] );
		}
	}
	a->gc->prpl->add_buddy( a->gc, cmd[2] );
	add_buddy( a->gc, NULL, cmd[2], cmd[2] );
	
	irc_usermsg( irc, "User `%s' added to your contact list as `%s'", cmd[2], user_findhandle( a->gc, cmd[2] )->nick );
	
	return( 0 );
}

int cmd_info( irc_t *irc, char **cmd )
{
	struct gaim_connection *gc;
	account_t *a;
	
	if( !cmd[2] )
	{
		user_t *u = user_find( irc, cmd[1] );
		if( !u || !u->gc )
		{
			irc_usermsg( irc, "Nick `%s' does not exist", cmd[1] );
			return( 1 );
		}
		gc = u->gc;
		cmd[2] = u->handle;
	}
	else if( !( a = account_get( irc, cmd[1] ) ) )
	{
		irc_usermsg( irc, "Invalid account" );
		return( 1 );
	}
	else if( !( ( gc = a->gc ) && ( a->gc->flags & OPT_LOGGED_IN ) ) )
	{
		irc_usermsg( irc, "That account is not on-line" );
		return( 1 );
	}
	
	if( !gc->prpl->get_info )
	{
		irc_usermsg( irc, "Command `%s' not supported by this protocol", cmd[0] );
		return( 1 );
	}
	gc->prpl->get_info( gc, cmd[2] );
	
	return( 0 );
}

int cmd_rename( irc_t *irc, char **cmd )
{
	user_t *u;
	
	if( g_strcasecmp( cmd[1], irc->nick ) == 0 )
	{
		irc_usermsg( irc, "Nick `%s' can't be changed", cmd[1] );
		return( 1 );
	}
	if( user_find( irc, cmd[2] ) && ( nick_cmp( cmd[1], cmd[2] ) != 0 ) )
	{
		irc_usermsg( irc, "Nick `%s' already exists", cmd[2] );
		return( 1 );
	}
	if( !nick_ok( cmd[2] ) )
	{
		irc_usermsg( irc, "Nick `%s' is invalid", cmd[2] );
		return( 1 );
	}
	if( !( u = user_find( irc, cmd[1] ) ) )
	{
		irc_usermsg( irc, "Nick `%s' does not exist", cmd[1] );
		return( 1 );
	}
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
	
	return( 0 );
}

int cmd_remove( irc_t *irc, char **cmd )
{
	user_t *u;
	char *s;
	
	if( !( u = user_find( irc, cmd[1] ) ) || !u->gc )
	{
		irc_usermsg( irc, "Buddy `%s' not found", cmd[1] );
		return( 1 );
	}
	s = g_strdup( u->handle );
	
	u->gc->prpl->remove_buddy( u->gc, u->handle, NULL );
	user_del( irc, cmd[1] );
	nick_del( irc, cmd[1] );
	
	irc_usermsg( irc, "Buddy `%s' (nick %s) removed from contact list", s, cmd[1] );
	g_free( s );
	
	return( 0 );
}

int cmd_block( irc_t *irc, char **cmd )
{
	struct gaim_connection *gc;
	account_t *a;
	
	if( !cmd[2] )
	{
		user_t *u = user_find( irc, cmd[1] );
		if( !u || !u->gc )
		{
			irc_usermsg( irc, "Nick `%s' does not exist", cmd[1] );
			return( 1 );
		}
		gc = u->gc;
		cmd[2] = u->handle;
	}
	else if( !( a = account_get( irc, cmd[1] ) ) )
	{
		irc_usermsg( irc, "Invalid account" );
		return( 1 );
	}
	else if( !( ( gc = a->gc ) && ( a->gc->flags & OPT_LOGGED_IN ) ) )
	{
		irc_usermsg( irc, "That account is not on-line" );
		return( 1 );
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
	
	return( 0 );
}

int cmd_allow( irc_t *irc, char **cmd )
{
	struct gaim_connection *gc;
	account_t *a;
	
	if( !cmd[2] )
	{
		user_t *u = user_find( irc, cmd[1] );
		if( !u || !u->gc )
		{
			irc_usermsg( irc, "Nick `%s' does not exist", cmd[1] );
			return( 1 );
		}
		gc = u->gc;
		cmd[2] = u->handle;
	}
	else if( !( a = account_get( irc, cmd[1] ) ) )
	{
		irc_usermsg( irc, "Invalid account" );
		return( 1 );
	}
	else if( !( ( gc = a->gc ) && ( a->gc->flags & OPT_LOGGED_IN ) ) )
	{
		irc_usermsg( irc, "That account is not on-line" );
		return( 1 );
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
	
	return( 0 );
}

int cmd_yesno( irc_t *irc, char **cmd )
{
	query_t *q = NULL;
	int numq = 0;
	
	if( irc->queries == NULL )
	{
		irc_usermsg( irc, "Did I ask you something?" );
		return( 0 );
	}
	
	/* If there's an argument, the user seems to want to answer another question than the
	   first/last (depending on the query_order setting) one. */
	if( cmd[1] )
	{
		if( sscanf( cmd[1], "%d", &numq ) != 1 )
		{
			irc_usermsg( irc, "Invalid query number" );
			return( 0 );
		}
		
		for( q = irc->queries; q; q = q->next, numq -- )
			if( numq == 0 )
				break;
		
		if( !q )
		{
			irc_usermsg( irc, "Uhm, I never asked you something like that..." );
			return( 0 );
		}
	}
	
	if( g_strcasecmp( cmd[0], "yes" ) == 0 )
		query_answer( irc, q, 1 );
	else if( g_strcasecmp( cmd[0], "no" ) == 0 )
		query_answer( irc, q, 0 );
	
	return( 1 );
}

int cmd_set( irc_t *irc, char **cmd )
{
	if( cmd[1] && cmd[2] )
	{
		set_setstr( irc, cmd[1], cmd[2] );
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
	
	return( 0 );
}

int cmd_save( irc_t *irc, char **cmd )
{
	if( bitlbee_save( irc ) )
		irc_usermsg( irc, "Configuration saved" );
	else
		irc_usermsg( irc, "Configuration could not be saved!" );
	
	return( 0 );
}

int cmd_blist( irc_t *irc, char **cmd )
{
	int online = 0, away = 0, offline = 0;
	user_t *u;
	char s[64];
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
	
	irc_usermsg( irc, "%-16.16s  %-40.40s  %s", "Nick", "User/Host/Network", "Status" );
	
	if( online == 1 ) for( u = irc->users; u; u = u->next ) if( u->gc && u->online && !u->away )
	{
		g_snprintf( s, 63, "%s@%s (%s)", u->user, u->host, u->gc->user->prpl->name );
		irc_usermsg( irc, "%-16.16s  %-40.40s  %s", u->nick, s, "Online" );
		n_online ++;
	}

	if( away == 1 ) for( u = irc->users; u; u = u->next ) if( u->gc && u->online && u->away )
	{
		g_snprintf( s, 63, "%s@%s (%s)", u->user, u->host, u->gc->user->prpl->name );
		irc_usermsg( irc, "%-16.16s  %-40.40s  %s", u->nick, s, u->away );
		n_away ++;
	}
	
	if( offline == 1 ) for( u = irc->users; u; u = u->next ) if( u->gc && !u->online )
	{
		g_snprintf( s, 63, "%s@%s (%s)", u->user, u->host, u->gc->user->prpl->name );
		irc_usermsg( irc, "%-16.16s  %-40.40s  %s", u->nick, s, "Offline" );
		n_offline ++;
	}
	
	irc_usermsg( irc, "%d buddies (%d available, %d away, %d offline)", n_online + n_away + n_offline, n_online, n_away, n_offline );
	
	return( 0 );
}

int cmd_nick( irc_t *irc, char **cmd ) 
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
		char utf8[1024];
		
		irc_usermsg( irc, "Setting your name to `%s'", cmd[2] );
		
		if( g_strncasecmp( set_getstr( irc, "charset" ), "none", 4 ) != 0 &&
		    do_iconv( set_getstr( irc, "charset" ), "UTF-8", cmd[2], utf8, 0, 1024 ) != -1 )
			a->gc->prpl->set_info( a->gc, utf8 );
		else
			a->gc->prpl->set_info( a->gc, cmd[2] );
	}
	
	return( 1 );
}

int cmd_qlist( irc_t *irc, char **cmd )
{
	query_t *q = irc->queries;
	int num;
	
	if( !q )
	{
		irc_usermsg( irc, "There are no pending questions." );
		return( 0 );
	}
	
	irc_usermsg( irc, "Pending queries:" );
	
	for( num = 0; q; q = q->next, num ++ )
		if( q->gc ) /* Not necessary yet, but it might come later */
			irc_usermsg( irc, "%d, %s(%s): %s", num, q->gc->prpl->name, q->gc->username, q->question );
		else
			irc_usermsg( irc, "%d, BitlBee: %s", num, q->question );
	
	return( 0 );
}

int cmd_import_buddies( irc_t *irc, char **cmd )
{
	struct gaim_connection *gc;
	account_t *a;
	nick_t *n;
	
	if( !( a = account_get( irc, cmd[1] ) ) )
	{
		irc_usermsg( irc, "Invalid account" );
		return( 0 );
	}
	else if( !( ( gc = a->gc ) && ( a->gc->flags & OPT_LOGGED_IN ) ) )
	{
		irc_usermsg( irc, "That account is not on-line" );
		return( 0 );
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
			return( 0 );
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
	
	return( 0 );
}
