  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2010 Wilmer van der Gaast and others                *
  \********************************************************************/

/* The IRC-based UI - Representing (virtual) channels.                  */

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

#include "bitlbee.h"

static gint irc_channel_user_cmp( gconstpointer a_, gconstpointer b_ );
static const struct irc_channel_funcs control_channel_funcs;

irc_channel_t *irc_channel_new( irc_t *irc, const char *name )
{
	irc_channel_t *ic;
	
	if( strchr( CTYPES, name[0] ) == NULL || !nick_ok( name + 1 ) )
		return NULL;
	
	ic = g_new0( irc_channel_t, 1 );
	ic->f = &control_channel_funcs;
	ic->irc = irc;
	ic->name = g_strdup( name );
	strcpy( ic->mode, CMODE );
	
	irc_channel_add_user( ic, irc->root );
	if( strcmp( set_getstr( &irc->b->set, "ops" ), "both" ) == 0 ||
	    strcmp( set_getstr( &irc->b->set, "ops" ), "root" ) == 0 )
		irc_channel_user_set_mode( ic, irc->root, IRC_CHANNEL_USER_OP );
	
	irc->channels = g_slist_prepend( irc->channels, ic );
	
	return ic;
}

irc_channel_t *irc_channel_by_name( irc_t *irc, const char *name )
{
	GSList *l;
	
	for( l = irc->channels; l; l = l->next )
	{
		irc_channel_t *ic = l->data;
		
		if( name[0] == ic->name[0] && nick_cmp( name + 1, ic->name + 1 ) == 0 )
			return ic;
	}
	
	return NULL;
}

int irc_channel_free( irc_channel_t *ic )
{
	irc_t *irc = ic->irc;
	
	if( ic->flags & IRC_CHANNEL_JOINED )
		irc_channel_del_user( ic, irc->user );
	
	irc->channels = g_slist_remove( irc->channels, ic );
	while( ic->users )
	{
		g_free( ic->users->data );
		ic->users = g_slist_remove( ic->users, ic->users->data );
	}
	
	g_free( ic->name );
	g_free( ic->topic );
	g_free( ic );
	
	return 1;
}

int irc_channel_add_user( irc_channel_t *ic, irc_user_t *iu )
{
	irc_channel_user_t *icu;
	
	if( irc_channel_has_user( ic, iu ) )
		return 0;
	
	icu = g_new0( irc_channel_user_t, 1 );
	icu->iu = iu;
	
	ic->users = g_slist_insert_sorted( ic->users, icu, irc_channel_user_cmp );
	
	if( iu == ic->irc->user || ic->flags & IRC_CHANNEL_JOINED )
	{
		ic->flags |= IRC_CHANNEL_JOINED;
		irc_send_join( ic, iu );
	}
	
	return 1;
}

int irc_channel_del_user( irc_channel_t *ic, irc_user_t *iu )
{
	irc_channel_user_t *icu;
	
	if( !( icu = irc_channel_has_user( ic, iu ) ) )
		return 0;
	
	ic->users = g_slist_remove( ic->users, icu );
	g_free( icu );
	
	if( ic->flags & IRC_CHANNEL_JOINED )
		irc_send_part( ic, iu, "" );
	
	if( iu == ic->irc->user )
		ic->flags &= ~IRC_CHANNEL_JOINED;
	
	return 1;
}

irc_channel_user_t *irc_channel_has_user( irc_channel_t *ic, irc_user_t *iu )
{
	GSList *l;
	
	for( l = ic->users; l; l = l->next )
	{
		irc_channel_user_t *icu = l->data;
		
		if( icu->iu == iu )
			return icu;
	}
	
	return NULL;
}

int irc_channel_set_topic( irc_channel_t *ic, const char *topic, const irc_user_t *iu )
{
	g_free( ic->topic );
	ic->topic = g_strdup( topic );
	
	g_free( ic->topic_who );
	if( iu )
		ic->topic_who = g_strdup_printf( "%s!%s@%s", iu->nick, iu->user, iu->host );
	else
		ic->topic_who = NULL;
	
	ic->topic_time = time( NULL );
	
	if( ic->flags & IRC_CHANNEL_JOINED )
		irc_send_topic( ic, TRUE );
	
	return 1;
}

void irc_channel_user_set_mode( irc_channel_t *ic, irc_user_t *iu, irc_channel_user_flags_t flags )
{
	irc_channel_user_t *icu = irc_channel_has_user( ic, iu );
	
	if( icu->flags == flags )
		return;
	
	if( ic->flags & IRC_CHANNEL_JOINED )
		irc_send_channel_user_mode_diff( ic, iu, icu->flags, flags );
	
	icu->flags = flags;
}

gboolean irc_channel_name_ok( const char *name )
{
	return strchr( CTYPES, name[0] ) != NULL && nick_ok( name + 1 );
}

static gint irc_channel_user_cmp( gconstpointer a_, gconstpointer b_ )
{
	const irc_channel_user_t *a = a_, *b = b_;
	
	return irc_user_cmp( a->iu, b->iu );
}

/* Channel-type dependent functions, for control channels: */
static gboolean control_channel_privmsg( irc_channel_t *ic, const char *msg )
{
	irc_t *irc = ic->irc;
	const char *s;
	
	/* Scan for non-whitespace chars followed by a colon: */
	for( s = msg; *s && !isspace( *s ) && *s != ':'; s ++ ) {}
	
	if( *s == ':' )
	{
		char to[s-msg+1];
		irc_user_t *iu;
		
		strncpy( to, msg, s - msg );
		while( *(++s) && isspace( *s ) ) {}
		
		iu = irc_user_by_name( irc, to );
		if( iu && iu->f->privmsg )
		{
			iu->flags &= ~IRC_USER_PRIVATE;
			iu->f->privmsg( iu, s );
		}
	}
	else
	{
		/* TODO: Maybe just use root->privmsg here now? */
		char cmd[strlen(msg)+1];
		
		g_free( ic->irc->last_root_cmd );
		ic->irc->last_root_cmd = g_strdup( ic->name );
		
		strcpy( cmd, msg );
		root_command_string( ic->irc, cmd );
	}
	
	return TRUE;
}

static const struct irc_channel_funcs control_channel_funcs = {
	control_channel_privmsg,
};
