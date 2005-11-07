  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2004 Wilmer van der Gaast and others                *
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

void nick_set( irc_t *irc, char *handle, struct prpl *proto, char *nick )
{
	nick_t *m = NULL, *n = irc->nicks;
	
	while( n )
	{
		if( ( g_strcasecmp( n->handle, handle ) == 0 ) && n->proto == proto )
		{
			g_free( n->nick );
			n->nick = nick_dup( nick );
			nick_strip( n->nick );
			
			return;
		}
		n = ( m = n )->next;	// :-P
	}
	
	if( m )
		n = m->next = g_new0( nick_t, 1 );
	else
		n = irc->nicks = g_new0( nick_t, 1 );
	
	n->handle = g_strdup( handle );
	n->proto = proto;
	n->nick = nick_dup( nick );
	
	nick_strip( n->nick );
}

char *nick_get( irc_t *irc, char *handle, struct prpl *proto, const char *realname )
{
	static char nick[MAX_NICK_LENGTH+1];
	nick_t *n = irc->nicks;
	int inf_protection = 256;
	
	memset( nick, 0, MAX_NICK_LENGTH + 1 );
	
	while( n && !*nick )
		if( ( n->proto == proto ) && ( g_strcasecmp( n->handle, handle ) == 0 ) )
			strcpy( nick, n->nick );
		else
			n = n->next;
	
	if( !n )
	{
		char *s;
		
		g_snprintf( nick, MAX_NICK_LENGTH, "%s", handle );
		if( ( s = strchr( nick, '@' ) ) )
			while( *s )
				*(s++) = 0;
		
		/* All-digit handles (mainly ICQ UINs) aren't cool, try to
		   use the realname instead. */
		for( s = nick; *s && isdigit( *s ); s ++ );
		if( !*s && realname && *realname )
			g_snprintf( nick, MAX_NICK_LENGTH, "%s", realname );
		
		nick_strip( nick );
		if (set_getint(irc, "lcnicks")) 
			nick_lc( nick );
	}
	
	while( !nick_ok( nick ) || user_find( irc, nick ) )
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
			
			irc_usermsg( irc, "WARNING: Almost had an infinite loop in nick_get()! "
			                  "This used to be a fatal BitlBee bug, but we tried to fix it. "
			                  "This message should *never* appear anymore. "
			                  "If it does, please *do* send us a bug report! "
			                  "Please send all the following lines in your report:" );
			
			irc_usermsg( irc, "Trying to get a sane nick for handle %s", handle );
			for( i = 0; i < MAX_NICK_LENGTH; i ++ )
				irc_usermsg( irc, "Char %d: %c/%d", i, nick[i], nick[i] );
			
			irc_usermsg( irc, "FAILED. Returning an insane nick now. Things might break. "
			                  "Good luck, and please don't forget to paste the lines up here "
			                  "in #bitlbee on OFTC or in a mail to wilmer@gaast.net" );
			
			g_snprintf( nick, MAX_NICK_LENGTH + 1, "xx%x", rand() );
			
			break;
		}
	}
	
	return( nick );
}

void nick_del( irc_t *irc, char *nick )
{
	nick_t *l = NULL, *n = irc->nicks;
	
	while( n )
	{
		if( g_strcasecmp( n->nick, nick ) == 0 )
		{
			if( l )
				l->next = n->next;
			else
				irc->nicks = n->next;
			
			g_free( n->handle );
			g_free( n->nick );
			g_free( n );
			
			break;
		}
		n = (l=n)->next;
	}
}


/* Character maps, _lc_[x] == _uc_[x] (but uppercase), according to the RFC's.
   With one difference, we allow dashes. */

static char *nick_lc_chars = "0123456789abcdefghijklmnopqrstuvwxyz{}^-_|";
static char *nick_uc_chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ[]~-_\\";

void nick_strip( char * nick )
{
	int i, j;
	
	for( i = j = 0; nick[i] && i < MAX_NICK_LENGTH; i++ )
	{
		if( strchr( nick_lc_chars, nick[i] ) || 
		    strchr( nick_uc_chars, nick[i] ) )
		{
			nick[j] = nick[i];
			j++;
		}
	}
	while( j < MAX_NICK_LENGTH )
		nick[j++] = '\0';
}

int nick_ok( char *nick )
{
	char *s;
	
	/* Empty/long nicks are not allowed */
	if( !*nick || strlen( nick ) > MAX_NICK_LENGTH )
		return( 0 );
	
	for( s = nick; *s; s ++ )
		if( !strchr( nick_lc_chars, *s ) && !strchr( nick_uc_chars, *s ) )
			return( 0 );
	
	return( 1 );
}

int nick_lc( char *nick )
{
	static char tab[256] = { 0 };
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

int nick_cmp( char *a, char *b )
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

char *nick_dup( char *nick )
{
	char *cp;
	
	cp = g_new0 ( char, MAX_NICK_LENGTH + 1 );
	strncpy( cp, nick, MAX_NICK_LENGTH );
	
	return( cp );
}
