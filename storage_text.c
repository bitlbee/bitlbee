  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2004 Wilmer van der Gaast and others                *
  \********************************************************************/

/* Storage backend that uses the same file format as <=1.0 */

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
#include "crypting.h"

static void text_init (void)
{
	/* Don't complain about the configuration directory anymore, leave it
	   up to the XML storage module, which uses the same directory for it
	   anyway. Nobody should be using just the text plugin anymore since
	   it's read only! */
}

static storage_status_t text_load( irc_t *irc, const char* password )
{
	char s[512];
	char *line;
	int proto;
	char nick[MAX_NICK_LENGTH+1];
	FILE *fp;
	user_t *ru = user_find( irc, ROOT_NICK );
	account_t *acc, *acc_lookup[9];
	
	g_snprintf( s, 511, "%s%s%s", global.conf->configdir, irc->nick, ".accounts" );
   	fp = fopen( s, "r" );
   	if( !fp ) return STORAGE_NO_SUCH_USER;
	
	fscanf( fp, "%32[^\n]s", s );

	if( checkpass( password, s ) != 0 )
	{
		fclose( fp );
		return STORAGE_INVALID_PASSWORD;
	}
	
	while( fscanf( fp, "%511[^\n]s", s ) > 0 )
	{
		fgetc( fp );
		line = deobfucrypt( s, password );
		if (line == NULL) return STORAGE_OTHER_ERROR;
		root_command_string( irc, ru, line, 0 );
		g_free( line );
	}
	fclose( fp );
	
	/* Build a list with the first listed account of every protocol
	   number. So if the user had nicks defined for a second account on
	   the same IM network, those nicks will be added to the wrong
	   account, and the user should rename those buddies again. But at
	   least from now on things will be saved properly. */
	memset( acc_lookup, 0, sizeof( acc_lookup ) );
	for( acc = irc->accounts; acc; acc = acc->next )
	{
		if( acc_lookup[0] == NULL && strcmp( acc->prpl->name, "oscar" ) == 0 )
			acc_lookup[0] = acc_lookup[1] = acc_lookup[3] = acc;
		else if( acc_lookup[2] == NULL && strcmp( acc->prpl->name, "yahoo" ) == 0 )
			acc_lookup[2] = acc;
		else if( acc_lookup[4] == NULL && strcmp( acc->prpl->name, "msn" ) == 0 )
			acc_lookup[4] = acc;
		else if( acc_lookup[8] == NULL && strcmp( acc->prpl->name, "jabber" ) == 0 )
			acc_lookup[8] = acc;
	}
	
	g_snprintf( s, 511, "%s%s%s", global.conf->configdir, irc->nick, ".nicks" );
	fp = fopen( s, "r" );
	if( !fp ) return STORAGE_NO_SUCH_USER;
	while( fscanf( fp, "%s %d %s", s, &proto, nick ) > 0 )
	{
		if( proto < 0 || proto > 8 || ( acc = acc_lookup[proto] ) == NULL )
			continue;
		
		http_decode( s );
		nick_set( acc, s, nick );
	}
	fclose( fp );
	
	return STORAGE_OK;
}

static storage_status_t text_check_pass( const char *nick, const char *password )
{
	char s[512];
	FILE *fp;
	
	g_snprintf( s, 511, "%s%s%s", global.conf->configdir, nick, ".accounts" );
	fp = fopen( s, "r" );
	if (!fp)
		return STORAGE_NO_SUCH_USER;

	fscanf( fp, "%32[^\n]s", s );
	fclose( fp );

	if (checkpass( password, s) == -1)
		return STORAGE_INVALID_PASSWORD;

	return STORAGE_OK;
}

static storage_status_t text_remove( const char *nick, const char *password )
{
	char s[512];
	storage_status_t status;

	status = text_check_pass( nick, password );
	if (status != STORAGE_OK)
		return status;

	g_snprintf( s, 511, "%s%s%s", global.conf->configdir, nick, ".accounts" );
	if (unlink( s ) == -1)
		return STORAGE_OTHER_ERROR;
	
	g_snprintf( s, 511, "%s%s%s", global.conf->configdir, nick, ".nicks" );
	if (unlink( s ) == -1)
		return STORAGE_OTHER_ERROR;

	return STORAGE_OK;
}

storage_t storage_text = {
	.name = "text",
	.init = text_init,
	.check_pass = text_check_pass,
	.remove = text_remove,
	.load = text_load
};
