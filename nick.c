  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2010 Wilmer van der Gaast and others                *
  \********************************************************************/

/* Some stuff to fetch, save and handle nicknames for your buddies      */

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

/* Character maps, _lc_[x] == _uc_[x] (but uppercase), according to the RFC's.
   With one difference, we allow dashes. These are used to do uc/lc conversions
   and strip invalid chars. */
static char *nick_lc_chars = "0123456789abcdefghijklmnopqrstuvwxyz{}^`-_|";
static char *nick_uc_chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ[]~`-_\\";

/* Store handles in lower case and strip spaces, because AIM is braindead. */
static char *clean_handle( const char *orig )
{
	char *new = g_malloc( strlen( orig ) + 1 );
	int i = 0;
	
	do {
		if (*orig != ' ')
			new[i++] = tolower( *orig );
	}
	while (*(orig++));
	
	return new;
}

void nick_set_raw( account_t *acc, const char *handle, const char *nick )
{
	char *store_handle, *store_nick = g_malloc( MAX_NICK_LENGTH + 1 );
	
	store_handle = clean_handle( handle );
	store_nick[MAX_NICK_LENGTH] = '\0';
	strncpy( store_nick, nick, MAX_NICK_LENGTH );
	nick_strip( store_nick );
	
	g_hash_table_replace( acc->nicks, store_handle, store_nick );
}

void nick_set( bee_user_t *bu, const char *nick )
{
	nick_set_raw( bu->ic->acc, bu->handle, nick );
}

char *nick_get( bee_user_t *bu )
{
	static char nick[MAX_NICK_LENGTH+1];
	char *store_handle, *found_nick;
	
	memset( nick, 0, MAX_NICK_LENGTH + 1 );
	
	store_handle = clean_handle( bu->handle );
	/* Find out if we stored a nick for this person already. If not, try
	   to generate a sane nick automatically. */
	if( ( found_nick = g_hash_table_lookup( bu->ic->acc->nicks, store_handle ) ) )
	{
		strncpy( nick, found_nick, MAX_NICK_LENGTH );
	}
	else if( ( found_nick = nick_gen( bu ) ) )
	{
		strncpy( nick, found_nick, MAX_NICK_LENGTH );
		g_free( found_nick );
	}
	else
	{
		/* Keep this fallback since nick_gen() can return NULL in some cases. */
		char *s;
		
		g_snprintf( nick, MAX_NICK_LENGTH, "%s", bu->handle );
		if( ( s = strchr( nick, '@' ) ) )
			while( *s )
				*(s++) = 0;
		
		nick_strip( nick );
		if( set_getbool( &bu->bee->set, "lcnicks" ) )
			nick_lc( nick );
	}
	g_free( store_handle );
	
	/* Make sure the nick doesn't collide with an existing one by adding
	   underscores and that kind of stuff, if necessary. */
	nick_dedupe( bu, nick );
	
	return nick;
}

char *nick_gen( bee_user_t *bu )
{
	gboolean ok = FALSE; /* Set to true once the nick contains something unique. */
	GString *ret = g_string_new( "" );
	char *fmt = set_getstr( &bu->ic->acc->set, "nick_format" ) ? :
	            set_getstr( &bu->bee->set, "nick_format" );
	
	while( fmt && *fmt && ret->len < MAX_NICK_LENGTH )
	{
		char *part = NULL, chop = '\0', *asc = NULL;
		int len = MAX_NICK_LENGTH;
		
		if( *fmt != '%' )
		{
			g_string_append_c( ret, *fmt );
			fmt ++;
			continue;
		}
		
		fmt ++;
		while( *fmt )
		{
			/* -char means chop off everything from char */
			if( *fmt == '-' )
			{
				chop = fmt[1];
				if( chop == '\0' )
				{
					g_string_free( ret, TRUE );
					return NULL;
				}
				fmt += 2;
			}
			else if( isdigit( *fmt ) )
			{
				len = 0;
				/* Grab a number. */
				while( isdigit( *fmt ) )
					len = len * 10 + ( *(fmt++) - '0' );
			}
			else if( g_strncasecmp( fmt, "nick", 4 ) == 0 )
			{
				part = bu->nick ? : bu->handle;
				fmt += 4;
				ok |= TRUE;
				break;
			}
			else if( g_strncasecmp( fmt, "handle", 6 ) == 0 )
			{
				part = bu->handle;
				fmt += 6;
				ok |= TRUE;
				break;
			}
			else if( g_strncasecmp( fmt, "full_name", 9 ) == 0 )
			{
				part = bu->fullname;
				fmt += 9;
				ok |= part && *part;
				break;
			}
			else if( g_strncasecmp( fmt, "first_name", 10 ) == 0 )
			{
				part = bu->fullname;
				fmt += 10;
				ok |= part && *part;
				chop = ' ';
				break;
			}
			else if( g_strncasecmp( fmt, "group", 5 ) == 0 )
			{
				part = bu->group ? bu->group->name : NULL;
				fmt += 5;
				break;
			}
			else if( g_strncasecmp( fmt, "account", 7 ) == 0 )
			{
				part = bu->ic->acc->tag;
				fmt += 7;
				break;
			}
			else
			{
				g_string_free( ret, TRUE );
				return NULL;
			}
		}
		
		/* Credits to Josay_ in #bitlbee for this idea. //TRANSLIT
		   should do lossy/approximate conversions, so letters with
		   accents don't just get stripped. Note that it depends on
		   LC_CTYPE being set to something other than C/POSIX. */
		if( part )
			part = asc = g_convert_with_fallback( part, -1, "ASCII//TRANSLIT",
			                                      "UTF-8", "", NULL, NULL, NULL );
		
		if( ret->len == 0 && part && isdigit( *part ) )
			g_string_append_c( ret, '_' );
		
		while( part && *part && *part != chop && len > 0 )
		{
			if( strchr( nick_lc_chars, *part ) ||
			    strchr( nick_uc_chars, *part ) )
				g_string_append_c( ret, *part );
			
			part ++;
			len --;
		}
		g_free( asc );
	}
	
	/* This returns NULL if the nick is empty or otherwise not ok. */
	return g_string_free( ret, ret->len == 0 || !ok );
}

void nick_dedupe( bee_user_t *bu, char nick[MAX_NICK_LENGTH+1] )
{
	irc_t *irc = (irc_t*) bu->bee->ui_data;
	int inf_protection = 256;
	irc_user_t *iu;
	
	/* Now, find out if the nick is already in use at the moment, and make
	   subtle changes to make it unique. */
	while( !nick_ok( nick ) ||
	       ( ( iu = irc_user_by_name( irc, nick ) ) && iu->bu != bu ) )
	{
		if( strlen( nick ) < ( MAX_NICK_LENGTH - 1 ) )
		{
			nick[strlen(nick)+1] = 0;
			nick[strlen(nick)] = '_';
		}
		else
		{
			nick[0] ++;
		}
		
		if( inf_protection-- == 0 )
		{
			int i;
			
			irc_rootmsg( irc, "Warning: Almost had an infinite loop in nick_get()! "
			                  "This used to be a fatal BitlBee bug, but we tried to fix it. "
			                  "This message should *never* appear anymore. "
			                  "If it does, please *do* send us a bug report! "
			                  "Please send all the following lines in your report:" );
			
			irc_rootmsg( irc, "Trying to get a sane nick for handle %s", bu->handle );
			for( i = 0; i < MAX_NICK_LENGTH; i ++ )
				irc_rootmsg( irc, "Char %d: %c/%d", i, nick[i], nick[i] );
			
			irc_rootmsg( irc, "FAILED. Returning an insane nick now. Things might break. "
			                  "Good luck, and please don't forget to paste the lines up here "
			                  "in #bitlbee on OFTC or in a mail to wilmer@gaast.net" );
			
			g_snprintf( nick, MAX_NICK_LENGTH + 1, "xx%x", rand() );
			
			break;
		}
	}
}

/* Just check if there is a nickname set for this buddy or if we'd have to
   generate one. */
int nick_saved( bee_user_t *bu )
{
	char *store_handle, *found;
	
	store_handle = clean_handle( bu->handle );
	found = g_hash_table_lookup( bu->ic->acc->nicks, store_handle );
	g_free( store_handle );
	
	return found != NULL;
}

void nick_del( bee_user_t *bu )
{
	g_hash_table_remove( bu->ic->acc->nicks, bu->handle );
}


void nick_strip( char *nick )
{
	int i, j;
	
	for( i = j = 0; nick[i] && j < MAX_NICK_LENGTH; i++ )
	{
		if( strchr( nick_lc_chars, nick[i] ) || 
		    strchr( nick_uc_chars, nick[i] ) )
		{
			nick[j] = nick[i];
			j++;
		}
	}
	if( isdigit( nick[0] ) )
	{
		char *orig;
		
		orig = g_strdup( nick );
		g_snprintf( nick, MAX_NICK_LENGTH, "_%s", orig );
		g_free( orig );
		j ++;
	}
	while( j <= MAX_NICK_LENGTH )
		nick[j++] = '\0';
}

int nick_ok( const char *nick )
{
	const char *s;
	
	/* Empty/long nicks are not allowed, nor numbers at [0] */
	if( !*nick || isdigit( nick[0] ) || strlen( nick ) > MAX_NICK_LENGTH )
		return( 0 );
	
	for( s = nick; *s; s ++ )
		if( !strchr( nick_lc_chars, *s ) && !strchr( nick_uc_chars, *s ) )
			return( 0 );
	
	return( 1 );
}

int nick_lc( char *nick )
{
	static char tab[128] = { 0 };
	int i;
	
	if( tab['A'] == 0 )
		for( i = 0; nick_lc_chars[i]; i ++ )
		{
			tab[(int)nick_uc_chars[i]] = nick_lc_chars[i];
			tab[(int)nick_lc_chars[i]] = nick_lc_chars[i];
		}
	
	for( i = 0; nick[i]; i ++ )
	{
		if( !tab[(int)nick[i]] )
			return( 0 );
		
		nick[i] = tab[(int)nick[i]];
	}
	
	return( 1 );
}

int nick_uc( char *nick )
{
	static char tab[128] = { 0 };
	int i;
	
	if( tab['A'] == 0 )
		for( i = 0; nick_lc_chars[i]; i ++ )
		{
			tab[(int)nick_uc_chars[i]] = nick_uc_chars[i];
			tab[(int)nick_lc_chars[i]] = nick_uc_chars[i];
		}
	
	for( i = 0; nick[i]; i ++ )
	{
		if( !tab[(int)nick[i]] )
			return( 0 );
		
		nick[i] = tab[(int)nick[i]];
	}
	
	return( 1 );
}

int nick_cmp( const char *a, const char *b )
{
	char aa[1024] = "", bb[1024] = "";
	
	strncpy( aa, a, sizeof( aa ) - 1 );
	strncpy( bb, b, sizeof( bb ) - 1 );
	if( nick_lc( aa ) && nick_lc( bb ) )
	{
		return( strcmp( aa, bb ) );
	}
	else
	{
		return( -1 );	/* Hmm... Not a clear answer.. :-/ */
	}
}

char *nick_dup( const char *nick )
{
	return g_strndup( nick, MAX_NICK_LENGTH );
}
