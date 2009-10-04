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

static PurpleCoreUiOps bee_core_uiops = 
{
	NULL,
	NULL,
	NULL, //null_ui_init,
	NULL,

	/* padding */
	NULL,
	NULL,
	NULL,
	NULL
};

static PurpleConversationUiOps bee_conv_uiops = 
{
	NULL,                      /* create_conversation  */
	NULL,                      /* destroy_conversation */
	NULL,                      /* write_chat           */
	NULL,                      /* write_im             */
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

static void purple_init( account_t *acc )
{
	set_t *s;
	char str[16];
	
}

static void purple_login( account_t *acc )
{
	struct im_connection *ic = imcb_new( acc );
	struct ns_srv_reply *srv = NULL;
	char *connect_to, *s;
	int i;
	
	/* For now this is needed in the _connected() handlers if using
	   GLib event handling, to make sure we're not handling events
	   on dead connections. */
	purple_connections = g_slist_prepend( purple_connections, ic );
	
}

static void purple_logout( struct im_connection *ic )
{
	purple_connections = g_slist_remove( purple_connections, ic );
}

static int purple_buddy_msg( struct im_connection *ic, char *who, char *message, int flags )
{
}

static GList *purple_away_states( struct im_connection *ic )
{
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
