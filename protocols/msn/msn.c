  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2004 Wilmer van der Gaast and others                *
  \********************************************************************/

/* MSN module - Main file; functions to be called from BitlBee          */

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

#include "nogaim.h"
#include "msn.h"

static char *msn_set_display_name( set_t *set, char *value );

static void msn_init( account_t *acc )
{
	set_t *s;
	
	s = set_add( &acc->set, "display_name", NULL, msn_set_display_name, acc );
	s->flags |= ACC_SET_NOSAVE | ACC_SET_ONLINE_ONLY;

	s = set_add( &acc->set, "mail_notifications", "false", set_eval_bool, acc );
}

static void msn_login( account_t *acc )
{
	struct im_connection *ic = imcb_new( acc );
	struct msn_data *md = g_new0( struct msn_data, 1 );
	
	ic->proto_data = md;
	md->fd = -1;
	
	if( strchr( acc->user, '@' ) == NULL )
	{
		imcb_error( ic, "Invalid account name" );
		imc_logout( ic, FALSE );
		return;
	}
	
	imcb_log( ic, "Connecting" );
	
	md->fd = proxy_connect( "messenger.hotmail.com", 1863, msn_ns_connected, ic );
	if( md->fd < 0 )
	{
		imcb_error( ic, "Could not connect to server" );
		imc_logout( ic, TRUE );
		return;
	}
	
	md->ic = ic;
	md->away_state = msn_away_state_list;
	
	msn_connections = g_slist_append( msn_connections, ic );
}

static void msn_logout( struct im_connection *ic )
{
	struct msn_data *md = ic->proto_data;
	GSList *l;
	
	if( md )
	{
		while( md->filetransfers ) {
			imcb_file_canceled( md->filetransfers->data, "Closing connection" );
		}
		
		if( md->fd >= 0 )
			closesocket( md->fd );
		
		if( md->handler )
		{
			if( md->handler->rxq ) g_free( md->handler->rxq );
			if( md->handler->cmd_text ) g_free( md->handler->cmd_text );
			g_free( md->handler );
		}
		
		while( md->switchboards )
			msn_sb_destroy( md->switchboards->data );
		
		msn_msgq_purge( ic, &md->msgq );
		
		while( md->groupcount > 0 )
			g_free( md->grouplist[--md->groupcount] );
		g_free( md->grouplist );
		
		g_free( md );
	}
	
	for( l = ic->permit; l; l = l->next )
		g_free( l->data );
	g_slist_free( ic->permit );
	
	for( l = ic->deny; l; l = l->next )
		g_free( l->data );
	g_slist_free( ic->deny );
	
	msn_connections = g_slist_remove( msn_connections, ic );
}

static int msn_buddy_msg( struct im_connection *ic, char *who, char *message, int away )
{
	struct msn_switchboard *sb;
	
	if( ( sb = msn_sb_by_handle( ic, who ) ) )
	{
		return( msn_sb_sendmessage( sb, message ) );
	}
	else
	{
		struct msn_message *m;
		
		/* Create a message. We have to arrange a usable switchboard, and send the message later. */
		m = g_new0( struct msn_message, 1 );
		m->who = g_strdup( who );
		m->text = g_strdup( message );
		
		return msn_sb_write_msg( ic, m );
	}
	
	return( 0 );
}

static GList *msn_away_states( struct im_connection *ic )
{
	static GList *l = NULL;
	int i;
	
	if( l == NULL )
		for( i = 0; msn_away_state_list[i].number > -1; i ++ )
			l = g_list_append( l, (void*) msn_away_state_list[i].name );
	
	return l;
}

static void msn_set_away( struct im_connection *ic, char *state, char *message )
{
	char buf[1024];
	struct msn_data *md = ic->proto_data;
	const struct msn_away_state *st;
	
	if( strcmp( state, GAIM_AWAY_CUSTOM ) == 0 )
		st = msn_away_state_by_name( "Away" );
	else
		st = msn_away_state_by_name( state );
	
	if( !st ) st = msn_away_state_list;
	md->away_state = st;
	
	g_snprintf( buf, sizeof( buf ), "CHG %d %s\r\n", ++md->trId, st->code );
	msn_write( ic, buf, strlen( buf ) );
}

static void msn_set_my_name( struct im_connection *ic, char *info )
{
	msn_set_display_name( set_find( &ic->acc->set, "display_name" ), info );
}

static void msn_get_info(struct im_connection *ic, char *who) 
{
	/* Just make an URL and let the user fetch the info */
	imcb_log( ic, "%s\n%s: %s%s", _("User Info"), _("For now, fetch yourself"), PROFILE_URL, who );
}

static void msn_add_buddy( struct im_connection *ic, char *who, char *group )
{
	msn_buddy_list_add( ic, "FL", who, who );
}

static void msn_remove_buddy( struct im_connection *ic, char *who, char *group )
{
	msn_buddy_list_remove( ic, "FL", who );
}

static void msn_chat_msg( struct groupchat *c, char *message, int flags )
{
	struct msn_switchboard *sb = msn_sb_by_chat( c );
	
	if( sb )
		msn_sb_sendmessage( sb, message );
	/* FIXME: Error handling (although this can't happen unless something's
	   already severely broken) disappeared here! */
}

static void msn_chat_invite( struct groupchat *c, char *who, char *message )
{
	struct msn_switchboard *sb = msn_sb_by_chat( c );
	char buf[1024];
	
	if( sb )
	{
		g_snprintf( buf, sizeof( buf ), "CAL %d %s\r\n", ++sb->trId, who );
		msn_sb_write( sb, buf, strlen( buf ) );
	}
}

static void msn_chat_leave( struct groupchat *c )
{
	struct msn_switchboard *sb = msn_sb_by_chat( c );
	
	if( sb )
		msn_sb_write( sb, "OUT\r\n", 5 );
}

static struct groupchat *msn_chat_with( struct im_connection *ic, char *who )
{
	struct msn_switchboard *sb;
	
	if( ( sb = msn_sb_by_handle( ic, who ) ) )
	{
		debug( "Converting existing switchboard to %s to a groupchat", who );
		return msn_sb_to_chat( sb );
	}
	else
	{
		struct msn_message *m;
		
		/* Create a magic message. This is quite hackish, but who cares? :-P */
		m = g_new0( struct msn_message, 1 );
		m->who = g_strdup( who );
		m->text = g_strdup( GROUPCHAT_SWITCHBOARD_MESSAGE );
		
		msn_sb_write_msg( ic, m );

		return NULL;
	}
	
	return NULL;
}

static void msn_keepalive( struct im_connection *ic )
{
	msn_write( ic, "PNG\r\n", strlen( "PNG\r\n" ) );
}

static void msn_add_permit( struct im_connection *ic, char *who )
{
	msn_buddy_list_add( ic, "AL", who, who );
}

static void msn_rem_permit( struct im_connection *ic, char *who )
{
	msn_buddy_list_remove( ic, "AL", who );
}

static void msn_add_deny( struct im_connection *ic, char *who )
{
	struct msn_switchboard *sb;
	
	msn_buddy_list_add( ic, "BL", who, who );
	
	/* If there's still a conversation with this person, close it. */
	if( ( sb = msn_sb_by_handle( ic, who ) ) )
	{
		msn_sb_destroy( sb );
	}
}

static void msn_rem_deny( struct im_connection *ic, char *who )
{
	msn_buddy_list_remove( ic, "BL", who );
}

static int msn_send_typing( struct im_connection *ic, char *who, int typing )
{
	if( typing & OPT_TYPING )
		return( msn_buddy_msg( ic, who, TYPING_NOTIFICATION_MESSAGE, 0 ) );
	else
		return( 1 );
}

static char *msn_set_display_name( set_t *set, char *value )
{
	account_t *acc = set->data;
	struct im_connection *ic = acc->ic;
	struct msn_data *md;
	char buf[1024], *fn;
	
	/* Double-check. */
	if( ic == NULL )
		return NULL;
	
	md = ic->proto_data;
	
	if( strlen( value ) > 129 )
	{
		imcb_log( ic, "Maximum name length exceeded" );
		return NULL;
	}
	
	fn = msn_http_encode( value );
	
	g_snprintf( buf, sizeof( buf ), "REA %d %s %s\r\n", ++md->trId, ic->acc->user, fn );
	msn_write( ic, buf, strlen( buf ) );
	g_free( fn );
	
	/* Returning NULL would be better, because the server still has to
	   confirm the name change. However, it looks a bit confusing to the
	   user. */
	return value;
}

void msn_initmodule()
{
	struct prpl *ret = g_new0(struct prpl, 1);
	
	ret->name = "msn";
	ret->login = msn_login;
	ret->init = msn_init;
	ret->logout = msn_logout;
	ret->buddy_msg = msn_buddy_msg;
	ret->away_states = msn_away_states;
	ret->set_away = msn_set_away;
	ret->get_info = msn_get_info;
	ret->set_my_name = msn_set_my_name;
	ret->add_buddy = msn_add_buddy;
	ret->remove_buddy = msn_remove_buddy;
	ret->chat_msg = msn_chat_msg;
	ret->chat_invite = msn_chat_invite;
	ret->chat_leave = msn_chat_leave;
	ret->chat_with = msn_chat_with;
	ret->keepalive = msn_keepalive;
	ret->add_permit = msn_add_permit;
	ret->rem_permit = msn_rem_permit;
	ret->add_deny = msn_add_deny;
	ret->rem_deny = msn_rem_deny;
	ret->send_typing = msn_send_typing;
	ret->handle_cmp = g_strcasecmp;
	ret->transfer_request = msn_ftp_transfer_request;

	register_protocol(ret);
}
