  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2005 Wilmer van der Gaast and others                *
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

set_t *set_add( set_t **head, char *key, char *def, set_eval eval, void *data )
{
	set_t *s = set_find( head, key );
	
	/* Possibly the setting already exists. If it doesn't exist yet,
	   we create it. If it does, we'll just change the default. */
	if( !s )
	{
		if( ( s = *head ) )
		{
			while( s->next ) s = s->next;
			s->next = g_new0( set_t, 1 );
			s = s->next;
		}
		else
		{
			s = *head = g_new0( set_t, 1 );
		}
		s->key = g_strdup( key );
	}
	
	if( s->def )
	{
		g_free( s->def );
		s->def = NULL;
	}
	if( def ) s->def = g_strdup( def );
	
	s->eval = eval;
	s->data = data;
	
	return s;
}

set_t *set_find( set_t **head, char *key )
{
	set_t *s = *head;
	
	while( s )
	{
		if( g_strcasecmp( s->key, key ) == 0 )
			break;
		s = s->next;
	}
	
	return s;
}

char *set_getstr( set_t **head, char *key )
{
	set_t *s = set_find( head, key );
	
	if( !s || ( !s->value && !s->def ) )
		return NULL;
	
	return s->value ? s->value : s->def;
}

int set_getint( set_t **head, char *key )
{
	char *s = set_getstr( head, key );
	int i = 0;
	
	if( !s )
		return 0;
	
	if( ( g_strcasecmp( s, "true" ) == 0 ) || ( g_strcasecmp( s, "yes" ) == 0 ) || ( g_strcasecmp( s, "on" ) == 0 ) )
		return 1;
	
	if( sscanf( s, "%d", &i ) != 1 )
		return 0;
	
	return i;
}

int set_getbool( set_t **head, char *key )
{
	char *s = set_getstr( head, key );
	
	if( !s )
		return 0;
	
	return bool2int( s );
}

int set_setstr( set_t **head, char *key, char *value )
{
	set_t *s = set_find( head, key );
	char *nv = value;
	
	if( !s )
		s = set_add( head, key, NULL, NULL, NULL );
	
	if( s->eval && !( nv = s->eval( s, value ) ) )
		return 0;
	
	if( s->value )
	{
		g_free( s->value );
		s->value = NULL;
	}
	
	/* If there's a default setting and it's equal to what we're trying to
	   set, stick with s->value = NULL. Otherwise, remember the setting. */
	if( !s->def || ( strcmp( nv, s->def ) != 0 ) )
		s->value = g_strdup( nv );
	
	if( nv != value )
		g_free( nv );
	
	return 1;
}

int set_setint( set_t **head, char *key, int value )
{
	char s[24];	/* Not quite 128-bit clean eh? ;-) */
	
	g_snprintf( s, sizeof( s ), "%d", value );
	return set_setstr( head, key, s );
}

void set_del( set_t **head, char *key )
{
	set_t *s = *head, *t = NULL;
	
	while( s )
	{
		if( g_strcasecmp( s->key, key ) == 0 )
			break;
		s = (t=s)->next;
	}
	if( s )
	{
		if( t )
			t->next = s->next;
		else
			*head = s->next;
		
		g_free( s->key );
		if( s->value ) g_free( s->value );
		if( s->def ) g_free( s->def );
		g_free( s );
	}
}

char *set_eval_int( set_t *set, char *value )
{
	char *s = value;
	
	/* Allow a minus at the first position. */
	if( *s == '-' )
		s ++;
	
	for( ; *s; s ++ )
		if( !isdigit( *s ) )
			return NULL;
	
	return value;
}

char *set_eval_bool( set_t *set, char *value )
{
	return is_bool( value ) ? value : NULL;
}

char *set_eval_to_char( set_t *set, char *value )
{
	char *s = g_new( char, 3 );
	
	if( *value == ' ' )
		strcpy( s, " " );
	else
		sprintf( s, "%c ", *value );
	
	return s;
}

char *set_eval_ops( set_t *set, char *value )
{
	irc_t *irc = set->data;
	
	if( g_strcasecmp( value, "user" ) == 0 )
		irc_write( irc, ":%s!%s@%s MODE %s %s %s %s", irc->mynick, irc->mynick, irc->myhost,
		                                              irc->channel, "+o-o", irc->nick, irc->mynick );
	else if( g_strcasecmp( value, "root" ) == 0 )
		irc_write( irc, ":%s!%s@%s MODE %s %s %s %s", irc->mynick, irc->mynick, irc->myhost,
		                                              irc->channel, "-o+o", irc->nick, irc->mynick );
	else if( g_strcasecmp( value, "both" ) == 0 )
		irc_write( irc, ":%s!%s@%s MODE %s %s %s %s", irc->mynick, irc->mynick, irc->myhost,
		                                              irc->channel, "+oo", irc->nick, irc->mynick );
	else if( g_strcasecmp( value, "none" ) == 0 )
		irc_write( irc, ":%s!%s@%s MODE %s %s %s %s", irc->mynick, irc->mynick, irc->myhost,
		                                              irc->channel, "-oo", irc->nick, irc->mynick );
	else
		return NULL;
	
	return value;
}

char *set_eval_charset( set_t *set, char *value )
{
	GIConv cd;

	if ( g_strncasecmp( value, "none", 4 ) == 0 )
		return value;

	cd = g_iconv_open( "UTF-8", value );
	if( cd == (GIConv) -1 )
		return NULL;

	g_iconv_close( cd );
	return value;
}
