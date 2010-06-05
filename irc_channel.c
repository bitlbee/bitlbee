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

static char *set_eval_channel_type( set_t *set, char *value );
static gint irc_channel_user_cmp( gconstpointer a_, gconstpointer b_ );
static const struct irc_channel_funcs control_channel_funcs;
static const struct irc_channel_funcs groupchat_stub_funcs;

irc_channel_t *irc_channel_new( irc_t *irc, const char *name )
{
	irc_channel_t *ic;
	
	if( !irc_channel_name_ok( name ) || irc_channel_by_name( irc, name ) )
		return NULL;
	
	ic = g_new0( irc_channel_t, 1 );
	ic->irc = irc;
	ic->name = g_strdup( name );
	strcpy( ic->mode, CMODE );
	
	irc_channel_add_user( ic, irc->root );
	if( strcmp( set_getstr( &irc->b->set, "ops" ), "both" ) == 0 ||
	    strcmp( set_getstr( &irc->b->set, "ops" ), "root" ) == 0 )
		irc_channel_user_set_mode( ic, irc->root, IRC_CHANNEL_USER_OP );
	
	irc->channels = g_slist_prepend( irc->channels, ic );
	
	set_add( &ic->set, "type", "control", set_eval_channel_type, ic );
	
	if( name[0] == '&' )
		set_setstr( &ic->set, "type", "control" );
	else /* if( name[0] == '#' ) */
		set_setstr( &ic->set, "type", "chat" );
	
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

static char *set_eval_channel_type( set_t *set, char *value )
{
	struct irc_channel *ic = set->data;
	const struct irc_channel_funcs *new;
	
	if( strcmp( value, "control" ) == 0 )
		new = &control_channel_funcs;
	else if( strcmp( value, "chat" ) == 0 )
		new = &groupchat_stub_funcs;
	else
		return SET_INVALID;
	
	/* TODO: Return values. */
	if( ic->f && ic->f->_free )
		ic->f->_free( ic );
	
	ic->f = new;
	
	if( ic->f && ic->f->_init )
		ic->f->_init( ic );
	
	return value;
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

void irc_channel_printf( irc_channel_t *ic, char *format, ... )
{
	va_list params;
	char *text;
	
	va_start( params, format );
	text = g_strdup_vprintf( format, params );
	va_end( params );
	
	irc_send_msg( ic->irc->root, "PRIVMSG", ic->name, text, NULL );
	g_free( text );
}

gboolean irc_channel_name_ok( const char *name )
{
	char name_[strlen(name)+1];
	
	/* Check if the first character is in CTYPES (#&) */
	if( strchr( CTYPES, name[0] ) == NULL )
		return FALSE;
	
	/* Check the rest of the name. Just checking name + 1 doesn't work
	   since it will fail if the first character is a number, or if
	   it's a one-char channel name - both of which are legal. */
	name_[0] = '_';
	strcpy( name_ + 1, name + 1 );
	return nick_ok( name_ );
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
	for( s = msg; *s && !isspace( *s ) && *s != ':' && *s != ','; s ++ ) {}
	
	if( *s == ':' || *s == ',' )
	{
		char to[s-msg+1];
		irc_user_t *iu;
		
		memset( to, 0, sizeof( to ) );
		strncpy( to, msg, s - msg );
		while( *(++s) && isspace( *s ) ) {}
		
		iu = irc_user_by_name( irc, to );
		if( iu && iu->f->privmsg )
		{
			iu->flags &= ~IRC_USER_PRIVATE;
			iu->f->privmsg( iu, s );
		}
		else
		{
			irc_channel_printf( ic, "User does not exist: %s", to );
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

static char *set_eval_by_account( set_t *set, char *value );
static char *set_eval_fill_by( set_t *set, char *value );
static char *set_eval_by_group( set_t *set, char *value );

static gboolean control_channel_init( irc_channel_t *ic )
{
	struct irc_control_channel *icc;
	
	set_add( &ic->set, "account", NULL, set_eval_by_account, ic );
	set_add( &ic->set, "fill_by", "all", set_eval_fill_by, ic );
	set_add( &ic->set, "group", NULL, set_eval_by_group, ic );
	
	ic->data = icc = g_new0( struct irc_control_channel, 1 );
	icc->type = IRC_CC_TYPE_DEFAULT;
	
	if( bee_group_by_name( ic->irc->b, ic->name + 1, FALSE ) )
	{
		set_setstr( &ic->set, "group", ic->name + 1 );
		set_setstr( &ic->set, "fill_by", "group" );
	}
	else if( set_setstr( &ic->set, "account", ic->name + 1 ) )
	{
		set_setstr( &ic->set, "fill_by", "account" );
	}
	
	return TRUE;
}

static char *set_eval_by_account( set_t *set, char *value )
{
	struct irc_channel *ic = set->data;
	struct irc_control_channel *icc = ic->data;
	account_t *acc;
	
	if( !( acc = account_get( ic->irc->b, value ) ) )
		return SET_INVALID;
	
	icc->account = acc;
	bee_irc_channel_update( ic->irc, ic, NULL );
	return g_strdup_printf( "%s(%s)", acc->prpl->name, acc->user );
}

static char *set_eval_fill_by( set_t *set, char *value )
{
	struct irc_channel *ic = set->data;
	struct irc_control_channel *icc = ic->data;
	
	if( strcmp( value, "all" ) == 0 )
		icc->type = IRC_CC_TYPE_DEFAULT;
	else if( strcmp( value, "rest" ) == 0 )
		icc->type = IRC_CC_TYPE_REST;
	else if( strcmp( value, "group" ) == 0 )
		icc->type = IRC_CC_TYPE_GROUP;
	else if( strcmp( value, "account" ) == 0 )
		icc->type = IRC_CC_TYPE_ACCOUNT;
	else
		return SET_INVALID;
	
	return value;
}

static char *set_eval_by_group( set_t *set, char *value )
{
	struct irc_channel *ic = set->data;
	struct irc_control_channel *icc = ic->data;
	
	icc->group = bee_group_by_name( ic->irc->b, value, TRUE );
	bee_irc_channel_update( ic->irc, ic, NULL );
	return g_strdup( icc->group->name );
}

static gboolean control_channel_free( irc_channel_t *ic )
{
	struct irc_control_channel *icc = ic->data;
	
	set_del( &ic->set, "account" );
	set_del( &ic->set, "fill_by" );
	set_del( &ic->set, "group" );
	
	g_free( icc );
	ic->data = NULL;
	
	return TRUE;
}

static const struct irc_channel_funcs control_channel_funcs = {
	control_channel_privmsg,
	NULL,
	NULL,
	NULL,
	NULL,
	
	control_channel_init,
	control_channel_free,
};

/* Groupchat stub: Only handles /INVITE at least for now. */
static gboolean groupchat_stub_invite( irc_channel_t *ic, irc_user_t *iu )
{
	bee_user_t *bu = iu->bu;
	
	if( iu->bu->ic->acc->prpl->chat_with )
	{
		ic->flags |= IRC_CHANNEL_CHAT_PICKME;
		iu->bu->ic->acc->prpl->chat_with( bu->ic, bu->handle );
		ic->flags &= ~IRC_CHANNEL_CHAT_PICKME;
		return TRUE;
	}
	else
	{
		irc_send_num( ic->irc, 482, "%s :IM protocol does not support room invitations", ic->name );
		return FALSE;
	}
}

static gboolean groupchat_stub_join( irc_channel_t *ic )
{
	struct irc_groupchat_stub *igs = ic->data;
	
	if( igs && igs->acc->ic && igs->acc->prpl->chat_join )
	{
		ic->flags |= IRC_CHANNEL_CHAT_PICKME;
		igs->acc->prpl->chat_join( igs->acc->ic, igs->room, ic->irc->user->nick, NULL );
		ic->flags &= ~IRC_CHANNEL_CHAT_PICKME;
		return FALSE;
	}
	else
	{
		irc_send_num( ic->irc, 403, "%s :Can't join channel, account offline?", ic->name );
		return FALSE;
	}
}

static const struct irc_channel_funcs groupchat_stub_funcs = {
	NULL,
	groupchat_stub_join,
	NULL,
	NULL,
	groupchat_stub_invite,
};
