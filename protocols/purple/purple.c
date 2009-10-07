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

#include <glib.h>
#include <purple.h>

#include "bitlbee.h"

GSList *purple_connections;

#undef g_io_add_watch
#undef g_io_add_watch_full
#undef g_timeout_add
#undef g_source_remove

/**
 * The following eventloop functions are used in both pidgin and purple-text. If your
 * application uses glib mainloop, you can safely use this verbatim.
 */
#define PURPLE_GLIB_READ_COND  (G_IO_IN | G_IO_HUP | G_IO_ERR)
#define PURPLE_GLIB_WRITE_COND (G_IO_OUT | G_IO_HUP | G_IO_ERR | G_IO_NVAL)

typedef struct _PurpleGLibIOClosure {
	PurpleInputFunction function;
	guint result;
	gpointer data;
} PurpleGLibIOClosure;

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

static void purple_glib_io_destroy(gpointer data)
{
	g_free(data);
}

static gboolean purple_glib_io_invoke(GIOChannel *source, GIOCondition condition, gpointer data)
{
	PurpleGLibIOClosure *closure = data;
	PurpleInputCondition purple_cond = 0;

	if (condition & PURPLE_GLIB_READ_COND)
		purple_cond |= PURPLE_INPUT_READ;
	if (condition & PURPLE_GLIB_WRITE_COND)
		purple_cond |= PURPLE_INPUT_WRITE;

	closure->function(closure->data, g_io_channel_unix_get_fd(source),
			  purple_cond);

	return TRUE;
}

static guint glib_input_add(gint fd, PurpleInputCondition condition, PurpleInputFunction function,
							   gpointer data)
{
	PurpleGLibIOClosure *closure = g_new0(PurpleGLibIOClosure, 1);
	GIOChannel *channel;
	GIOCondition cond = 0;

	closure->function = function;
	closure->data = data;

	if (condition & PURPLE_INPUT_READ)
		cond |= PURPLE_GLIB_READ_COND;
	if (condition & PURPLE_INPUT_WRITE)
		cond |= PURPLE_GLIB_WRITE_COND;

	channel = g_io_channel_unix_new(fd);
	closure->result = g_io_add_watch_full(channel, G_PRIORITY_DEFAULT, cond,
					      purple_glib_io_invoke, closure, purple_glib_io_destroy);

	g_io_channel_unref(channel);
	return closure->result;
}

static PurpleEventLoopUiOps glib_eventloops = 
{
	g_timeout_add,
	g_source_remove,
	glib_input_add,
	g_source_remove,
	NULL,
#if GLIB_CHECK_VERSION(2,14,0)
	g_timeout_add_seconds,
#else
	NULL,
#endif

	/* padding */
	NULL,
	NULL,
	NULL
};

static void purple_init( account_t *acc )
{
	/* TODO: Figure out variables to export via set. */
	
}

static void purple_login( account_t *acc )
{
	struct im_connection *ic = imcb_new( acc );
	PurpleAccount *pa;
	//PurpleSavedStatus *ps;
	GList *i;
	
	/* For now this is needed in the _connected() handlers if using
	   GLib event handling, to make sure we're not handling events
	   on dead connections. */
	purple_connections = g_slist_prepend( purple_connections, ic );
	
	pa = purple_account_new( acc->user, acc->prpl->name );
	purple_account_set_password( pa, acc->pass );
	
	ic->proto_data = pa;
	
	purple_account_set_enabled( pa, "BitlBee", TRUE );
	
	/*
	for( i = ((PurplePluginProtocolInfo *)pa->gc->prpl->info->extra_info)->protocol_options; i; i = i->next )
	{
		PurpleAccountOption *o = i->data;
		
		printf( "%s\n", o->pref_name );
	}
	*/
	
	//ps = purple_savedstatus_new( NULL, PURPLE_STATUS_AVAILABLE );
	//purple_savedstatus_activate_for_account( ps, pa );
}

static void purple_logout( struct im_connection *ic )
{
	purple_connections = g_slist_remove( purple_connections, ic );
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
	return NULL;
}

static void purple_set_away( struct im_connection *ic, char *state_txt, char *message )
{
}

static void purple_add_buddy( struct im_connection *ic, char *who, char *group )
{
}

static void purple_remove_buddy( struct im_connection *ic, char *who, char *group )
{
}

static void purple_keepalive( struct im_connection *ic )
{
}

static int purple_send_typing( struct im_connection *ic, char *who, int typing )
{
	return 1;
}

static void purple_ui_init();

static PurpleCoreUiOps bee_core_uiops = 
{
	NULL,
	NULL,
	purple_ui_init,
	NULL,

	/* padding */
	NULL,
	NULL,
	NULL,
	NULL
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
		imc_logout( ic, TRUE );
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
	struct im_connection *ic = purple_ic_by_pa( bud->account );
	
	if( node->type == PURPLE_BLIST_BUDDY_NODE && ic != NULL )
	{
		imcb_add_buddy( ic, bud->name, NULL );
		if( bud->server_alias )
			imcb_buddy_nick_hint( ic, bud->name, bud->server_alias );
	}
}

static void prplcb_blist_update( PurpleBuddyList *list, PurpleBlistNode *node )
{
	PurpleBuddy *bud = (PurpleBuddy*) node;
	struct im_connection *ic = purple_ic_by_pa( bud->account );
	
	if( node->type == PURPLE_BLIST_BUDDY_NODE && ic != NULL  )
	{
		imcb_buddy_status( ic, bud->name,
		                   purple_presence_is_online( bud->presence ) ? OPT_LOGGED_IN : 0,
		                   NULL, NULL );
	}
}

static void prplcb_blist_remove( PurpleBuddyList *list, PurpleBlistNode *node )
{
	PurpleBuddy *bud = (PurpleBuddy*) node;
	struct im_connection *ic = purple_ic_by_pa( bud->account );
	
	if( node->type == PURPLE_BLIST_BUDDY_NODE && ic != NULL  )
	{
		imcb_remove_buddy( ic, bud->name, NULL );
	}
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
	
	/* ..._SEND means it's an outgoing message, no need to echo those. */
	if( !( flags & PURPLE_MESSAGE_SEND ) )
		imcb_buddy_msg( ic, (char*) who, (char*) message, 0, mtime );
}

static PurpleConversationUiOps bee_conv_uiops = 
{
	NULL,                      /* create_conversation  */
	NULL,                      /* destroy_conversation */
	NULL,                      /* write_chat           */
	prplcb_conv_im,            /* write_im             */
	NULL, //null_write_conv,           /* write_conv           */
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
	NULL,
	NULL,
	NULL,
	NULL
};

static void prplcb_debug_print( PurpleDebugLevel level, const char *category, const char *arg_s )
{
	printf( "DEBUG %s: %s", category, arg_s );
}

static PurpleDebugUiOps bee_debug_uiops =
{
	prplcb_debug_print,
};

static void purple_ui_init()
{
	purple_blist_set_ui_ops( &bee_blist_uiops );
	purple_connections_set_ui_ops( &bee_conn_uiops );
	purple_conversations_set_ui_ops( &bee_conv_uiops );
	//purple_debug_set_ui_ops( &bee_debug_uiops );
}

void purple_initmodule()
{
	GList *prots;
	
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
	
	for( prots = purple_plugins_get_protocols(); prots; prots = prots->next )
	{
		struct prpl *ret = g_new0( struct prpl, 1 );
		PurplePlugin *prot = prots->data;
		
		ret->name = prot->info->id;
		ret->login = purple_login;
		ret->init = purple_init;
		ret->logout = purple_logout;
		ret->buddy_msg = purple_buddy_msg;
		ret->away_states = purple_away_states;
		ret->set_away = purple_set_away;
		ret->add_buddy = purple_add_buddy;
		ret->remove_buddy = purple_remove_buddy;
		ret->keepalive = purple_keepalive;
		ret->send_typing = purple_send_typing;
		ret->handle_cmp = g_strcasecmp;
		
		register_protocol( ret );
	}
}
