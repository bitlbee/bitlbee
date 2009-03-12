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

int set_reset( set_t **head, char *key )
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

char *set_eval_to_char( set_t *set, char *value )
{
	char *s = g_new( char, 3 );
	
	if( *value == ' ' )
		strcpy( s, " " );
	else
		sprintf( s, "%c ", *value );
	
	return s;
}

char* set_eval_op_root( set_t *set, char* value )
{
	irc_t *irc = set->data;
	char* ret = set_eval_bool(set, value);
	int b = bool2int(ret);

	irc_write( irc, ":%s!%s@%s MODE %s %s %s", irc->mynick, irc->mynick, irc->myhost,
                                               irc->channel, b?"+o":"-o", irc->mynick);

	return ret;
}

char* set_eval_op_user( set_t *set, char* value )
{
	irc_t *irc = set->data;
	char* ret = set_eval_bool(set, value);
	int b = bool2int(ret);

	irc_write( irc, ":%s!%s@%s MODE %s %s %s", irc->mynick, irc->mynick, irc->myhost,
                                               irc->channel, b?"+o":"-o", irc->nick);

	return ret;
}

/* generalized version of set_eval_op/voice_buddies */
char *set_eval_mode_buddies( set_t *set, char *value, char modeflag )
{
	irc_t *irc = set->data;
	char op[64], deop[64];
	int nop=0, ndeop=0;
	user_t *u;
	int mode;
	
	if(!strcmp(value, "false"))
		mode=0;
	else if(!strcmp(value, "encrypted"))
		mode=1;
	else if(!strcmp(value, "trusted"))
		mode=2;
	else if(!strcmp(value, "notaway"))
		mode=3;
	else
		return NULL;

	/* sorry for calling them op/deop - too lazy for search+replace :P */
	op[0]='\0';
	deop[0]='\0';
	for(u=irc->users; u; u=u->next) {
		/* we're only concerned with online buddies */
		if(!u->ic || !u->online)
			continue;

		/* just in case... */
		if(strlen(u->nick) >= 64)
			continue;
		
		/* dump out ops/deops when the corresponding name list fills up */
		if(strlen(op)+strlen(u->nick)+2 > 64) {
			char *flags = g_strnfill(nop, modeflag);
			irc_write( irc, ":%s!%s@%s MODE %s +%s%s", irc->mynick, irc->mynick, irc->myhost,
		                                               irc->channel, flags, op );
		    op[0]='\0';
            nop=0;
		    g_free(flags);
		}
		if(strlen(deop)+strlen(u->nick)+2 > 64) {
			char *flags = g_strnfill(ndeop, modeflag);
			irc_write( irc, ":%s!%s@%s MODE %s -%s%s", irc->mynick, irc->mynick, irc->myhost,
		                                               irc->channel, flags, deop );
		    deop[0]='\0';
            ndeop=0;
		    g_free(flags);
		}
		
		switch(mode) {
		/* "false" */
		case 0:
			g_strlcat(deop, " ", 64);
			g_strlcat(deop, u->nick, 64);
			ndeop++;
			break;
		/* "encrypted" */
		case 1:
			if(u->encrypted) {
				g_strlcat(op, " ", 64);
				g_strlcat(op, u->nick, 64);
				nop++;
			} else {
				g_strlcat(deop, " ", 64);
				g_strlcat(deop, u->nick, 64);
				ndeop++;
			}
			break;
		/* "trusted" */
		case 2:
			if(u->encrypted > 1) {
				g_strlcat(op, " ", 64);
				g_strlcat(op, u->nick, 64);
				nop++;
			} else {
				g_strlcat(deop, " ", 64);
				g_strlcat(deop, u->nick, 64);
				ndeop++;
			}
			break;
		/* "notaway" */
		case 3:
			if(u->away) {
				g_strlcat(deop, " ", 64);
				g_strlcat(deop, u->nick, 64);
				ndeop++;
			} else {
				g_strlcat(op, " ", 64);
				g_strlcat(op, u->nick, 64);
				nop++;
			}
		}
	}
	/* dump anything left in op/deop lists */
	if(*op) {
		char *flags = g_strnfill(nop, modeflag);
		irc_write( irc, ":%s!%s@%s MODE %s +%s%s", irc->mynick, irc->mynick, irc->myhost,
		                                               irc->channel, flags, op );
		g_free(flags);
	}
	if(*deop) {
		char *flags = g_strnfill(ndeop, modeflag);
		irc_write( irc, ":%s!%s@%s MODE %s -%s%s", irc->mynick, irc->mynick, irc->myhost,
                                                   irc->channel, flags, deop );
        g_free(flags);
    }

	return value;
}

char *set_eval_op_buddies( set_t *set, char *value )
{
	return set_eval_mode_buddies(set, value, 'o');
}

char *set_eval_halfop_buddies( set_t *set, char *value )
{
	return set_eval_mode_buddies(set, value, 'h');
}

char *set_eval_voice_buddies( set_t *set, char *value )
{
	return set_eval_mode_buddies(set, value, 'v');
}

/* possible values: never, opportunistic, manual, always */
char *set_eval_otr_policy( set_t *set, char *value )
{
	if ( !strcmp(value, "never") )
		return value;
	if ( !strcmp(value, "opportunistic") )
		return value;
	if ( !strcmp(value, "manual") )
		return value;
	if ( !strcmp(value, "always") )
		return value;
	return NULL;
}

