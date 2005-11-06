  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2004 Wilmer van der Gaast and others                *
  \********************************************************************/

/* Some stuff to register, handle and save user preferences             */

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

set_t *set_add( irc_t *irc, char *key, char *def, void *eval )
{
	set_t *s = set_find( irc, key );
	
	if( !s )
	{
		if( ( s = irc->set ) )
		{
			while( s->next ) s = s->next;
			s->next = g_new ( set_t, 1 );
			s = s->next;
		}
		else
		{
			s = irc->set = g_new( set_t, 1 );
		}
		memset( s, 0, sizeof( set_t ) );
		s->key = g_strdup( key );
	}
	
	if( s->def )
	{
		g_free( s->def );
		s->def = NULL;
	}
	if( def ) s->def = g_strdup( def );
	
	if( s->eval )
	{
		g_free( s->eval );
		s->eval = NULL;
	}
	if( eval ) s->eval = eval;
	
	return( s );
}

set_t *set_find( irc_t *irc, char *key )
{
	set_t *s = irc->set;
	
	while( s )
	{
		if( g_strcasecmp( s->key, key ) == 0 )
			break;
		s = s->next;
	}
	
	return( s );
}

char *set_getstr( irc_t *irc, char *key )
{
	set_t *s = set_find( irc, key );
	
	if( !s || ( !s->value && !s->def ) )
		return( NULL );
	
	return( s->value?s->value:s->def );
}

int set_getint( irc_t *irc, char *key )
{
	char *s = set_getstr( irc, key );
	int i = 0;
	
	if( !s )
		return( 0 );
	
	if( ( g_strcasecmp( s, "true" ) == 0 ) || ( g_strcasecmp( s, "yes" ) == 0 ) || ( g_strcasecmp( s, "on" ) == 0 ) )
		return( 1 );
	
	if( sscanf( s, "%d", &i ) != 1 )
		return( 0 );
	
	return( i );
}

int set_setstr( irc_t *irc, char *key, char *value )
{
	set_t *s = set_find( irc, key );
	char *nv = value;
	
	if( !s )
		s = set_add( irc, key, NULL, NULL );
	
	if( s->eval && !( nv = s->eval( irc, s, value ) ) )
		return( 0 );
	
	if( s->value )
	{
		g_free( s->value );
		s->value = NULL;
	}
	
	if( !s->def || ( strcmp( nv, s->def ) != 0 ) )
		s->value = g_strdup( nv );
	
	if( nv != value )
		g_free( nv );
	
	return( 1 );
}

int set_setint( irc_t *irc, char *key, int value )
{
	char s[24];	/* Not quite 128-bit clean eh? ;-) */
	
	sprintf( s, "%d", value );
	return( set_setstr( irc, key, s ) );
}

void set_del( irc_t *irc, char *key )
{
	set_t *s = irc->set, *t = NULL;
	
	while( s )
	{
		if( g_strcasecmp( s->key, key ) == 0 )
			break;
		s = (t=s)->next;
	}
	if( s )
	{
		t->next = s->next;
		g_free( s->key );
		if( s->value ) g_free( s->value );
		if( s->def ) g_free( s->def );
		g_free( s );
	}
}

char *set_eval_int( irc_t *irc, set_t *set, char *value )
{
	char *s = value;
	
	for( ; *s; s ++ )
		if( *s < '0' || *s > '9' )
			return( NULL );
	
	return( value );
}

char *set_eval_bool( irc_t *irc, set_t *set, char *value )
{
	if( ( g_strcasecmp( value, "true" ) == 0 ) || ( g_strcasecmp( value, "yes" ) == 0 ) || ( g_strcasecmp( value, "on" ) == 0 ) )
		return( value );
	if( ( g_strcasecmp( value, "false" ) == 0 ) || ( g_strcasecmp( value, "no" ) == 0 ) || ( g_strcasecmp( value, "off" ) == 0 ) )
		return( value );
	return( set_eval_int( irc, set, value ) );
}

char *set_eval_to_char( irc_t *irc, set_t *set, char *value )
{
	char *s = g_new( char, 3 );
	
	if( *value == ' ' )
		strcpy( s, " " );
	else
		sprintf( s, "%c ", *value );
	
	return( s );
}

char *set_eval_ops( irc_t *irc, set_t *set, char *value )
{
	if( g_strcasecmp( value, "user" ) == 0 )
	{
		irc_write( irc, ":%s!%s@%s MODE %s %s %s %s", irc->mynick, irc->mynick, irc->myhost,
		                                              irc->channel, "+o-o", irc->nick, irc->mynick );
		return( value );
	}
	else if( g_strcasecmp( value, "root" ) == 0 )
	{
		irc_write( irc, ":%s!%s@%s MODE %s %s %s %s", irc->mynick, irc->mynick, irc->myhost,
		                                              irc->channel, "-o+o", irc->nick, irc->mynick );
		return( value );
	}
	else if( g_strcasecmp( value, "both" ) == 0 )
	{
		irc_write( irc, ":%s!%s@%s MODE %s %s %s %s", irc->mynick, irc->mynick, irc->myhost,
		                                              irc->channel, "+oo", irc->nick, irc->mynick );
		return( value );
	}
	else if( g_strcasecmp( value, "none" ) == 0 )
	{
		irc_write( irc, ":%s!%s@%s MODE %s %s %s %s", irc->mynick, irc->mynick, irc->myhost,
		                                              irc->channel, "-oo", irc->nick, irc->mynick );
		return( value );
	}
	
	return( NULL );
}

