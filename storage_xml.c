  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2006 Wilmer van der Gaast and others                *
  \********************************************************************/

/* Storage backend that uses an XMLish format for all data. */

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

struct xml_parsedata
{
	irc_t *irc;
	char *current_setting;
	account_t *current_account;
};

static char *xml_attr( const gchar **attr_names, const gchar **attr_values, const gchar *key )
{
	int i;
	
	for( i = 0; attr_names[i]; i ++ )
		if( g_strcasecmp( attr_names[i], key ) == 0 )
			return attr_values[i];
	
	return NULL;
}

static void xml_start_element( GMarkupParseContext *ctx, const gchar *element_name, const gchar **attr_names, const gchar **attr_values, gpointer data, GError **error )
{
	struct xml_parsedata *xd = data;
	irc_t *irc = data->irc;
	
	if( g_strcasecmp( element_name, "user" ) == 0 )
	{
		char *nick = xml_attr( attr_names, attr_values, "nick" );
		
		if( nick && g_strcasecmp( nick, irc->nick ) == 0 )
		{
			/* Okay! */
		}
	}
	else if( g_strcasecmp( element_name, "account" ) == 0 )
	{
		char *protocol, *handle, *password;
		struct prpl *prpl = NULL;
		
		handle = xml_attr( attr_names, attr_values, "handle" );
		password = xml_attr( attr_names, attr_values, "password" );
		
		protocol = xml_attr( attr_names, attr_values, "protocol" );
		if( protocol )
			prpl = find_protocol( protocol );
		
		if( handle && password && prpl )
		{
			xd->current_account = account_add( irc, prpl, handle, password )
		}
	}
	else if( g_strcasecmp( element_name, "setting" ) == 0 )
	{
		if( xd->current_account == NULL )
		{
			current_setting = xml_attr( attr_names, attr_values, "name" );
		}
	}
	else if( g_strcasecmp( element_name, "buddy" ) == 0 )
	{
	}
	else if( g_strcasecmp( element_name, "password" ) == 0 )
	{
	}
	else
	{
		/* Return "unknown element" error. */
	}
}

static void xml_end_element( GMarkupParseContext *ctx, const gchar *element_name, gpointer data, GError **error )
{
}

static void xml_text( GMarkupParseContext *ctx, const gchar *text, gsize text_len, gpointer data, GError **error )
{
	struct xml_parsedata *xd = data;
	irc_t *irc = data->irc;
	
	if( xd->current_setting )
	{
		set_setstr( irc, xd->current_setting, text );
	}
}

static void xml_error( GMarkupParseContext *ctx, GError *error, gpointer data )
{
}

GMarkupParser xml_parser =
{
	xml_start_element,
	xml_end_element,
	xml_text,
	NULL,
	xml_error
};

static void xml_init( void )
{
	if( access( global.conf->configdir, F_OK ) != 0 )
		log_message( LOGLVL_WARNING, "The configuration directory %s does not exist. Configuration won't be saved.", CONFIG );
	else if( access( global.conf->configdir, R_OK ) != 0 || access( global.conf->configdir, W_OK ) != 0 )
		log_message( LOGLVL_WARNING, "Permission problem: Can't read/write from/to %s.", global.conf->configdir );
}

static storage_status_t xml_load ( const char *my_nick, const char* password, irc_t *irc )
{
	GMarkupParseContext *ctx;
	
	ctx = g_markup_parse_context_new( parser, 0, xd, NULL );
	if( irc->status >= USTATUS_IDENTIFIED )
		return( 1 );
	
	g_snprintf( s, 511, "%s%s%s", global.conf->configdir, my_nick, ".accounts" );
   	fp = fopen( s, "r" );
   	if( !fp ) return STORAGE_NO_SUCH_USER;
	
	fscanf( fp, "%32[^\n]s", s );

	if (checkpass (password, s) != 0) 
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
		line = deobfucrypt( s, password );
		if (line == NULL) return STORAGE_OTHER_ERROR;
		root_command_string( irc, ru, line, 0 );
		g_free( line );
	}
	fclose( fp );
	
	g_snprintf( s, 511, "%s%s%s", global.conf->configdir, my_nick, ".nicks" );
	fp = fopen( s, "r" );
	if( !fp ) return STORAGE_NO_SUCH_USER;
	while( fscanf( fp, "%s %d %s", s, &proto, nick ) > 0 )
	{
		struct prpl *prpl;

		prpl = find_protocol_by_id(proto);

		if (!prpl)
			continue;

		http_decode( s );
		nick_set( irc, s, prpl, nick );
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
	
	hash = hashpass( irc->password );
	if( hash == NULL )
	{
		irc_usermsg( irc, "Please register yourself if you want to save your settings." );
		return STORAGE_OTHER_ERROR;
	}
	
	g_snprintf( path, 511, "%s%s%s", global.conf->configdir, irc->nick, ".nicks~" );
	fp = fopen( path, "w" );
	if( !fp ) return STORAGE_OTHER_ERROR;
	for( n = irc->nicks; n; n = n->next )
	{
		strcpy( s, n->handle );
		s[169] = 0; /* Prevent any overflow (169 ~ 512 / 3) */
		http_encode( s );
		g_snprintf( s + strlen( s ), 510 - strlen( s ), " %d %s", find_protocol_id(n->proto->name), n->nick );
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
		if( !strcmp(a->prpl->name, "oscar") )
			g_snprintf( s, sizeof( s ), "account add oscar \"%s\" \"%s\" %s", a->user, a->pass, a->server );
		else
			g_snprintf( s, sizeof( s ), "account add %s \"%s\" \"%s\" \"%s\"",
			            a->prpl->name, a->user, a->pass, a->server ? a->server : "" );
		
		line = obfucrypt( s, irc->password );
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
			line = obfucrypt( s, irc->password );
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
		line = obfucrypt( s, irc->password );
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

storage_t storage_xml = {
	.name = "xml",
	.init = xml_init,
	.check_pass = xml_check_pass,
	.remove = xml_remove,
	.load = xml_load,
	.save = xml_save
};
