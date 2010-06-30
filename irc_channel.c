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

extern const struct irc_channel_funcs irc_channel_im_chat_funcs;

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
	
	irc->channels = g_slist_append( irc->channels, ic );
	
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
		
		if( irc_channel_name_cmp( name, ic->name ) == 0 )
			return ic;
	}
	
	return NULL;
}

irc_channel_t *irc_channel_get( irc_t *irc, char *id )
{
	irc_channel_t *ic, *ret = NULL;
	GSList *l;
	int nr;
	
	if( sscanf( id, "%d", &nr ) == 1 && nr < 1000 )
	{
		for( l = irc->channels; l; l = l->next )
		{
			ic = l->data;
			if( ( nr-- ) == 0 ) 
				return ic;
		}
		
		return NULL;
	}
	
	/* Exact match first: Partial match only sucks if there's a channel
	   #aa and #aabb */
	if( ( ret = irc_channel_by_name( irc, id ) ) )
		return ret;
	
	for( l = irc->channels; l; l = l->next )
	{
		ic = l->data;
		
		if( strstr( ic->name, id ) )
		{
			/* Make sure it's a unique match. */
			if( !ret )
				ret = ic;
			else
				return NULL;
		}
	}
	
	return ret;
}

int irc_channel_free( irc_channel_t *ic )
{
	irc_t *irc = ic->irc;
	
	if( ic->flags & IRC_CHANNEL_JOINED )
		irc_channel_del_user( ic, irc->user, FALSE, "Cleaning up channel" );
	
	if( ic->f->_free )
		ic->f->_free( ic );
	
	while( ic->set )
		set_del( &ic->set, ic->set->key );
	
	irc->channels = g_slist_remove( irc->channels, ic );
	while( ic->users )
	{
		g_free( ic->users->data );
		ic->users = g_slist_remove( ic->users, ic->users->data );
	}
	
	g_free( ic->name );
	g_free( ic->topic );
	g_free( ic->topic_who );
	g_free( ic );
	
	return 1;
}

struct irc_channel_free_data
{
	irc_t *irc;
	irc_channel_t *ic;
	char *name;
};

static gboolean irc_channel_free_callback( gpointer data, gint fd, b_input_condition cond )
{
	struct irc_channel_free_data *d = data;
	
	if( g_slist_find( irc_connection_list, d->irc ) &&
	    irc_channel_by_name( d->irc, d->name ) == d->ic &&
	    !( d->ic->flags & IRC_CHANNEL_JOINED ) )
		irc_channel_free( d->ic );

	g_free( d->name );
	g_free( d );
	return FALSE;
}

/* Free the channel, but via the event loop, so after finishing whatever event
   we're currently handling. */
void irc_channel_free_soon( irc_channel_t *ic )
{
	struct irc_channel_free_data *d = g_new0( struct irc_channel_free_data, 1 );
	
	d->irc = ic->irc;
	d->ic = ic;
	d->name = g_strdup( ic->name );
	
	b_timeout_add( 0, irc_channel_free_callback, d );
}

static char *set_eval_channel_type( set_t *set, char *value )
{
	struct irc_channel *ic = set->data;
	const struct irc_channel_funcs *new;
	
	if( strcmp( value, "control" ) == 0 )
		new = &control_channel_funcs;
	else if( strcmp( value, "chat" ) == 0 )
		new = &irc_channel_im_chat_funcs;
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
	
	irc_channel_update_ops( ic, set_getstr( &ic->irc->b->set, "ops" ) );
	
	if( iu == ic->irc->user || ic->flags & IRC_CHANNEL_JOINED )
	{
		ic->flags |= IRC_CHANNEL_JOINED;
		irc_send_join( ic, iu );
	}
	
	return 1;
}

int irc_channel_del_user( irc_channel_t *ic, irc_user_t *iu, gboolean silent, const char *msg )
{
	irc_channel_user_t *icu;
	
	if( !( icu = irc_channel_has_user( ic, iu ) ) )
		return 0;
	
	ic->users = g_slist_remove( ic->users, icu );
	g_free( icu );
	
	if( ic->flags & IRC_CHANNEL_JOINED && !silent )
		irc_send_part( ic, iu, msg );
	
	if( iu == ic->irc->user )
	{
		ic->flags &= ~IRC_CHANNEL_JOINED;
		
		if( ic->irc->status & USTATUS_SHUTDOWN )
		{
			/* Don't do anything fancy when we're shutting down anyway. */
		}
		else if( ic->flags & IRC_CHANNEL_TEMP )
		{
			irc_channel_free_soon( ic );
		}
		else
		{
			/* Flush userlist now. The user won't see it anyway. */
			while( ic->users )
			{
				g_free( ic->users->data );
				ic->users = g_slist_remove( ic->users, ic->users->data );
			}
			irc_channel_add_user( ic, ic->irc->root );
		}
	}
	
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
	
	if( !icu || icu->flags == flags )
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

gboolean irc_channel_name_ok( const char *name_ )
{
	const unsigned char *name = (unsigned char*) name_;
	int i;
	
	if( name_[0] == '\0' )
		return FALSE;
	
	/* Check if the first character is in CTYPES (#&) */
	if( strchr( CTYPES, name_[0] ) == NULL )
		return FALSE;
	
	/* RFC 1459 keeps amazing me: While only a "few" chars are allowed
	   in nicknames, channel names can be pretty much anything as long
	   as they start with # or &. I'll be a little bit more strict and
	   disallow all non-printable characters. */
	for( i = 1; name[i]; i ++ )
		if( name[i] <= ' ' || name[i] == ',' )
			return FALSE;
	
	return TRUE;
}

void irc_channel_name_strip( char *name )
{
	int i, j;
	
	for( i = j = 0; name[i]; i ++ )
		if( name[i] > ' ' && name[i] != ',' )
			name[j++] = name[i];
	
	name[j] = '\0';
}

int irc_channel_name_cmp( const char *a_, const char *b_ )
{
	static unsigned char case_map[256];
	const unsigned char *a = (unsigned char*) a_, *b = (unsigned char*) b_;
	int i;
	
	if( case_map['A'] == '\0' )
	{
		for( i = 33; i < 256; i ++ )
			if( i != ',' )
				case_map[i] = i;
		
		for( i = 0; i < 26; i ++ )
			case_map['A'+i] = 'a' + i;
		
		case_map['['] = '{';
		case_map[']'] = '}';
		case_map['~'] = '`';
		case_map['\\'] = '|';
	}
	
	if( !irc_channel_name_ok( a_ ) || !irc_channel_name_ok( b_ ) )
		return -1;
	
	for( i = 0; a[i] && b[i] && case_map[a[i]] && case_map[b[i]]; i ++ )
	{
		if( case_map[a[i]] == case_map[b[i]] )
			continue;
		else
			return case_map[a[i]] - case_map[b[i]];
	}
	
	return case_map[a[i]] - case_map[b[i]];
}

static gint irc_channel_user_cmp( gconstpointer a_, gconstpointer b_ )
{
	const irc_channel_user_t *a = a_, *b = b_;
	
	return irc_user_cmp( a->iu, b->iu );
}

void irc_channel_update_ops( irc_channel_t *ic, char *value )
{
	irc_channel_user_set_mode( ic, ic->irc->root,
		( strcmp( value, "both" ) == 0 ||
		  strcmp( value, "root" ) == 0 ) ? IRC_CHANNEL_USER_OP : 0 );
	irc_channel_user_set_mode( ic, ic->irc->user,
		( strcmp( value, "both" ) == 0 ||
		  strcmp( value, "user" ) == 0 ) ? IRC_CHANNEL_USER_OP : 0 );
}

char *set_eval_irc_channel_ops( set_t *set, char *value )
{
	irc_t *irc = set->data;
	GSList *l;
	
	if( strcmp( value, "both" ) != 0 && strcmp( value, "none" ) != 0 && 
	    strcmp( value, "user" ) != 0 && strcmp( value, "root" ) != 0 )
		return SET_INVALID;
	
	for( l = irc->channels; l; l = l->next )
		irc_channel_update_ops( l->data, value );
	
	return value;
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
			iu->last_channel = ic;
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

static gboolean control_channel_invite( irc_channel_t *ic, irc_user_t *iu )
{
	struct irc_control_channel *icc = ic->data;
	bee_user_t *bu = iu->bu;
	
	if( bu == NULL )
		return FALSE;
	
	if( icc->type != IRC_CC_TYPE_GROUP )
	{
		irc_send_num( ic->irc, 482, "%s :Invitations are only possible to fill_by=group channels", ic->name );
		return FALSE;
	}
	
	bu->ic->acc->prpl->add_buddy( bu->ic, bu->handle,
	                              icc->group ? icc->group->name : NULL );
	
	return TRUE;
}

static char *set_eval_by_account( set_t *set, char *value );
static char *set_eval_fill_by( set_t *set, char *value );
static char *set_eval_by_group( set_t *set, char *value );
static char *set_eval_by_protocol( set_t *set, char *value );

static gboolean control_channel_init( irc_channel_t *ic )
{
	struct irc_control_channel *icc;
	
	set_add( &ic->set, "account", NULL, set_eval_by_account, ic );
	set_add( &ic->set, "fill_by", "all", set_eval_fill_by, ic );
	set_add( &ic->set, "group", NULL, set_eval_by_group, ic );
	set_add( &ic->set, "protocol", NULL, set_eval_by_protocol, ic );
	
	ic->data = icc = g_new0( struct irc_control_channel, 1 );
	icc->type = IRC_CC_TYPE_DEFAULT;
	
	if( bee_group_by_name( ic->irc->b, ic->name + 1, FALSE ) )
	{
		set_setstr( &ic->set, "group", ic->name + 1 );
		set_setstr( &ic->set, "fill_by", "group" );
	}
	else if( set_setstr( &ic->set, "protocol", ic->name + 1 ) )
	{
		set_setstr( &ic->set, "fill_by", "protocol" );
	}
	else if( set_setstr( &ic->set, "account", ic->name + 1 ) )
	{
		set_setstr( &ic->set, "fill_by", "account" );
	}
	else
	{
		bee_irc_channel_update( ic->irc, ic, NULL );
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
	if( icc->type == IRC_CC_TYPE_ACCOUNT )
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
	else if( strcmp( value, "protocol" ) == 0 )
		icc->type = IRC_CC_TYPE_PROTOCOL;
	else
		return SET_INVALID;
	
	bee_irc_channel_update( ic->irc, ic, NULL );
	return value;
}

static char *set_eval_by_group( set_t *set, char *value )
{
	struct irc_channel *ic = set->data;
	struct irc_control_channel *icc = ic->data;
	
	icc->group = bee_group_by_name( ic->irc->b, value, TRUE );
	if( icc->type == IRC_CC_TYPE_GROUP )
		bee_irc_channel_update( ic->irc, ic, NULL );
	
	return g_strdup( icc->group->name );
}

static char *set_eval_by_protocol( set_t *set, char *value )
{
	struct irc_channel *ic = set->data;
	struct irc_control_channel *icc = ic->data;
	struct prpl *prpl;
	
	if( !( prpl = find_protocol( value ) ) )
		return SET_INVALID;
	
	icc->protocol = prpl;
	if( icc->type == IRC_CC_TYPE_PROTOCOL )
		bee_irc_channel_update( ic->irc, ic, NULL );
	
	return value;
}

static gboolean control_channel_free( irc_channel_t *ic )
{
	struct irc_control_channel *icc = ic->data;
	
	set_del( &ic->set, "account" );
	set_del( &ic->set, "fill_by" );
	set_del( &ic->set, "group" );
	set_del( &ic->set, "protocol" );
	
	g_free( icc );
	ic->data = NULL;
	
	return TRUE;
}

static const struct irc_channel_funcs control_channel_funcs = {
	control_channel_privmsg,
	NULL,
	NULL,
	NULL,
	control_channel_invite,
	
	control_channel_init,
	control_channel_free,
};
