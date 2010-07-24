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

/* Used to use NULL for this, but NULL is actually a "valid" value. */
char *SET_INVALID = "nee";

set_t *set_add( set_t **head, const char *key, const char *def, set_eval eval, void *data )
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

set_t *set_find( set_t **head, const char *key )
{
	set_t *s = *head;
	
	while( s )
	{
		if( g_strcasecmp( s->key, key ) == 0 ||
		    ( s->old_key && g_strcasecmp( s->old_key, key ) == 0 ) )
			break;
		s = s->next;
	}
	
	return s;
}

char *set_getstr( set_t **head, const char *key )
{
	set_t *s = set_find( head, key );
	
	if( !s || ( !s->value && !s->def ) )
		return NULL;
	
	return s->value ? s->value : s->def;
}

int set_getint( set_t **head, const char *key )
{
	char *s = set_getstr( head, key );
	int i = 0;
	
	if( !s )
		return 0;
	
	if( sscanf( s, "%d", &i ) != 1 )
		return 0;
	
	return i;
}

int set_getbool( set_t **head, const char *key )
{
	char *s = set_getstr( head, key );
	
	if( !s )
		return 0;
	
	return bool2int( s );
}

int set_setstr( set_t **head, const char *key, char *value )
{
	set_t *s = set_find( head, key );
	char *nv = value;
	
	if( !s )
		/*
		Used to do this, but it never really made sense.
		s = set_add( head, key, NULL, NULL, NULL );
		*/
		return 0;
	
	if( value == NULL && ( s->flags & SET_NULL_OK ) == 0 )
		return 0;
	
	/* Call the evaluator. For invalid values, evaluators should now
	   return SET_INVALID, but previously this was NULL. Try to handle
	   that too if NULL is not an allowed value for this setting. */
	if( s->eval && ( ( nv = s->eval( s, value ) ) == SET_INVALID ||
	                 ( ( s->flags & SET_NULL_OK ) == 0 && nv == NULL ) ) )
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

int set_setint( set_t **head, const char *key, int value )
{
	char s[24];	/* Not quite 128-bit clean eh? ;-) */
	
	g_snprintf( s, sizeof( s ), "%d", value );
	return set_setstr( head, key, s );
}

void set_del( set_t **head, const char *key )
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
		g_free( s->old_key );
		g_free( s->value );
		g_free( s->def );
		g_free( s );
	}
}

int set_reset( set_t **head, const char *key )
{
	set_t *s;
	
	s = set_find( head, key );
	if( s )
		return set_setstr( head, key, s->def );
	
	return 0;
}

char *set_eval_int( set_t *set, char *value )
{
	char *s = value;
	
	/* Allow a minus at the first position. */
	if( *s == '-' )
		s ++;
	
	for( ; *s; s ++ )
		if( !isdigit( *s ) )
			return SET_INVALID;
	
	return value;
}

char *set_eval_bool( set_t *set, char *value )
{
	return is_bool( value ) ? value : SET_INVALID;
}

char *set_eval_list( set_t *set, char *value )
{
	GSList *options = set->eval_data, *opt;
	
	for( opt = options; opt; opt = opt->next )
		if( strcmp( value, opt->data ) == 0 )
			return value;
	
	/* TODO: It'd be nice to show the user a list of allowed values,
	         but we don't have enough context here to do that. May
	         want to fix that. */
	
	return NULL;
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

/*
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
		return SET_INVALID;
	
	return value;
}
*/
