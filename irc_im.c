  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2010 Wilmer van der Gaast and others                *
  \********************************************************************/

/* Some glue to put the IRC and the IM stuff together.                  */

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
#include "dcc.h"

/* IM->IRC callbacks: Simple IM/buddy-related stuff. */

static const struct irc_user_funcs irc_user_im_funcs;

static gboolean bee_irc_user_new( bee_t *bee, bee_user_t *bu )
{
	irc_user_t *iu;
	char nick[MAX_NICK_LENGTH+1], *s;
	
	memset( nick, 0, MAX_NICK_LENGTH + 1 );
	strcpy( nick, nick_get( bu->ic->acc, bu->handle ) );
	
	bu->ui_data = iu = irc_user_new( (irc_t*) bee->ui_data, nick );
	iu->bu = bu;
	
	if( ( s = strchr( bu->handle, '@' ) ) )
	{
		iu->host = g_strdup( s + 1 );
		iu->user = g_strndup( bu->handle, s - bu->handle );
	}
	else if( bu->ic->acc->server )
	{
		iu->host = g_strdup( bu->ic->acc->server );
		iu->user = g_strdup( bu->handle );
		
		/* s/ /_/ ... important for AOL screennames */
		for( s = iu->user; *s; s ++ )
			if( *s == ' ' )
				*s = '_';
	}
	else
	{
		iu->host = g_strdup( bu->ic->acc->prpl->name );
		iu->user = g_strdup( bu->handle );
	}
	
	if( set_getbool( &bee->set, "private" ) )
		iu->flags |= IRC_USER_PRIVATE;
	
	if( bu->flags & BEE_USER_LOCAL )
	{
		char *s = set_getstr( &bee->set, "handle_unknown" );
		
		if( strcmp( s, "add_private" ) == 0 )
			iu->flags |= IRC_USER_PRIVATE;
		else if( strcmp( s, "add_channel" ) == 0 )
			iu->flags &= ~IRC_USER_PRIVATE;
	}
	
	iu->f = &irc_user_im_funcs;
	//iu->last_typing_notice = 0;
	
	return TRUE;
}

static gboolean bee_irc_user_free( bee_t *bee, bee_user_t *bu )
{
	return irc_user_free( bee->ui_data, (irc_user_t *) bu->ui_data );
}

static gboolean bee_irc_user_status( bee_t *bee, bee_user_t *bu, bee_user_t *old )
{
	irc_t *irc = bee->ui_data;
	irc_user_t *iu = bu->ui_data;
	
	/* Do this outside the if below since away state can change without
	   the online state changing. */
	iu->flags &= ~IRC_USER_AWAY;
	if( bu->flags & BEE_USER_AWAY || !( bu->flags & BEE_USER_ONLINE ) )
		iu->flags |= IRC_USER_AWAY;
	
	if( ( bu->flags & BEE_USER_ONLINE ) != ( old->flags & BEE_USER_ONLINE ) )
	{
		if( bu->flags & BEE_USER_ONLINE )
		{
			if( g_hash_table_lookup( irc->watches, iu->key ) )
				irc_send_num( irc, 600, "%s %s %s %d :%s", iu->nick, iu->user,
				              iu->host, (int) time( NULL ), "logged online" );
		}
		else
		{
			if( g_hash_table_lookup( irc->watches, iu->key ) )
				irc_send_num( irc, 601, "%s %s %s %d :%s", iu->nick, iu->user,
				              iu->host, (int) time( NULL ), "logged offline" );
		}
	}
	
	bee_irc_channel_update( irc, NULL, iu );
	
	return TRUE;
}

void bee_irc_channel_update( irc_t *irc, irc_channel_t *ic, irc_user_t *iu )
{
	struct irc_control_channel *icc;
	GSList *l;
	gboolean show;
	
	if( ic == NULL )
	{
		for( l = irc->channels; l; l = l->next )
		{
			ic = l->data;
			/* TODO: Just add a type flag or so.. */
			if( ic->f == irc->default_channel->f )
				bee_irc_channel_update( irc, ic, iu );
		}
		return;
	}
	if( iu == NULL )
	{
		for( l = irc->users; l; l = l->next )
		{
			iu = l->data;
			if( iu->bu )
				bee_irc_channel_update( irc, ic, l->data );
		}
		return;
	}
	
	icc = ic->data;
	
	if( !( iu->bu->flags & BEE_USER_ONLINE ) )
		show = FALSE;
	else if( icc->type == IRC_CC_TYPE_DEFAULT )
		show = TRUE;
	else if( icc->type == IRC_CC_TYPE_GROUP )
		show = iu->bu->group == icc->group;
	else if( icc->type == IRC_CC_TYPE_ACCOUNT )
		show = iu->bu->ic->acc == icc->account;
	
	if( !show )
	{
		irc_channel_del_user( ic, iu );
	}
	else
	{
		irc_channel_add_user( ic, iu );
		
		if( set_getbool( &irc->b->set, "away_devoice" ) )
			irc_channel_user_set_mode( ic, iu, ( iu->bu->flags & BEE_USER_AWAY ) ?
			                           0 : IRC_CHANNEL_USER_VOICE );
	}
}

static gboolean bee_irc_user_msg( bee_t *bee, bee_user_t *bu, const char *msg, time_t sent_at )
{
	irc_t *irc = bee->ui_data;
	irc_channel_t *ic = irc->default_channel;
	irc_user_t *iu = (irc_user_t *) bu->ui_data;
	char *dst, *prefix = NULL;
	char *wrapped, *ts = NULL;
	
	if( sent_at > 0 && set_getbool( &irc->b->set, "display_timestamps" ) )
		ts = irc_format_timestamp( irc, sent_at );
	
	if( iu->flags & IRC_USER_PRIVATE )
	{
		dst = irc->user->nick;
		prefix = ts;
		ts = NULL;
	}
	else
	{
		dst = ic->name;
		prefix = g_strdup_printf( "%s%s%s", irc->user->nick, set_getstr( &bee->set, "to_char" ), ts ? : "" );
	}
	
	wrapped = word_wrap( msg, 425 );
	irc_send_msg( iu, "PRIVMSG", dst, wrapped, prefix );
	
	g_free( wrapped );
	g_free( prefix );
	g_free( ts );
	
	return TRUE;
}

static gboolean bee_irc_user_typing( bee_t *bee, bee_user_t *bu, uint32_t flags )
{
	irc_t *irc = (irc_t *) bee->ui_data;
	
	if( set_getbool( &bee->set, "typing_notice" ) )
		irc_send_msg_f( (irc_user_t *) bu->ui_data, "PRIVMSG", irc->user->nick,
		                "\001TYPING %d\001", ( flags >> 8 ) & 3 );
	else
		return FALSE;
	
	return TRUE;
}

static gboolean bee_irc_user_fullname( bee_t *bee, bee_user_t *bu )
{
	irc_user_t *iu = (irc_user_t *) bu->ui_data;
	irc_t *irc = (irc_t *) bee->ui_data;
	char *s;
	
	if( iu->fullname != iu->nick )
		g_free( iu->fullname );
	iu->fullname = g_strdup( bu->fullname );
	
	/* Strip newlines (unlikely, but IRC-unfriendly so they must go)
	   TODO(wilmer): Do the same with away msgs again! */
	for( s = iu->fullname; *s; s ++ )
		if( isspace( *s ) ) *s = ' ';
	
	if( ( bu->ic->flags & OPT_LOGGED_IN ) && set_getbool( &bee->set, "display_namechanges" ) )
	{
		char *msg = g_strdup_printf( "<< \002BitlBee\002 - Changed name to `%s' >>", iu->fullname );
		irc_send_msg( iu, "NOTICE", irc->user->nick, msg, NULL );
	}
	
	s = set_getstr( &bu->ic->acc->set, "nick_source" );
	if( strcmp( s, "handle" ) != 0 )
	{
		char *name = g_strdup( bu->fullname );
		
		if( strcmp( s, "first_name" ) == 0 )
		{
			int i;
			for( i = 0; name[i] && !isspace( name[i] ); i ++ ) {}
			name[i] = '\0';
		}
		
		imcb_buddy_nick_hint( bu->ic, bu->handle, name );
		
		g_free( name );
	}
	
	return TRUE;
}

/* IRC->IM calls */

static gboolean bee_irc_user_privmsg( irc_user_t *iu, const char *msg )
{
	if( iu->bu )
		return bee_user_msg( iu->irc->b, iu->bu, msg, 0 );
	else
		return FALSE;
}

static gboolean bee_irc_user_ctcp( irc_user_t *iu, char *const *ctcp )
{
	if( ctcp[1] && g_strcasecmp( ctcp[0], "DCC" ) == 0
	            && g_strcasecmp( ctcp[1], "SEND" ) == 0 )
	{
		if( iu->bu && iu->bu->ic && iu->bu->ic->acc->prpl->transfer_request )
		{
			file_transfer_t *ft = dcc_request( iu->bu->ic, ctcp );
			if ( ft )
				iu->bu->ic->acc->prpl->transfer_request( iu->bu->ic, ft, iu->bu->handle );
			
			return TRUE;
		}
	}
	else if( g_strcasecmp( ctcp[0], "TYPING" ) == 0 )
	{
		if( iu->bu && iu->bu->ic && iu->bu->ic->acc->prpl->send_typing && ctcp[1] )
		{
			int st = ctcp[1][0];
			if( st >= '0' && st <= '2' )
			{
				st <<= 8;
				iu->bu->ic->acc->prpl->send_typing( iu->bu->ic, iu->bu->handle, st );
			}
			
			return TRUE;
		}
	}
	
	return FALSE;
}

static const struct irc_user_funcs irc_user_im_funcs = {
	bee_irc_user_privmsg,
	bee_irc_user_ctcp,
};


/* IM->IRC: Groupchats */
static const struct irc_channel_funcs irc_channel_im_chat_funcs;

static gboolean bee_irc_chat_new( bee_t *bee, struct groupchat *c )
{
	irc_t *irc = bee->ui_data;
	irc_channel_t *ic;
	char *topic;
	GSList *l;
	int i;
	
	/* Try to find a channel that expects to receive a groupchat.
	   This flag is set by groupchat_stub_invite(). */
	for( l = irc->channels; l; l = l->next )
	{
		ic = l->data;
		if( ic->flags & IRC_CHANNEL_CHAT_PICKME )
			break;
	}
	
	/* If we found none, just generate some stupid name. */
	if( l == NULL ) for( i = 0; i <= 999; i ++ )
	{
		char name[16];
		sprintf( name, "&chat_%03d", i );
		if( ( ic = irc_channel_new( irc, name ) ) )
			break;
	}
	
	if( ic == NULL )
		return FALSE;
	
	c->ui_data = ic;
	ic->data = c;
	ic->f = &irc_channel_im_chat_funcs;
	
	topic = g_strdup_printf( "BitlBee groupchat: \"%s\". Please keep in mind that root-commands won't work here. Have fun!", c->title );
	irc_channel_set_topic( ic, topic, irc->root );
	g_free( topic );
	
	return TRUE;
}

static gboolean bee_irc_chat_free( bee_t *bee, struct groupchat *c )
{
	irc_channel_t *ic = c->ui_data;
	
	if( ic->flags & IRC_CHANNEL_JOINED )
		irc_channel_printf( ic, "Cleaning up channel, bye!" );
	
	irc_channel_free( ic );
	
	return TRUE;
}

static gboolean bee_irc_chat_log( bee_t *bee, struct groupchat *c, const char *text )
{
	irc_channel_t *ic = c->ui_data;
	
	irc_channel_printf( ic, "%s", text );
	
	return TRUE;
}

static gboolean bee_irc_chat_msg( bee_t *bee, struct groupchat *c, bee_user_t *bu, const char *msg, time_t sent_at )
{
	irc_t *irc = bee->ui_data;
	irc_user_t *iu = bu->ui_data;
	irc_channel_t *ic = c->ui_data;
	char *ts = NULL;
	
	if( sent_at > 0 && set_getbool( &bee->set, "display_timestamps" ) )
		ts = irc_format_timestamp( irc, sent_at );
	
	irc_send_msg( iu, "PRIVMSG", ic->name, msg, ts );
	g_free( ts );
	
	return TRUE;
}

static gboolean bee_irc_chat_add_user( bee_t *bee, struct groupchat *c, bee_user_t *bu )
{
	irc_t *irc = bee->ui_data;
	
	irc_channel_add_user( c->ui_data, bu == bee->user ? irc->user : bu->ui_data );
	
	return TRUE;
}

static gboolean bee_irc_chat_remove_user( bee_t *bee, struct groupchat *c, bee_user_t *bu )
{
	irc_t *irc = bee->ui_data;
	
	irc_channel_del_user( c->ui_data, bu == bee->user ? irc->user : bu->ui_data );
	
	return TRUE;
}

static gboolean bee_irc_chat_topic( bee_t *bee, struct groupchat *c, const char *new, bee_user_t *bu )
{
	irc_t *irc = bee->ui_data;
	irc_user_t *iu;
	
	if( bu == NULL )
		iu = irc->root;
	else if( bu == bee->user )
		iu = irc->user;
	else
		iu = bu->ui_data;
	
	irc_channel_set_topic( c->ui_data, new, iu );
	
	return TRUE;
}

static gboolean bee_irc_chat_name_hint( bee_t *bee, struct groupchat *c, const char *name )
{
	irc_t *irc = bee->ui_data;
	irc_channel_t *ic = c->ui_data;
	char stripped[MAX_NICK_LENGTH+1], *full_name;
	
	/* Don't rename a channel if the user's in it already. */
	if( ic->flags & IRC_CHANNEL_JOINED )
		return FALSE;
	
	strncpy( stripped, name, MAX_NICK_LENGTH );
	stripped[MAX_NICK_LENGTH] = '\0';
	nick_strip( stripped );
	if( set_getbool( &bee->set, "lcnicks" ) )
		nick_lc( stripped );
	
	full_name = g_strdup_printf( "&%s", stripped );
	
	if( stripped[0] && irc_channel_by_name( irc, full_name ) == NULL )
	{
		g_free( ic->name );
		ic->name = full_name;
	}
	else
	{
		g_free( full_name );
	}
	
	return TRUE;
}

/* IRC->IM */
static gboolean bee_irc_channel_chat_privmsg( irc_channel_t *ic, const char *msg )
{
	struct groupchat *c = ic->data;
	
	bee_chat_msg( ic->irc->b, c, msg, 0 );
	
	return TRUE;
	
}

static gboolean bee_irc_channel_chat_part( irc_channel_t *ic, const char *msg )
{
	struct groupchat *c = ic->data;
	
	if( c->ic->acc->prpl->chat_leave )
		c->ic->acc->prpl->chat_leave( c );
	
	return TRUE;
	
}

static gboolean bee_irc_channel_chat_topic( irc_channel_t *ic, const char *new )
{
	return TRUE;
}

static gboolean bee_irc_channel_chat_invite( irc_channel_t *ic, irc_user_t *iu )
{
	struct groupchat *c = ic->data;
	
	if( iu->bu->ic != c->ic )
		irc_send_num( ic->irc, 482, "%s :Can't mix different IM networks in one groupchat", ic->name );
	else if( c->ic->acc->prpl->chat_invite )
		c->ic->acc->prpl->chat_invite( c, iu->bu->handle, NULL );
	else
		irc_send_num( ic->irc, 482, "%s :IM protocol does not support room invitations", ic->name );
	
	return TRUE;
}

static const struct irc_channel_funcs irc_channel_im_chat_funcs = {
	bee_irc_channel_chat_privmsg,
	NULL, /* join */
	bee_irc_channel_chat_part,
	bee_irc_channel_chat_topic,
	bee_irc_channel_chat_invite,
};


/* IM->IRC: File transfers */
static file_transfer_t *bee_irc_ft_in_start( bee_t *bee, bee_user_t *bu, const char *file_name, size_t file_size )
{
	return dccs_send_start( bu->ic, (irc_user_t *) bu->ui_data, file_name, file_size );
}

static gboolean bee_irc_ft_out_start( struct im_connection *ic, file_transfer_t *ft )
{
	return dccs_recv_start( ft );
}

static void bee_irc_ft_close( struct im_connection *ic, file_transfer_t *ft )
{
	return dcc_close( ft );
}

static void bee_irc_ft_finished( struct im_connection *ic, file_transfer_t *file )
{
	dcc_file_transfer_t *df = file->priv;

	if( file->bytes_transferred >= file->file_size )
		dcc_finish( file );
	else
		df->proto_finished = TRUE;
}

const struct bee_ui_funcs irc_ui_funcs = {
	bee_irc_user_new,
	bee_irc_user_free,
	bee_irc_user_fullname,
	bee_irc_user_status,
	bee_irc_user_msg,
	bee_irc_user_typing,
	
	bee_irc_chat_new,
	bee_irc_chat_free,
	bee_irc_chat_log,
	bee_irc_chat_msg,
	bee_irc_chat_add_user,
	bee_irc_chat_remove_user,
	bee_irc_chat_topic,
	bee_irc_chat_name_hint,
	
	bee_irc_ft_in_start,
	bee_irc_ft_out_start,
	bee_irc_ft_close,
	bee_irc_ft_finished,
};
