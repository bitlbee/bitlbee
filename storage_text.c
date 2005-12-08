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
	if( access( global.conf->configdir, F_OK ) != 0 )
		log_message( LOGLVL_WARNING, "The configuration directory %s does not exist. Configuration won't be saved.", CONFIG );
	else if( access( global.conf->configdir, R_OK ) != 0 || access( global.conf->configdir, W_OK ) != 0 )
		log_message( LOGLVL_WARNING, "Permission problem: Can't read/write from/to %s.", global.conf->configdir );
}

static storage_status_t text_load ( const char *my_nick, const char* password, irc_t *irc )
{
	char s[512];
	char *line;
	int proto;
	char nick[MAX_NICK_LENGTH+1];
	FILE *fp;
	user_t *ru = user_find( irc, ROOT_NICK );
	
	if( irc->status == USTATUS_IDENTIFIED )
		return( 1 );
	
	g_snprintf( s, 511, "%s%s%s", global.conf->configdir, my_nick, ".accounts" );
   	fp = fopen( s, "r" );
   	if( !fp ) return STORAGE_NO_SUCH_USER;
	
	fscanf( fp, "%32[^\n]s", s );
	if( setpass( irc, password, s ) < 0 )
	{
		fclose( fp );
		return STORAGE_INVALID_PASSWORD;
	}
	
	/* Do this now. If the user runs with AuthMode = Registered, the
	   account command will not work otherwise. */
	irc->status = USTATUS_IDENTIFIED;
	
	while( fscanf( fp, "%511[^\n]s", s ) > 0 )
	{
		fgetc( fp );
		line = deobfucrypt( irc, s );
		root_command_string( irc, ru, line, 0 );
		g_free( line );
	}
	fclose( fp );
	
	g_snprintf( s, 511, "%s%s%s", global.conf->configdir, my_nick, ".nicks" );
	fp = fopen( s, "r" );
	if( !fp ) return STORAGE_NO_SUCH_USER;
	while( fscanf( fp, "%s %d %s", s, &proto, nick ) > 0 )
	{
		http_decode( s );
		nick_set( irc, s, proto, nick );
	}
	fclose( fp );
	
	if( set_getint( irc, "auto_connect" ) )
	{
		strcpy( s, "account on" );	/* Can't do this directly because r_c_s alters the string */
		root_command_string( irc, ru, s, 0 );
	}
	
	return STORAGE_OK;
}

static storage_status_t text_save( irc_t *irc, int overwrite )
{
	char s[512];
	char path[512], new_path[512];
	char *line;
	nick_t *n;
	set_t *set;
	mode_t ou = umask( 0077 );
	account_t *a;
	FILE *fp;
	char *hash;

	if (!overwrite) {
		g_snprintf( path, 511, "%s%s%s", global.conf->configdir, irc->nick, ".accounts" );
		if (access( path, F_OK ) != -1)
			return STORAGE_ALREADY_EXISTS;
	
		g_snprintf( path, 511, "%s%s%s", global.conf->configdir, irc->nick, ".nicks" );
		if (access( path, F_OK ) != -1)
			return STORAGE_ALREADY_EXISTS;
	}
	
	/*\
	 *  [SH] Nothing should be saved if no password is set, because the
	 *  password is not set if it was wrong, or if one is not identified
	 *  yet. This means that a malicious user could easily overwrite
	 *  files owned by someone else:
	 *  a Bad Thing, methinks
	\*/

	/* [WVG] No? Really? */

	/*\
	 *  [SH] Okay, okay, it wasn't really Wilmer who said that, it was
	 *  me. I just thought it was funny.
	\*/
	
	hash = hashpass( irc );
	if( hash == NULL )
	{
		irc_usermsg( irc, "Please register yourself if you want to save your settings." );
		return( 0 );
	}
	
	g_snprintf( path, 511, "%s%s%s", global.conf->configdir, irc->nick, ".nicks~" );
	fp = fopen( path, "w" );
	if( !fp ) return STORAGE_OTHER_ERROR;
	for( n = irc->nicks; n; n = n->next )
	{
		strcpy( s, n->handle );
		s[169] = 0; /* Prevent any overflow (169 ~ 512 / 3) */
		http_encode( s );
		g_snprintf( s + strlen( s ), 510 - strlen( s ), " %d %s", n->proto, n->nick );
		if( fprintf( fp, "%s\n", s ) != strlen( s ) + 1 )
		{
			irc_usermsg( irc, "fprintf() wrote too little. Disk full?" );
			fclose( fp );
			return STORAGE_OTHER_ERROR;
		}
	}
	if( fclose( fp ) != 0 )
	{
		irc_usermsg( irc, "fclose() reported an error. Disk full?" );
		return STORAGE_OTHER_ERROR;
	}
  
	g_snprintf( new_path, 512, "%s%s%s", global.conf->configdir, irc->nick, ".nicks" );
	if( unlink( new_path ) != 0 )
	{
		if( errno != ENOENT )
		{
			irc_usermsg( irc, "Error while removing old .nicks file" );
			return STORAGE_OTHER_ERROR;
		}
	}
	if( rename( path, new_path ) != 0 )
	{
		irc_usermsg( irc, "Error while renaming new .nicks file" );
		return STORAGE_OTHER_ERROR;
	}
	
	g_snprintf( path, 511, "%s%s%s", global.conf->configdir, irc->nick, ".accounts~" );
	fp = fopen( path, "w" );
	if( !fp ) return STORAGE_OTHER_ERROR;
	if( fprintf( fp, "%s", hash ) != strlen( hash ) )
	{
		irc_usermsg( irc, "fprintf() wrote too little. Disk full?" );
		fclose( fp );
		return STORAGE_OTHER_ERROR;
	}
	g_free( hash );

	for( a = irc->accounts; a; a = a->next )
	{
		if( a->protocol == PROTO_OSCAR || a->protocol == PROTO_ICQ || a->protocol == PROTO_TOC )
			g_snprintf( s, sizeof( s ), "account add oscar \"%s\" \"%s\" %s", a->user, a->pass, a->server );
		else
			g_snprintf( s, sizeof( s ), "account add %s \"%s\" \"%s\" \"%s\"",
			            proto_name[a->protocol], a->user, a->pass, a->server ? a->server : "" );
		
		line = obfucrypt( irc, s );
		if( *line )
		{
			if( fprintf( fp, "%s\n", line ) != strlen( line ) + 1 )
			{
				irc_usermsg( irc, "fprintf() wrote too little. Disk full?" );
				fclose( fp );
				return STORAGE_OTHER_ERROR;
			}
		}
		g_free( line );
	}
	
	for( set = irc->set; set; set = set->next )
	{
		if( set->value && set->def )
		{
			g_snprintf( s, sizeof( s ), "set %s \"%s\"", set->key, set->value );
			line = obfucrypt( irc, s );
			if( *line )
			{
				if( fprintf( fp, "%s\n", line ) != strlen( line ) + 1 )
				{
					irc_usermsg( irc, "fprintf() wrote too little. Disk full?" );
					fclose( fp );
					return STORAGE_OTHER_ERROR;
				}
			}
			g_free( line );
		}
	}
	
	if( strcmp( irc->mynick, ROOT_NICK ) != 0 )
	{
		g_snprintf( s, sizeof( s ), "rename %s %s", ROOT_NICK, irc->mynick );
		line = obfucrypt( irc, s );
		if( *line )
		{
			if( fprintf( fp, "%s\n", line ) != strlen( line ) + 1 )
			{
				irc_usermsg( irc, "fprintf() wrote too little. Disk full?" );
				fclose( fp );
				return STORAGE_OTHER_ERROR;
			}
		}
		g_free( line );
	}
	if( fclose( fp ) != 0 )
	{
		irc_usermsg( irc, "fclose() reported an error. Disk full?" );
		return STORAGE_OTHER_ERROR;
	}
	
 	g_snprintf( new_path, 512, "%s%s%s", global.conf->configdir, irc->nick, ".accounts" );
 	if( unlink( new_path ) != 0 )
	{
		if( errno != ENOENT )
		{
			irc_usermsg( irc, "Error while removing old .accounts file" );
			return STORAGE_OTHER_ERROR;
		}
	}
	if( rename( path, new_path ) != 0 )
	{
		irc_usermsg( irc, "Error while renaming new .accounts file" );
		return STORAGE_OTHER_ERROR;
	}
	
	umask( ou );
	
	return STORAGE_OK;
}

static storage_status_t text_check_pass( const char *nick, const char *password )
{
	char s[512];
	FILE *fp;
	
	g_snprintf( s, 511, "%s%s%s", global.conf->configdir, nick, ".nicks" );
	fp = fopen( s, "r" );
	if (!fp)
		return STORAGE_NO_SUCH_USER;

	fscanf( fp, "%32[^\n]s", s );
	fclose( fp );

	/*FIXME: Check password */

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
	.load = text_load,
	.save = text_save
};
