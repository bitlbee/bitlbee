  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2012 Wilmer van der Gaast and others                *
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
#include "soap.h"
#include "msn.h"

int msn_chat_id;
GSList *msn_connections;
GSList *msn_switchboards;

static char *set_eval_display_name( set_t *set, char *value );

static void msn_init( account_t *acc )
{
	set_t *s;
	
	s = set_add( &acc->set, "display_name", NULL, set_eval_display_name, acc );
	s->flags |= ACC_SET_NOSAVE | ACC_SET_ONLINE_ONLY;
	
	set_add( &acc->set, "mail_notifications", "false", set_eval_bool, acc );
	set_add( &acc->set, "switchboard_keepalives", "false", set_eval_bool, acc );
	
	acc->flags |= ACC_FLAG_AWAY_MESSAGE | ACC_FLAG_STATUS_MESSAGE;
}

static void msn_login( account_t *acc )
{
	struct im_connection *ic = imcb_new( acc );
	struct msn_data *md = g_new0( struct msn_data, 1 );
	
	ic->proto_data = md;
	
	if( strchr( acc->user, '@' ) == NULL )
	{
		imcb_error( ic, "Invalid account name" );
		imc_logout( ic, FALSE );
		return;
	}
	
	md->ic = ic;
	md->away_state = msn_away_state_list;
	md->domaintree = g_tree_new( msn_domaintree_cmp );
	md->ns->fd = -1;
	
	msn_connections = g_slist_prepend( msn_connections, ic );
	
	imcb_log( ic, "Connecting" );
	msn_ns_connect( ic, md->ns, MSN_NS_HOST, MSN_NS_PORT );
}

static void msn_logout( struct im_connection *ic )
{
	struct msn_data *md = ic->proto_data;
	GSList *l;
	int i;
	
	if( md )
	{
		/** Disabling MSN ft support for now.
		while( md->filetransfers ) {
			imcb_file_canceled( md->filetransfers->data, "Closing connection" );
		}
		*/
		
		msn_ns_close( md->ns );
		
		while( md->switchboards )
			msn_sb_destroy( md->switchboards->data );
		
		msn_msgq_purge( ic, &md->msgq );
		msn_soapq_flush( ic, FALSE );
		
		for( i = 0; i < sizeof( md->tokens ) / sizeof( md->tokens[0] ); i ++ )
			g_free( md->tokens[i] );
		g_free( md->lock_key );
		g_free( md->pp_policy );
		g_free( md->uuid );
		
		while( md->groups )
		{
			struct msn_group *mg = md->groups->data;
			g_free( mg->id );
			g_free( mg->name );
			g_free( mg );
			md->groups = g_slist_remove( md->groups, mg );
		}
		
		g_free( md->profile_rid );
		
		if( md->domaintree )
			g_tree_destroy( md->domaintree );
		md->domaintree = NULL;
		
		while( md->grpq )
		{
			struct msn_groupadd *ga = md->grpq->data;
			g_free( ga->group );
			g_free( ga->who );
			g_free( ga );
			md->grpq = g_slist_remove( md->grpq, ga );
		}
		
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
	
#ifdef DEBUG
	if( strcmp( who, "raw" ) == 0 )
	{
		msn_ns_write( ic, -1, "%s\r\n", message );
	}
	else
#endif
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
		for( i = 0; *msn_away_state_list[i].code; i ++ )
			if( *msn_away_state_list[i].name )
				l = g_list_append( l, (void*) msn_away_state_list[i].name );
	
	return l;
}

static void msn_set_away( struct im_connection *ic, char *state, char *message )
{
	char *uux;
	struct msn_data *md = ic->proto_data;
	
	if( state == NULL )
		md->away_state = msn_away_state_list;
	else if( ( md->away_state = msn_away_state_by_name( state ) ) == NULL )
		md->away_state = msn_away_state_list + 1;
	
	if( !msn_ns_write( ic, -1, "CHG %d %s %d:%02d\r\n", ++md->trId, md->away_state->code, MSN_CAP1, MSN_CAP2 ) )
		return;
	
	uux = g_markup_printf_escaped( "<EndpointData><Capabilities>%d:%02d"
	                               "</Capabilities></EndpointData>",
	                               MSN_CAP1, MSN_CAP2 );
	msn_ns_write( ic, -1, "UUX %d %zd\r\n%s", ++md->trId, strlen( uux ), uux );
	g_free( uux );
	
	uux = g_markup_printf_escaped( "<PrivateEndpointData><EpName>%s</EpName>"
	                               "<Idle>%s</Idle><ClientType>%d</ClientType>"
	                               "<State>%s</State></PrivateEndpointData>",
	                               md->uuid,
	                               strcmp( md->away_state->code, "IDL" ) ? "false" : "true",
	                               1, /* ? */
	                               md->away_state->code );
	msn_ns_write( ic, -1, "UUX %d %zd\r\n%s", ++md->trId, strlen( uux ), uux );
	g_free( uux );
	
	uux = g_markup_printf_escaped( "<Data><DDP></DDP><PSM>%s</PSM>"
	                               "<CurrentMedia></CurrentMedia>"
	                               "<MachineGuid>%s</MachineGuid></Data>",
	                               message ? message : "", md->uuid );
	msn_ns_write( ic, -1, "UUX %d %zd\r\n%s", ++md->trId, strlen( uux ), uux );
	g_free( uux );
}

static void msn_get_info(struct im_connection *ic, char *who) 
{
	/* Just make an URL and let the user fetch the info */
	imcb_log( ic, "%s\n%s: %s%s", _("User Info"), _("For now, fetch yourself"), PROFILE_URL, who );
}

static void msn_add_buddy( struct im_connection *ic, char *who, char *group )
{
	struct bee_user *bu = bee_user_by_handle( ic->bee, ic, who );
	
	msn_buddy_list_add( ic, MSN_BUDDY_FL, who, who, group );
	if( bu && bu->group )
		msn_buddy_list_remove( ic, MSN_BUDDY_FL, who, bu->group->name );
}

static void msn_remove_buddy( struct im_connection *ic, char *who, char *group )
{
	msn_buddy_list_remove( ic, MSN_BUDDY_FL, who, NULL );
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
	
	if( sb )
		msn_sb_write( sb, "CAL %d %s\r\n", ++sb->trId, who );
}

static void msn_chat_leave( struct groupchat *c )
{
	struct msn_switchboard *sb = msn_sb_by_chat( c );
	
	if( sb )
		msn_sb_write( sb, "OUT\r\n" );
}

static struct groupchat *msn_chat_with( struct im_connection *ic, char *who )
{
	struct msn_switchboard *sb;
	struct groupchat *c = imcb_chat_new( ic, who );
	
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

		return c;
	}
}

static void msn_keepalive( struct im_connection *ic )
{
	msn_ns_write( ic, -1, "PNG\r\n" );
}

static void msn_add_permit( struct im_connection *ic, char *who )
{
	msn_buddy_list_add( ic, MSN_BUDDY_AL, who, who, NULL );
}

static void msn_rem_permit( struct im_connection *ic, char *who )
{
	msn_buddy_list_remove( ic, MSN_BUDDY_AL, who, NULL );
}

static void msn_add_deny( struct im_connection *ic, char *who )
{
	struct msn_switchboard *sb;
	
	msn_buddy_list_add( ic, MSN_BUDDY_BL, who, who, NULL );
	
	/* If there's still a conversation with this person, close it. */
	if( ( sb = msn_sb_by_handle( ic, who ) ) )
	{
		msn_sb_destroy( sb );
	}
}

static void msn_rem_deny( struct im_connection *ic, char *who )
{
	msn_buddy_list_remove( ic, MSN_BUDDY_BL, who, NULL );
}

static int msn_send_typing( struct im_connection *ic, char *who, int typing )
{
	struct bee_user *bu = bee_user_by_handle( ic->bee, ic, who );
	
	if( !( bu->flags & BEE_USER_ONLINE ) )
		return 0;
	else if( typing & OPT_TYPING )
		return( msn_buddy_msg( ic, who, TYPING_NOTIFICATION_MESSAGE, 0 ) );
	else
		return 1;
}

static char *set_eval_display_name( set_t *set, char *value )
{
	account_t *acc = set->data;
	struct im_connection *ic = acc->ic;
	struct msn_data *md = ic->proto_data;
	
	if( md->flags & MSN_EMAIL_UNVERIFIED )
		imcb_log( ic, "Warning: Your e-mail address is unverified. MSN doesn't allow "
		              "changing your display name until your e-mail address is verified." );
	
	if( md->flags & MSN_GOT_PROFILE_DN )
		msn_soap_profile_set_dn( ic, value );
	else
		msn_soap_addressbook_set_display_name( ic, value );
	
	return msn_ns_set_display_name( ic, value ) ? value : NULL;
}

static void msn_buddy_data_add( bee_user_t *bu )
{
	struct msn_data *md = bu->ic->proto_data;
	bu->data = g_new0( struct msn_buddy_data, 1 );
	g_tree_insert( md->domaintree, bu->handle, bu );
}

static void msn_buddy_data_free( bee_user_t *bu )
{
	struct msn_data *md = bu->ic->proto_data;
	struct msn_buddy_data *bd = bu->data;
	
	g_free( bd->cid );
	g_free( bd );
	
	g_tree_remove( md->domaintree, bu->handle );
}

GList *msn_buddy_action_list( bee_user_t *bu )
{
	static GList *ret = NULL;
	
	if( ret == NULL )
	{
		static const struct buddy_action ba[2] = {
			{ "NUDGE", "Draw attention" },
		};
		
		ret = g_list_prepend( ret, (void*) ba + 0 );
	}
	
	return ret;
}

void *msn_buddy_action( struct bee_user *bu, const char *action, char * const args[], void *data )
{
	if( g_strcasecmp( action, "NUDGE" ) == 0 )
		msn_buddy_msg( bu->ic, bu->handle, NUDGE_MESSAGE, 0 );
	
	return NULL;
}

void msn_initmodule()
{
	struct prpl *ret = g_new0(struct prpl, 1);
	
	ret->name = "msn";
	ret->mms = 1409;         /* this guess taken from libotr UPGRADING file */
	ret->login = msn_login;
	ret->init = msn_init;
	ret->logout = msn_logout;
	ret->buddy_msg = msn_buddy_msg;
	ret->away_states = msn_away_states;
	ret->set_away = msn_set_away;
	ret->get_info = msn_get_info;
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
	ret->buddy_data_add = msn_buddy_data_add;
	ret->buddy_data_free = msn_buddy_data_free;
	ret->buddy_action_list = msn_buddy_action_list;
	ret->buddy_action = msn_buddy_action;
	
	//ret->transfer_request = msn_ftp_transfer_request;

	register_protocol(ret);
}
