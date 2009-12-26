/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  libpurple module - Main file                                             *
*                                                                           *
*  Copyright 2009 Wilmer van der Gaast <wilmer@gaast.net>                   *
*                                                                           *
*  This program is free software; you can redistribute it and/or modify     *
*  it under the terms of the GNU General Public License as published by     *
*  the Free Software Foundation; either version 2 of the License, or        *
*  (at your option) any later version.                                      *
*                                                                           *
*  This program is distributed in the hope that it will be useful,          *
*  but WITHOUT ANY WARRANTY; without even the implied warranty of           *
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
*  GNU General Public License for more details.                             *
*                                                                           *
*  You should have received a copy of the GNU General Public License along  *
*  with this program; if not, write to the Free Software Foundation, Inc.,  *
*  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.              *
*                                                                           *
\***************************************************************************/

#include "bitlbee.h"
#include "help.h"

#include <stdarg.h>

#include <glib.h>
#include <purple.h>

GSList *purple_connections;

/* This makes me VERY sad... :-( But some libpurple callbacks come in without
   any context so this is the only way to get that. Don't want to support
   libpurple in daemon mode anyway. */
static irc_t *local_irc;

static struct im_connection *purple_ic_by_pa( PurpleAccount *pa )
{
	GSList *i;
	
	for( i = purple_connections; i; i = i->next )
		if( ((struct im_connection *)i->data)->proto_data == pa )
			return i->data;
	
	return NULL;
}

static struct im_connection *purple_ic_by_gc( PurpleConnection *gc )
{
	return purple_ic_by_pa( purple_connection_get_account( gc ) );
}

static void purple_init( account_t *acc )
{
	PurplePlugin *prpl = purple_plugins_find_with_id( (char*) acc->prpl->data );
	PurplePluginProtocolInfo *pi = prpl->info->extra_info;
	GList *i;
	
	for( i = pi->protocol_options; i; i = i->next )
	{
		PurpleAccountOption *o = i->data;
		const char *name;
		char *def = NULL;
		set_eval eval = NULL;
		set_t *s;
		
		name = purple_account_option_get_setting( o );
		
		switch( purple_account_option_get_type( o ) )
		{
		case PURPLE_PREF_STRING:
			def = g_strdup( purple_account_option_get_default_string( o ) );
			break;
		
		case PURPLE_PREF_INT:
			def = g_strdup_printf( "%d", purple_account_option_get_default_int( o ) );
			eval = set_eval_int;
			break;
		
		case PURPLE_PREF_BOOLEAN:
			if( purple_account_option_get_default_bool( o ) )
				def = g_strdup( "true" );
			else
				def = g_strdup( "false" );
			eval = set_eval_bool;
			break;
		
		default:
			fprintf( stderr, "Setting with unknown type: %s (%d)\n", name, purple_account_option_get_type( o ) );
			name = NULL;
		}
		
		if( name != NULL )
		{
			s = set_add( &acc->set, name, def, eval, acc );
			s->flags |= ACC_SET_OFFLINE_ONLY;
			g_free( def );
		}
	}
}

static void purple_sync_settings( account_t *acc, PurpleAccount *pa )
{
	PurplePlugin *prpl = purple_plugins_find_with_id( pa->protocol_id );
	PurplePluginProtocolInfo *pi = prpl->info->extra_info;
	GList *i;
	
	for( i = pi->protocol_options; i; i = i->next )
	{
		PurpleAccountOption *o = i->data;
		const char *name;
		set_t *s;
		
		name = purple_account_option_get_setting( o );
		s = set_find( &acc->set, name );
		if( s->value == NULL )
			continue;
		
		switch( purple_account_option_get_type( o ) )
		{
		case PURPLE_PREF_STRING:
			purple_account_set_string( pa, name, set_getstr( &acc->set, name ) );
			break;
		
		case PURPLE_PREF_INT:
			purple_account_set_int( pa, name, set_getint( &acc->set, name ) );
			break;
		
		case PURPLE_PREF_BOOLEAN:
			purple_account_set_bool( pa, name, set_getbool( &acc->set, name ) );
			break;
		
		default:
			break;
		}
	}
}

static void purple_login( account_t *acc )
{
	struct im_connection *ic = imcb_new( acc );
	PurpleAccount *pa;
	
	if( local_irc != NULL && local_irc != acc->irc )
	{
		irc_usermsg( acc->irc, "Daemon mode detected. Do *not* try to use libpurple in daemon mode! "
		                       "Please use inetd or ForkDaemon mode instead." );
		return;
	}
	local_irc = acc->irc;
	
	/* For now this is needed in the _connected() handlers if using
	   GLib event handling, to make sure we're not handling events
	   on dead connections. */
	purple_connections = g_slist_prepend( purple_connections, ic );
	
	ic->proto_data = pa = purple_account_new( acc->user, (char*) acc->prpl->data );
	purple_account_set_password( pa, acc->pass );
	purple_sync_settings( acc, pa );
	
	purple_account_set_enabled( pa, "BitlBee", TRUE );
}

static void purple_logout( struct im_connection *ic )
{
	PurpleAccount *pa = ic->proto_data;
	
	purple_account_set_enabled( pa, "BitlBee", FALSE );
	purple_connections = g_slist_remove( purple_connections, ic );
	purple_accounts_remove( pa );
}

static int purple_buddy_msg( struct im_connection *ic, char *who, char *message, int flags )
{
	PurpleConversation *conv;
	
	if( ( conv = purple_find_conversation_with_account( PURPLE_CONV_TYPE_IM,
	                                                    who, ic->proto_data ) ) == NULL )
	{
		conv = purple_conversation_new( PURPLE_CONV_TYPE_IM,
		                                ic->proto_data, who );
	}
	
	purple_conv_im_send( purple_conversation_get_im_data( conv ), message );
	
	return 1;
}

static GList *purple_away_states( struct im_connection *ic )
{
	PurpleAccount *pa = ic->proto_data;
	GList *st, *ret = NULL;
	
	for( st = purple_account_get_status_types( pa ); st; st = st->next )
		ret = g_list_append( ret, (void*) purple_status_type_get_name( st->data ) );
	
	return ret;
}

static void purple_set_away( struct im_connection *ic, char *state_txt, char *message )
{
	PurpleAccount *pa = ic->proto_data;
	GList *status_types = purple_account_get_status_types( pa ), *st;
	PurpleStatusType *pst = NULL;
	
	for( st = status_types; st; st = st->next )
	{
		pst = st->data;
		
		if( g_strcasecmp( state_txt, purple_status_type_get_name( pst ) ) == 0 )
			break;
	}
	
	purple_account_set_status( pa, st ? purple_status_type_get_id( pst ) : "away",
	                           TRUE, "message", message, NULL );
}

static void purple_add_buddy( struct im_connection *ic, char *who, char *group )
{
	PurpleBuddy *pb;
	
	pb = purple_buddy_new( (PurpleAccount*) ic->proto_data, who, NULL );
	purple_blist_add_buddy( pb, NULL, NULL, NULL );
	purple_account_add_buddy( (PurpleAccount*) ic->proto_data, pb );
}

static void purple_remove_buddy( struct im_connection *ic, char *who, char *group )
{
	PurpleBuddy *pb;
	
	pb = purple_find_buddy( (PurpleAccount*) ic->proto_data, who );
	if( pb != NULL )
	{
		purple_account_remove_buddy( (PurpleAccount*) ic->proto_data, pb, NULL );
		purple_blist_remove_buddy( pb );
	}
}

static void purple_keepalive( struct im_connection *ic )
{
}

static int purple_send_typing( struct im_connection *ic, char *who, int flags )
{
	PurpleTypingState state = PURPLE_NOT_TYPING;
	PurpleConversation *conv;
	
	if( flags & OPT_TYPING )
		state = PURPLE_TYPING;
	else if( flags & OPT_THINKING )
		state = PURPLE_TYPED;
	
	if( ( conv = purple_find_conversation_with_account( PURPLE_CONV_TYPE_IM,
	                                                    who, ic->proto_data ) ) == NULL )
	{
		purple_conv_im_set_typing_state( purple_conversation_get_im_data( conv ), state );
		return 1;
	}
	else
	{
		return 0;
	}
}

static void purple_ui_init();

static PurpleCoreUiOps bee_core_uiops = 
{
	NULL,
	NULL,
	purple_ui_init,
	NULL,
};

static void prplcb_conn_progress( PurpleConnection *gc, const char *text, size_t step, size_t step_count )
{
	struct im_connection *ic = purple_ic_by_gc( gc );
	
	imcb_log( ic, "%s", text );
}

static void prplcb_conn_connected( PurpleConnection *gc )
{
	struct im_connection *ic = purple_ic_by_gc( gc );
	
	imcb_connected( ic );
	
	if( gc->flags & PURPLE_CONNECTION_HTML )
		ic->flags |= OPT_DOES_HTML;
}

static void prplcb_conn_disconnected( PurpleConnection *gc )
{
	struct im_connection *ic = purple_ic_by_gc( gc );
	
	if( ic != NULL )
	{
		imc_logout( ic, TRUE );
	}
}

static void prplcb_conn_notice( PurpleConnection *gc, const char *text )
{
	struct im_connection *ic = purple_ic_by_gc( gc );
	
	if( ic != NULL )
		imcb_log( ic, "%s", text );
}

static void prplcb_conn_report_disconnect_reason( PurpleConnection *gc, PurpleConnectionError reason, const char *text )
{
	struct im_connection *ic = purple_ic_by_gc( gc );
	
	/* PURPLE_CONNECTION_ERROR_NAME_IN_USE means concurrent login,
	   should probably handle that. */
	if( ic != NULL )
		imcb_error( ic, "%s", text );
}

static PurpleConnectionUiOps bee_conn_uiops =
{
	prplcb_conn_progress,
	prplcb_conn_connected,
	prplcb_conn_disconnected,
	prplcb_conn_notice,
	NULL,
	NULL,
	NULL,
	prplcb_conn_report_disconnect_reason,
};

static void prplcb_blist_new( PurpleBlistNode *node )
{
	PurpleBuddy *bud = (PurpleBuddy*) node;
	
	if( node->type == PURPLE_BLIST_BUDDY_NODE )
	{
		struct im_connection *ic = purple_ic_by_pa( bud->account );
		
		if( ic == NULL )
			return;
		
		imcb_add_buddy( ic, bud->name, NULL );
		if( bud->server_alias )
		{
			imcb_rename_buddy( ic, bud->name, bud->server_alias );
			imcb_buddy_nick_hint( ic, bud->name, bud->server_alias );
		}
	}
}

static void prplcb_blist_update( PurpleBuddyList *list, PurpleBlistNode *node )
{
	PurpleBuddy *bud = (PurpleBuddy*) node;
	
	if( node->type == PURPLE_BLIST_BUDDY_NODE )
	{
		struct im_connection *ic = purple_ic_by_pa( bud->account );
		PurpleStatus *as;
		int flags = 0;
		
		if( ic == NULL )
			return;
		
		if( bud->server_alias )
			imcb_rename_buddy( ic, bud->name, bud->server_alias );
		
		flags |= purple_presence_is_online( bud->presence ) ? OPT_LOGGED_IN : 0;
		flags |= purple_presence_is_available( bud->presence ) ? 0 : OPT_AWAY;
		
		as = purple_presence_get_active_status( bud->presence );
		
		imcb_buddy_status( ic, bud->name, flags, purple_status_get_name( as ),
		                   purple_status_get_attr_string( as, "message" ) );
	}
}

static void prplcb_blist_remove( PurpleBuddyList *list, PurpleBlistNode *node )
{
	/*
	PurpleBuddy *bud = (PurpleBuddy*) node;
	
	if( node->type == PURPLE_BLIST_BUDDY_NODE )
	{
		struct im_connection *ic = purple_ic_by_pa( bud->account );
		
		if( ic == NULL )
			return;
		
		imcb_remove_buddy( ic, bud->name, NULL );
	}
	*/
}

static PurpleBlistUiOps bee_blist_uiops =
{
	NULL,
	prplcb_blist_new,
	NULL,
	prplcb_blist_update,
	prplcb_blist_remove,
};

static void prplcb_conv_im( PurpleConversation *conv, const char *who, const char *message, PurpleMessageFlags flags, time_t mtime )
{
	struct im_connection *ic = purple_ic_by_pa( conv->account );
	PurpleBuddy *buddy;
	
	/* ..._SEND means it's an outgoing message, no need to echo those. */
	if( flags & PURPLE_MESSAGE_SEND )
		return;
	
	buddy = purple_find_buddy( conv->account, who );
	if( buddy != NULL )
		who = purple_buddy_get_name( buddy );
	
	imcb_buddy_msg( ic, (char*) who, (char*) message, 0, mtime );
}

static PurpleConversationUiOps bee_conv_uiops = 
{
	NULL,                      /* create_conversation  */
	NULL,                      /* destroy_conversation */
	NULL,                      /* write_chat           */
	prplcb_conv_im,            /* write_im             */
	NULL,                      /* write_conv           */
	NULL,                      /* chat_add_users       */
	NULL,                      /* chat_rename_user     */
	NULL,                      /* chat_remove_users    */
	NULL,                      /* chat_update_user     */
	NULL,                      /* present              */
	NULL,                      /* has_focus            */
	NULL,                      /* custom_smiley_add    */
	NULL,                      /* custom_smiley_write  */
	NULL,                      /* custom_smiley_close  */
	NULL,                      /* send_confirm         */
};

struct prplcb_request_action_data
{
	void *user_data, *bee_data;
	PurpleRequestActionCb yes, no;
	int yes_i, no_i;
};

static void prplcb_request_action_yes( void *data )
{
	struct prplcb_request_action_data *pqad = data;
	
	pqad->yes( pqad->user_data, pqad->yes_i );
	g_free( pqad );
}

static void prplcb_request_action_no( void *data )
{
	struct prplcb_request_action_data *pqad = data;
	
	pqad->no( pqad->user_data, pqad->no_i );
	g_free( pqad );
}

static void *prplcb_request_action( const char *title, const char *primary, const char *secondary,
                                    int default_action, PurpleAccount *account, const char *who,
                                    PurpleConversation *conv, void *user_data, size_t action_count,
                                    va_list actions )
{
	struct prplcb_request_action_data *pqad; 
	int i;
	char *q;
	
	pqad = g_new0( struct prplcb_request_action_data, 1 );
	
	for( i = 0; i < action_count; i ++ )
	{
		char *caption;
		void *fn;
		
		caption = va_arg( actions, char* );
		fn = va_arg( actions, void* );
		
		if( strcmp( caption, "Accept" ) == 0 )
		{
			pqad->yes = fn;
			pqad->yes_i = i;
		}
		else if( strcmp( caption, "Reject" ) == 0 )
		{
			pqad->no = fn;
			pqad->no_i = i;
		}
	}
	
	pqad->user_data = user_data;
	
	q = g_strdup_printf( "Request: %s\n\n%s\n\n%s", title, primary, secondary );
	pqad->bee_data = query_add( local_irc, purple_ic_by_pa( account ), q,
		prplcb_request_action_yes, prplcb_request_action_no, pqad );
	
	g_free( q );
	
	return pqad;
}

static PurpleRequestUiOps bee_request_uiops =
{
	NULL,
	NULL,
	prplcb_request_action,
	NULL,
	NULL,
	NULL,
	NULL,
};

static void prplcb_debug_print( PurpleDebugLevel level, const char *category, const char *arg_s )
{
	fprintf( stderr, "DEBUG %s: %s", category, arg_s );
}

static PurpleDebugUiOps bee_debug_uiops =
{
	prplcb_debug_print,
};

static guint prplcb_ev_timeout_add( guint interval, GSourceFunc func, gpointer udata )
{
	return b_timeout_add( interval, (b_event_handler) func, udata );
}

static guint prplcb_ev_input_add( int fd, PurpleInputCondition cond, PurpleInputFunction func, gpointer udata )
{
	return b_input_add( fd, cond | B_EV_FLAG_FORCE_REPEAT, (b_event_handler) func, udata );
}

static gboolean prplcb_ev_remove( guint id )
{
	b_event_remove( (gint) id );
	return TRUE;
}

static PurpleEventLoopUiOps glib_eventloops = 
{
	prplcb_ev_timeout_add,
	prplcb_ev_remove,
	prplcb_ev_input_add,
	prplcb_ev_remove,
};

static void purple_ui_init()
{
	purple_blist_set_ui_ops( &bee_blist_uiops );
	purple_connections_set_ui_ops( &bee_conn_uiops );
	purple_conversations_set_ui_ops( &bee_conv_uiops );
	purple_request_set_ui_ops( &bee_request_uiops );
	//purple_debug_set_ui_ops( &bee_debug_uiops );
}

void purple_initmodule()
{
	struct prpl funcs;
	GList *prots;
	GString *help;
	
	if( B_EV_IO_READ != PURPLE_INPUT_READ ||
	    B_EV_IO_WRITE != PURPLE_INPUT_WRITE )
	{
		/* FIXME FIXME FIXME FIXME FIXME :-) */
		exit( 1 );
	}
	
	purple_util_set_user_dir("/tmp");
	purple_debug_set_enabled(FALSE);
	purple_core_set_ui_ops(&bee_core_uiops);
	purple_eventloop_set_ui_ops(&glib_eventloops);
	if( !purple_core_init( "BitlBee") )
	{
		/* Initializing the core failed. Terminate. */
		fprintf( stderr, "libpurple initialization failed.\n" );
		abort();
	}
	
	/* This seems like stateful shit we don't want... */
	purple_set_blist(purple_blist_new());
	purple_blist_load();
	
	/* Meh? */
	purple_prefs_load();
	
	memset( &funcs, 0, sizeof( funcs ) );
	funcs.login = purple_login;
	funcs.init = purple_init;
	funcs.logout = purple_logout;
	funcs.buddy_msg = purple_buddy_msg;
	funcs.away_states = purple_away_states;
	funcs.set_away = purple_set_away;
	funcs.add_buddy = purple_add_buddy;
	funcs.remove_buddy = purple_remove_buddy;
	funcs.keepalive = purple_keepalive;
	funcs.send_typing = purple_send_typing;
	funcs.handle_cmp = g_strcasecmp;
	
	help = g_string_new("BitlBee libpurple module supports the following IM protocols:\n");
	
	for( prots = purple_plugins_get_protocols(); prots; prots = prots->next )
	{
		PurplePlugin *prot = prots->data;
		struct prpl *ret;
		
		ret = g_memdup( &funcs, sizeof( funcs ) );
		ret->name = ret->data = prot->info->id;
		if( strncmp( ret->name, "prpl-", 5 ) == 0 )
			ret->name += 5;
		register_protocol( ret );
		
		g_string_append_printf( help, "\n* %s (%s)", ret->name, prot->info->name );
		
		if( g_strcasecmp( prot->info->id, "prpl-aim" ) == 0 )
		{
			ret = g_memdup( &funcs, sizeof( funcs ) );
			ret->name = "oscar";
			ret->data = prot->info->id;
			register_protocol( ret );
		}
	}
	
	help_add_mem( &global.help, "purple", help->str );
	g_string_free( help, TRUE );
}
