  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2010 Wilmer van der Gaast and others                *
  \********************************************************************/

/*
 * nogaim
 *
 * Gaim without gaim - for BitlBee
 *
 * This file contains functions called by the Gaim IM-modules. It's written
 * from scratch for BitlBee and doesn't contain any code from Gaim anymore
 * (except for the function names).
 */

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
#include <ctype.h>

#include "nogaim.h"
#include "chat.h"

static int remove_chat_buddy_silent( struct groupchat *b, const char *handle );
static char *format_timestamp( irc_t *irc, time_t msg_ts );

GSList *connections;

#ifdef WITH_PLUGINS
gboolean load_plugin(char *path)
{
	void (*init_function) (void);
	
	GModule *mod = g_module_open(path, G_MODULE_BIND_LAZY);

	if(!mod) {
		log_message(LOGLVL_ERROR, "Can't find `%s', not loading (%s)\n", path, g_module_error());
		return FALSE;
	}

	if(!g_module_symbol(mod,"init_plugin",(gpointer *) &init_function)) {
		log_message(LOGLVL_WARNING, "Can't find function `init_plugin' in `%s'\n", path);
		return FALSE;
	}

	init_function();

	return TRUE;
}

void load_plugins(void)
{
	GDir *dir;
	GError *error = NULL;

	dir = g_dir_open(global.conf->plugindir, 0, &error);

	if (dir) {
		const gchar *entry;
		char *path;

		while ((entry = g_dir_read_name(dir))) {
			path = g_build_filename(global.conf->plugindir, entry, NULL);
			if(!path) {
				log_message(LOGLVL_WARNING, "Can't build path for %s\n", entry);
				continue;
			}

			load_plugin(path);

			g_free(path);
		}

		g_dir_close(dir);
	}
}
#endif

/* nogaim.c */

GList *protocols = NULL;
  
void register_protocol (struct prpl *p)
{
	int i;
	gboolean refused = global.conf->protocols != NULL;
 
	for (i = 0; global.conf->protocols && global.conf->protocols[i]; i++)
 	{
 		if (g_strcasecmp(p->name, global.conf->protocols[i]) == 0)
			refused = FALSE;
 	}

	if (refused)
		log_message(LOGLVL_WARNING, "Protocol %s disabled\n", p->name);
	else
		protocols = g_list_append(protocols, p);
}

struct prpl *find_protocol(const char *name)
{
	GList *gl;
	for (gl = protocols; gl; gl = gl->next) 
 	{
 		struct prpl *proto = gl->data;
 		if(!g_strcasecmp(proto->name, name)) 
			return proto;
 	}
 	return NULL;
}

/* nogaim.c */
void nogaim_init()
{
	extern void msn_initmodule();
	extern void oscar_initmodule();
	extern void byahoo_initmodule();
	extern void jabber_initmodule();
	extern void twitter_initmodule();

#ifdef WITH_MSN
	msn_initmodule();
#endif

#ifdef WITH_OSCAR
	oscar_initmodule();
#endif
	
#ifdef WITH_YAHOO
	byahoo_initmodule();
#endif
	
#ifdef WITH_JABBER
	jabber_initmodule();
#endif

#ifdef WITH_TWITTER
	twitter_initmodule();
#endif

#ifdef WITH_PLUGINS
	load_plugins();
#endif
}

GSList *get_connections() { return connections; }

/* multi.c */

struct im_connection *imcb_new( account_t *acc )
{
	struct im_connection *ic;
	
	ic = g_new0( struct im_connection, 1 );
	
	ic->irc = acc->irc;
	ic->acc = acc;
	acc->ic = ic;
	
	connections = g_slist_append( connections, ic );
	
	return( ic );
}

void imc_free( struct im_connection *ic )
{
	account_t *a;
	
	/* Destroy the pointer to this connection from the account list */
	for( a = ic->irc->accounts; a; a = a->next )
		if( a->ic == ic )
		{
			a->ic = NULL;
			break;
		}
	
	connections = g_slist_remove( connections, ic );
	g_free( ic );
}

static void serv_got_crap( struct im_connection *ic, char *format, ... )
{
	va_list params;
	char *text;
	account_t *a;
	
	va_start( params, format );
	text = g_strdup_vprintf( format, params );
	va_end( params );

	if( ( g_strcasecmp( set_getstr( &ic->irc->set, "strip_html" ), "always" ) == 0 ) ||
	    ( ( ic->flags & OPT_DOES_HTML ) && set_getbool( &ic->irc->set, "strip_html" ) ) )
		strip_html( text );
	
	/* Try to find a different connection on the same protocol. */
	for( a = ic->irc->accounts; a; a = a->next )
		if( a->prpl == ic->acc->prpl && a->ic != ic )
			break;
	
	/* If we found one, include the screenname in the message. */
	if( a )
		irc_usermsg( ic->irc, "%s(%s) - %s", ic->acc->prpl->name, ic->acc->user, text );
	else
		irc_usermsg( ic->irc, "%s - %s", ic->acc->prpl->name, text );
	
	g_free( text );
}

void imcb_log( struct im_connection *ic, char *format, ... )
{
	va_list params;
	char *text;
	
	va_start( params, format );
	text = g_strdup_vprintf( format, params );
	va_end( params );
	
	if( ic->flags & OPT_LOGGED_IN )
		serv_got_crap( ic, "%s", text );
	else
		serv_got_crap( ic, "Logging in: %s", text );
	
	g_free( text );
}

void imcb_error( struct im_connection *ic, char *format, ... )
{
	va_list params;
	char *text;
	
	va_start( params, format );
	text = g_strdup_vprintf( format, params );
	va_end( params );
	
	if( ic->flags & OPT_LOGGED_IN )
		serv_got_crap( ic, "Error: %s", text );
	else
		serv_got_crap( ic, "Couldn't log in: %s", text );
	
	g_free( text );
}

static gboolean send_keepalive( gpointer d, gint fd, b_input_condition cond )
{
	struct im_connection *ic = d;
	
	if( ic->acc->prpl->keepalive )
		ic->acc->prpl->keepalive( ic );
	
	return TRUE;
}

void imcb_connected( struct im_connection *ic )
{
	irc_t *irc = ic->irc;
	struct chat *c;
	user_t *u;
	
	/* MSN servers sometimes redirect you to a different server and do
	   the whole login sequence again, so these "late" calls to this
	   function should be handled correctly. (IOW, ignored) */
	if( ic->flags & OPT_LOGGED_IN )
		return;
	
	u = user_find( ic->irc, ic->irc->nick );
	
	imcb_log( ic, "Logged in" );
	
	ic->keepalive = b_timeout_add( 60000, send_keepalive, ic );
	ic->flags |= OPT_LOGGED_IN;
	
	/* Necessary to send initial presence status, even if we're not away. */
	imc_away_send_update( ic );
	
	/* Apparently we're connected successfully, so reset the
	   exponential backoff timer. */
	ic->acc->auto_reconnect_delay = 0;
	
	for( c = irc->chatrooms; c; c = c->next )
	{
		if( c->acc != ic->acc )
			continue;
		
		if( set_getbool( &c->set, "auto_join" ) )
			chat_join( irc, c, NULL );
	}
}

gboolean auto_reconnect( gpointer data, gint fd, b_input_condition cond )
{
	account_t *a = data;
	
	a->reconnect = 0;
	account_on( a->irc, a );
	
	return( FALSE );	/* Only have to run the timeout once */
}

void cancel_auto_reconnect( account_t *a )
{
	b_event_remove( a->reconnect );
	a->reconnect = 0;
}

void imc_logout( struct im_connection *ic, int allow_reconnect )
{
	irc_t *irc = ic->irc;
	user_t *t, *u;
	account_t *a;
	int delay;
	
	/* Nested calls might happen sometimes, this is probably the best
	   place to catch them. */
	if( ic->flags & OPT_LOGGING_OUT )
		return;
	else
		ic->flags |= OPT_LOGGING_OUT;
	
	imcb_log( ic, "Signing off.." );
	
	b_event_remove( ic->keepalive );
	ic->keepalive = 0;
	ic->acc->prpl->logout( ic );
	b_event_remove( ic->inpa );
	
	g_free( ic->away );
	ic->away = NULL;
	
	u = irc->users;
	while( u )
	{
		if( u->ic == ic )
		{
			t = u->next;
			user_del( irc, u->nick );
			u = t;
		}
		else
			u = u->next;
	}
	
	query_del_by_conn( ic->irc, ic );
	
	for( a = irc->accounts; a; a = a->next )
		if( a->ic == ic )
			break;
	
	if( !a )
	{
		/* Uhm... This is very sick. */
	}
	else if( allow_reconnect && set_getbool( &irc->set, "auto_reconnect" ) &&
	         set_getbool( &a->set, "auto_reconnect" ) &&
	         ( delay = account_reconnect_delay( a ) ) > 0 )
	{
		imcb_log( ic, "Reconnecting in %d seconds..", delay );
		a->reconnect = b_timeout_add( delay * 1000, auto_reconnect, a );
	}
	
	imc_free( ic );
}


/* dialogs.c */

void imcb_ask( struct im_connection *ic, char *msg, void *data,
               query_callback doit, query_callback dont )
{
	query_add( ic->irc, ic, msg, doit, dont, data );
}


/* list.c */

void imcb_add_buddy( struct im_connection *ic, const char *handle, const char *group )
{
	user_t *u;
	char nick[MAX_NICK_LENGTH+1], *s;
	irc_t *irc = ic->irc;
	
	if( user_findhandle( ic, handle ) )
	{
		if( set_getbool( &irc->set, "debug" ) )
			imcb_log( ic, "User already exists, ignoring add request: %s", handle );
		
		return;
		
		/* Buddy seems to exist already. Let's ignore this request then...
		   Eventually subsequent calls to this function *should* be possible
		   when a buddy is in multiple groups. But for now BitlBee doesn't
		   even support groups so let's silently ignore this for now. */
	}
	
	memset( nick, 0, MAX_NICK_LENGTH + 1 );
	strcpy( nick, nick_get( ic->acc, handle ) );
	
	u = user_add( ic->irc, nick );
	
//	if( !realname || !*realname ) realname = nick;
//	u->realname = g_strdup( realname );
	
	if( ( s = strchr( handle, '@' ) ) )
	{
		u->host = g_strdup( s + 1 );
		u->user = g_strndup( handle, s - handle );
	}
	else if( ic->acc->server )
	{
		u->host = g_strdup( ic->acc->server );
		u->user = g_strdup( handle );
		
		/* s/ /_/ ... important for AOL screennames */
		for( s = u->user; *s; s ++ )
			if( *s == ' ' )
				*s = '_';
	}
	else
	{
		u->host = g_strdup( ic->acc->prpl->name );
		u->user = g_strdup( handle );
	}
	
	u->ic = ic;
	u->handle = g_strdup( handle );
	if( group ) u->group = g_strdup( group );
	u->send_handler = buddy_send_handler;
	u->last_typing_notice = 0;
}

struct buddy *imcb_find_buddy( struct im_connection *ic, char *handle )
{
	static struct buddy b[1];
	user_t *u;
	
	u = user_findhandle( ic, handle );
	
	if( !u )
		return( NULL );
	
	memset( b, 0, sizeof( b ) );
	strncpy( b->name, handle, 80 );
	strncpy( b->show, u->realname, BUDDY_ALIAS_MAXLEN );
	b->present = u->online;
	b->ic = u->ic;
	
	return( b );
}

void imcb_rename_buddy( struct im_connection *ic, const char *handle, const char *realname )
{
	user_t *u = user_findhandle( ic, handle );
	char *set;
	
	if( !u || !realname ) return;
	
	if( g_strcasecmp( u->realname, realname ) != 0 )
	{
		if( u->realname != u->nick ) g_free( u->realname );
		
		u->realname = g_strdup( realname );
		
		if( ( ic->flags & OPT_LOGGED_IN ) && set_getbool( &ic->irc->set, "display_namechanges" ) )
			imcb_log( ic, "User `%s' changed name to `%s'", u->nick, u->realname );
	}
	
	set = set_getstr( &ic->acc->set, "nick_source" );
	if( strcmp( set, "handle" ) != 0 )
	{
		char *name = g_strdup( realname );
		
		if( strcmp( set, "first_name" ) == 0 )
		{
			int i;
			for( i = 0; name[i] && !isspace( name[i] ); i ++ ) {}
			name[i] = '\0';
		}
		
		imcb_buddy_nick_hint( ic, handle, name );
		
		g_free( name );
	}
}

void imcb_remove_buddy( struct im_connection *ic, const char *handle, char *group )
{
	user_t *u;
	
	if( ( u = user_findhandle( ic, handle ) ) )
		user_del( ic->irc, u->nick );
}

/* Mainly meant for ICQ (and now also for Jabber conferences) to allow IM
   modules to suggest a nickname for a handle. */
void imcb_buddy_nick_hint( struct im_connection *ic, const char *handle, const char *nick )
{
	user_t *u = user_findhandle( ic, handle );
	char newnick[MAX_NICK_LENGTH+1], *orig_nick;
	
	if( u && !u->online && !nick_saved( ic->acc, handle ) )
	{
		/* Only do this if the person isn't online yet (which should
		   be the case if we just added it) and if the user hasn't
		   assigned a nickname to this buddy already. */
		
		strncpy( newnick, nick, MAX_NICK_LENGTH );
		newnick[MAX_NICK_LENGTH] = 0;
		
		/* Some processing to make sure this string is a valid IRC nickname. */
		nick_strip( newnick );
		if( set_getbool( &ic->irc->set, "lcnicks" ) )
			nick_lc( newnick );
		
		if( strcmp( u->nick, newnick ) != 0 )
		{
			/* Only do this if newnick is different from the current one.
			   If rejoining a channel, maybe we got this nick already
			   (and dedupe would only add an underscore. */
			nick_dedupe( ic->acc, handle, newnick );
			
			/* u->nick will be freed halfway the process, so it can't be
			   passed as an argument. */
			orig_nick = g_strdup( u->nick );
			user_rename( ic->irc, orig_nick, newnick );
			g_free( orig_nick );
		}
	}
}


struct imcb_ask_cb_data
{
	struct im_connection *ic;
	char *handle;
};

static void imcb_ask_auth_cb_no( void *data )
{
	struct imcb_ask_cb_data *cbd = data;
	
	cbd->ic->acc->prpl->auth_deny( cbd->ic, cbd->handle );
	
	g_free( cbd->handle );
	g_free( cbd );
}

static void imcb_ask_auth_cb_yes( void *data )
{
	struct imcb_ask_cb_data *cbd = data;
	
	cbd->ic->acc->prpl->auth_allow( cbd->ic, cbd->handle );
	
	g_free( cbd->handle );
	g_free( cbd );
}

void imcb_ask_auth( struct im_connection *ic, const char *handle, const char *realname )
{
	struct imcb_ask_cb_data *data = g_new0( struct imcb_ask_cb_data, 1 );
	char *s, *realname_ = NULL;
	
	if( realname != NULL )
		realname_ = g_strdup_printf( " (%s)", realname );
	
	s = g_strdup_printf( "The user %s%s wants to add you to his/her buddy list.",
	                     handle, realname_ ?: "" );
	
	g_free( realname_ );
	
	data->ic = ic;
	data->handle = g_strdup( handle );
	query_add( ic->irc, ic, s, imcb_ask_auth_cb_yes, imcb_ask_auth_cb_no, data );
}


static void imcb_ask_add_cb_no( void *data )
{
	g_free( ((struct imcb_ask_cb_data*)data)->handle );
	g_free( data );
}

static void imcb_ask_add_cb_yes( void *data )
{
	struct imcb_ask_cb_data *cbd = data;
	
	cbd->ic->acc->prpl->add_buddy( cbd->ic, cbd->handle, NULL );
	
	return imcb_ask_add_cb_no( data );
}

void imcb_ask_add( struct im_connection *ic, const char *handle, const char *realname )
{
	struct imcb_ask_cb_data *data = g_new0( struct imcb_ask_cb_data, 1 );
	char *s;
	
	/* TODO: Make a setting for this! */
	if( user_findhandle( ic, handle ) != NULL )
		return;
	
	s = g_strdup_printf( "The user %s is not in your buddy list yet. Do you want to add him/her now?", handle );
	
	data->ic = ic;
	data->handle = g_strdup( handle );
	query_add( ic->irc, ic, s, imcb_ask_add_cb_yes, imcb_ask_add_cb_no, data );
}


/* server.c */                    

void imcb_buddy_status( struct im_connection *ic, const char *handle, int flags, const char *state, const char *message )
{
	user_t *u;
	int oa, oo;
	
	u = user_findhandle( ic, (char*) handle );
	
	if( !u )
	{
		if( g_strcasecmp( set_getstr( &ic->irc->set, "handle_unknown" ), "add" ) == 0 )
		{
			imcb_add_buddy( ic, (char*) handle, NULL );
			u = user_findhandle( ic, (char*) handle );
		}
		else
		{
			if( set_getbool( &ic->irc->set, "debug" ) || g_strcasecmp( set_getstr( &ic->irc->set, "handle_unknown" ), "ignore" ) != 0 )
			{
				imcb_log( ic, "imcb_buddy_status() for unknown handle %s:", handle );
				imcb_log( ic, "flags = %d, state = %s, message = %s", flags,
				          state ? state : "NULL", message ? message : "NULL" );
			}
			
			return;
		}
	}
	
	oa = u->away != NULL;
	oo = u->online;
	
	g_free( u->away );
	g_free( u->status_msg );
	u->away = u->status_msg = NULL;
	
	if( set_getbool( &ic->irc->set, "show_offline" ) && !u->online )
	{
		/* always set users as online */
		irc_spawn( ic->irc, u );
		u->online = 1;
		if( !( flags & OPT_LOGGED_IN ) )
		{
			/* set away message if user isn't really online */
			u->away = g_strdup( "User is offline" );
		}
	}
	else if( ( flags & OPT_LOGGED_IN ) && !u->online )
	{
		irc_spawn( ic->irc, u );
		u->online = 1;
	}
	else if( !( flags & OPT_LOGGED_IN ) && u->online )
	{
		struct groupchat *c;
		
		if( set_getbool( &ic->irc->set, "show_offline" ) )
		{
			/* keep offline users in channel and set away message to "offline" */
			u->away = g_strdup( "User is offline" );

			/* Keep showing him/her in the control channel but not in groupchats. */
			for( c = ic->groupchats; c; c = c->next )
			{
				if( remove_chat_buddy_silent( c, handle ) && c->joined )
					irc_part( c->ic->irc, u, c->channel );
			}
		}
		else
		{
			/* kill offline users */
			irc_kill( ic->irc, u );
			u->online = 0;

			/* Remove him/her from the groupchats to prevent PART messages after he/she QUIT already */
			for( c = ic->groupchats; c; c = c->next )
				remove_chat_buddy_silent( c, handle );
		}
	}

	if( flags & OPT_AWAY )
	{
		if( state && message )
		{
			u->away = g_strdup_printf( "%s (%s)", state, message );
		}
		else if( state )
		{
			u->away = g_strdup( state );
		}
		else if( message )
		{
			u->away = g_strdup( message );
		}
		else
		{
			u->away = g_strdup( "Away" );
		}
	}
	else
	{
		u->status_msg = g_strdup( message );
	}
	
	/* LISPy... */
	if( ( u->online ) &&						/* Don't touch offline people */
	    ( ( u->online != oo ) ||					/* Do joining people */
	      ( ( u->online == oo ) && ( oa == !u->away ) ) ) )		/* Do people changing state */
	{
		char *from;
		
		if( set_getbool( &ic->irc->set, "simulate_netsplit" ) )
		{
			from = g_strdup( ic->irc->myhost );
		}
		else
		{
			from = g_strdup_printf( "%s!%s@%s", ic->irc->mynick, ic->irc->mynick,
			                                    ic->irc->myhost );
		}

		if(!strcmp(set_getstr(&ic->irc->set, "voice_buddies"), "online")) {
			irc_write( ic->irc, ":%s MODE %s +v %s", from, ic->irc->channel, u->nick );
		}
		if(!strcmp(set_getstr(&ic->irc->set, "halfop_buddies"), "online")) {
			irc_write( ic->irc, ":%s MODE %s +h %s", from, ic->irc->channel, u->nick );
		}
		if(!strcmp(set_getstr(&ic->irc->set, "op_buddies"), "online")) {
			irc_write( ic->irc, ":%s MODE %s +o %s", from, ic->irc->channel, u->nick );
		}

		if(!strcmp(set_getstr(&ic->irc->set, "voice_buddies"), "notaway")) {
			irc_write( ic->irc, ":%s MODE %s %cv %s", from, ic->irc->channel,
		 	                                         u->away?'-':'+', u->nick );
		}
		if(!strcmp(set_getstr(&ic->irc->set, "halfop_buddies"), "notaway")) {
			irc_write( ic->irc, ":%s MODE %s %ch %s", from, ic->irc->channel,
		 	                                         u->away?'-':'+', u->nick );
		}
		if(!strcmp(set_getstr(&ic->irc->set, "op_buddies"), "notaway")) {
			irc_write( ic->irc, ":%s MODE %s %co %s", from, ic->irc->channel,
		 	                                         u->away?'-':'+', u->nick );
		}

		g_free( from );
	}
}

void imcb_buddy_msg( struct im_connection *ic, const char *handle, char *msg, uint32_t flags, time_t sent_at )
{
	irc_t *irc = ic->irc;
	char *wrapped, *ts = NULL;
	user_t *u;

	/* pass the message through OTR */
	msg = otr_handle_message(ic, handle, msg);
	if(!msg) {
		/* this was an internal OTR protocol message */
		return;
	}

	u = user_findhandle( ic, handle );
	if( !u )
	{
		char *h = set_getstr( &irc->set, "handle_unknown" );
		
		if( g_strcasecmp( h, "ignore" ) == 0 )
		{
			if( set_getbool( &irc->set, "debug" ) )
				imcb_log( ic, "Ignoring message from unknown handle %s", handle );
			
			g_free(msg);
			return;
		}
		else if( g_strncasecmp( h, "add", 3 ) == 0 )
		{
			int private = set_getbool( &irc->set, "private" );
			
			if( h[3] )
			{
				if( g_strcasecmp( h + 3, "_private" ) == 0 )
					private = 1;
				else if( g_strcasecmp( h + 3, "_channel" ) == 0 )
					private = 0;
			}
			
			imcb_add_buddy( ic, handle, NULL );
			u = user_findhandle( ic, handle );
			u->is_private = private;
		}
		else
		{
			imcb_log( ic, "Message from unknown handle %s:", handle );
			u = user_find( irc, irc->mynick );
		}
	}
	
	if( ( g_strcasecmp( set_getstr( &ic->irc->set, "strip_html" ), "always" ) == 0 ) ||
	    ( ( ic->flags & OPT_DOES_HTML ) && set_getbool( &ic->irc->set, "strip_html" ) ) )
		strip_html( msg );
	
	if( set_getbool( &ic->irc->set, "display_timestamps" ) &&
	    ( ts = format_timestamp( irc, sent_at ) ) )
	{
		char *new = g_strconcat( ts, msg, NULL );
		g_free( msg );
		msg = new;
	}
	
	wrapped = word_wrap( msg, 425 );
	irc_msgfrom( irc, u->nick, wrapped );
	g_free( wrapped );
	g_free( msg );
	g_free( ts );
}

void imcb_buddy_typing( struct im_connection *ic, char *handle, uint32_t flags )
{
	user_t *u;
	
	if( !set_getbool( &ic->irc->set, "typing_notice" ) )
		return;
	
	if( ( u = user_findhandle( ic, handle ) ) )
	{
		char buf[256]; 
		
		g_snprintf( buf, 256, "\1TYPING %d\1", ( flags >> 8 ) & 3 );
		irc_privmsg( ic->irc, u, "PRIVMSG", ic->irc->nick, NULL, buf );
	}
}

struct groupchat *imcb_chat_new( struct im_connection *ic, const char *handle )
{
	struct groupchat *c;
	
	/* This one just creates the conversation structure, user won't see anything yet */
	
	if( ic->groupchats )
	{
		for( c = ic->groupchats; c->next; c = c->next );
		c = c->next = g_new0( struct groupchat, 1 );
	}
	else
		ic->groupchats = c = g_new0( struct groupchat, 1 );
	
	c->ic = ic;
	c->title = g_strdup( handle );
	c->channel = g_strdup_printf( "&chat_%03d", ic->irc->c_id++ );
	c->topic = g_strdup_printf( "BitlBee groupchat: \"%s\". Please keep in mind that root-commands won't work here. Have fun!", c->title );
	
	if( set_getbool( &ic->irc->set, "debug" ) )
		imcb_log( ic, "Creating new conversation: (id=%p,handle=%s)", c, handle );
	
	return c;
}

void imcb_chat_name_hint( struct groupchat *c, const char *name )
{
	if( !c->joined )
	{
		struct im_connection *ic = c->ic;
		char stripped[MAX_NICK_LENGTH+1], *full_name;
		
		strncpy( stripped, name, MAX_NICK_LENGTH );
		stripped[MAX_NICK_LENGTH] = '\0';
		nick_strip( stripped );
		if( set_getbool( &ic->irc->set, "lcnicks" ) )
			nick_lc( stripped );
		
		full_name = g_strdup_printf( "&%s", stripped );
		
		if( stripped[0] &&
		    nick_cmp( stripped, ic->irc->channel + 1 ) != 0 &&
		    irc_chat_by_channel( ic->irc, full_name ) == NULL )
		{
			g_free( c->channel );
			c->channel = full_name;
		}
		else
		{
			g_free( full_name );
		}
	}
}

void imcb_chat_free( struct groupchat *c )
{
	struct im_connection *ic = c->ic;
	struct groupchat *l;
	GList *ir;
	
	if( set_getbool( &ic->irc->set, "debug" ) )
		imcb_log( ic, "You were removed from conversation %p", c );
	
	if( c )
	{
		if( c->joined )
		{
			user_t *u, *r;
			
			r = user_find( ic->irc, ic->irc->mynick );
			irc_privmsg( ic->irc, r, "PRIVMSG", c->channel, "", "Cleaning up channel, bye!" );
			
			u = user_find( ic->irc, ic->irc->nick );
			irc_kick( ic->irc, u, c->channel, r );
			/* irc_part( ic->irc, u, c->channel ); */
		}
		
		/* Find the previous chat in the linked list. */
		for( l = ic->groupchats; l && l->next != c; l = l->next );
		
		if( l )
			l->next = c->next;
		else
			ic->groupchats = c->next;
		
		for( ir = c->in_room; ir; ir = ir->next )
			g_free( ir->data );
		g_list_free( c->in_room );
		g_free( c->channel );
		g_free( c->title );
		g_free( c->topic );
		g_free( c );
	}
}

void imcb_chat_msg( struct groupchat *c, const char *who, char *msg, uint32_t flags, time_t sent_at )
{
	struct im_connection *ic = c->ic;
	char *wrapped;
	user_t *u;
	
	/* Gaim sends own messages through this too. IRC doesn't want this, so kill them */
	if( g_strcasecmp( who, ic->acc->user ) == 0 )
		return;
	
	u = user_findhandle( ic, who );
	
	if( ( g_strcasecmp( set_getstr( &ic->irc->set, "strip_html" ), "always" ) == 0 ) ||
	    ( ( ic->flags & OPT_DOES_HTML ) && set_getbool( &ic->irc->set, "strip_html" ) ) )
		strip_html( msg );
	
	wrapped = word_wrap( msg, 425 );
	if( c && u )
	{
		char *ts = NULL;
		if( set_getbool( &ic->irc->set, "display_timestamps" ) )
			ts = format_timestamp( ic->irc, sent_at );
		irc_privmsg( ic->irc, u, "PRIVMSG", c->channel, ts ? : "", wrapped );
		g_free( ts );
	}
	else
	{
		imcb_log( ic, "Message from/to conversation %s@%p (unknown conv/user): %s", who, c, wrapped );
	}
	g_free( wrapped );
}

void imcb_chat_log( struct groupchat *c, char *format, ... )
{
	irc_t *irc = c->ic->irc;
	va_list params;
	char *text;
	user_t *u;
	
	va_start( params, format );
	text = g_strdup_vprintf( format, params );
	va_end( params );
	
	u = user_find( irc, irc->mynick );
	
	irc_privmsg( irc, u, "PRIVMSG", c->channel, "System message: ", text );
	
	g_free( text );
}

void imcb_chat_topic( struct groupchat *c, char *who, char *topic, time_t set_at )
{
	struct im_connection *ic = c->ic;
	user_t *u = NULL;
	
	if( who == NULL)
		u = user_find( ic->irc, ic->irc->mynick );
	else if( g_strcasecmp( who, ic->acc->user ) == 0 )
		u = user_find( ic->irc, ic->irc->nick );
	else
		u = user_findhandle( ic, who );
	
	if( ( g_strcasecmp( set_getstr( &ic->irc->set, "strip_html" ), "always" ) == 0 ) ||
	    ( ( ic->flags & OPT_DOES_HTML ) && set_getbool( &ic->irc->set, "strip_html" ) ) )
		strip_html( topic );
	
	g_free( c->topic );
	c->topic = g_strdup( topic );
	
	if( c->joined && u )
		irc_write( ic->irc, ":%s!%s@%s TOPIC %s :%s", u->nick, u->user, u->host, c->channel, topic );
}


/* buddy_chat.c */

void imcb_chat_add_buddy( struct groupchat *b, const char *handle )
{
	user_t *u = user_findhandle( b->ic, handle );
	int me = 0;
	
	if( set_getbool( &b->ic->irc->set, "debug" ) )
		imcb_log( b->ic, "User %s added to conversation %p", handle, b );
	
	/* It might be yourself! */
	if( b->ic->acc->prpl->handle_cmp( handle, b->ic->acc->user ) == 0 )
	{
		u = user_find( b->ic->irc, b->ic->irc->nick );
		if( !b->joined )
			irc_join( b->ic->irc, u, b->channel );
		b->joined = me = 1;
	}
	
	/* Most protocols allow people to join, even when they're not in
	   your contact list. Try to handle that here */
	if( !u )
	{
		imcb_add_buddy( b->ic, handle, NULL );
		u = user_findhandle( b->ic, handle );
	}
	
	/* Add the handle to the room userlist, if it's not 'me' */
	if( !me )
	{
		if( b->joined )
			irc_join( b->ic->irc, u, b->channel );
		b->in_room = g_list_append( b->in_room, g_strdup( handle ) );
	}
}

/* This function is one BIG hack... :-( EREWRITE */
void imcb_chat_remove_buddy( struct groupchat *b, const char *handle, const char *reason )
{
	user_t *u;
	int me = 0;
	
	if( set_getbool( &b->ic->irc->set, "debug" ) )
		imcb_log( b->ic, "User %s removed from conversation %p (%s)", handle, b, reason ? reason : "" );
	
	/* It might be yourself! */
	if( g_strcasecmp( handle, b->ic->acc->user ) == 0 )
	{
		if( b->joined == 0 )
			return;
		
		u = user_find( b->ic->irc, b->ic->irc->nick );
		b->joined = 0;
		me = 1;
	}
	else
	{
		u = user_findhandle( b->ic, handle );
	}
	
	if( me || ( remove_chat_buddy_silent( b, handle ) && b->joined && u ) )
		irc_part( b->ic->irc, u, b->channel );
}

static int remove_chat_buddy_silent( struct groupchat *b, const char *handle )
{
	GList *i;
	
	/* Find the handle in the room userlist and shoot it */
	i = b->in_room;
	while( i )
	{
		if( g_strcasecmp( handle, i->data ) == 0 )
		{
			g_free( i->data );
			b->in_room = g_list_remove( b->in_room, i->data );
			return( 1 );
		}
		
		i = i->next;
	}
	
	return( 0 );
}


/* Misc. BitlBee stuff which shouldn't really be here */

char *set_eval_timezone( set_t *set, char *value )
{
	char *s;
	
	if( strcmp( value, "local" ) == 0 ||
	    strcmp( value, "gmt" ) == 0 || strcmp( value, "utc" ) == 0 )
		return value;
	
	/* Otherwise: +/- at the beginning optional, then one or more numbers,
	   possibly followed by a colon and more numbers. Don't bother bound-
	   checking them since users are free to shoot themselves in the foot. */
	s = value;
	if( *s == '+' || *s == '-' )
		s ++;
	
	/* \d+ */
	if( !isdigit( *s ) )
		return SET_INVALID;
	while( *s && isdigit( *s ) ) s ++;
	
	/* EOS? */
	if( *s == '\0' )
		return value;
	
	/* Otherwise, colon */
	if( *s != ':' )
		return SET_INVALID;
	s ++;
	
	/* \d+ */
	if( !isdigit( *s ) )
		return SET_INVALID;
	while( *s && isdigit( *s ) ) s ++;
	
	/* EOS */
	return *s == '\0' ? value : SET_INVALID;
}

static char *format_timestamp( irc_t *irc, time_t msg_ts )
{
	time_t now_ts = time( NULL );
	struct tm now, msg;
	char *set;
	
	/* If the timestamp is <= 0 or less than a minute ago, discard it as
	   it doesn't seem to add to much useful info and/or might be noise. */
	if( msg_ts <= 0 || msg_ts > now_ts - 60 )
		return NULL;
	
	set = set_getstr( &irc->set, "timezone" );
	if( strcmp( set, "local" ) == 0 )
	{
		localtime_r( &now_ts, &now );
		localtime_r( &msg_ts, &msg );
	}
	else
	{
		int hr, min = 0, sign = 60;
		
		if( set[0] == '-' )
		{
			sign *= -1;
			set ++;
		}
		else if( set[0] == '+' )
		{
			set ++;
		}
		
		if( sscanf( set, "%d:%d", &hr, &min ) >= 1 )
		{
			msg_ts += sign * ( hr * 60 + min );
			now_ts += sign * ( hr * 60 + min );
		}
		
		gmtime_r( &now_ts, &now );
		gmtime_r( &msg_ts, &msg );
	}
	
	if( msg.tm_year == now.tm_year && msg.tm_yday == now.tm_yday )
		return g_strdup_printf( "\x02[\x02\x02\x02%02d:%02d:%02d\x02]\x02 ",
		                        msg.tm_hour, msg.tm_min, msg.tm_sec );
	else
		return g_strdup_printf( "\x02[\x02\x02\x02%04d-%02d-%02d "
		                        "%02d:%02d:%02d\x02]\x02 ",
		                        msg.tm_year + 1900, msg.tm_mon + 1, msg.tm_mday,
		                        msg.tm_hour, msg.tm_min, msg.tm_sec );
}

/* The plan is to not allow straight calls to prpl functions anymore, but do
   them all from some wrappers. We'll start to define some down here: */

int imc_buddy_msg( struct im_connection *ic, char *handle, char *msg, int flags )
{
	char *buf = NULL;
	int st;
	
	if( ( ic->flags & OPT_DOES_HTML ) && ( g_strncasecmp( msg, "<html>", 6 ) != 0 ) )
	{
		buf = escape_html( msg );
		msg = buf;
	}

	/* if compiled without otr support, this just calls the prpl buddy_msg */
	st = otr_send_message(ic, handle, msg, flags);
	
	g_free(buf);
	return st;
}

int imc_chat_msg( struct groupchat *c, char *msg, int flags )
{
	char *buf = NULL;
	
	if( ( c->ic->flags & OPT_DOES_HTML ) && ( g_strncasecmp( msg, "<html>", 6 ) != 0 ) )
	{
		buf = escape_html( msg );
		msg = buf;
	}
	
	c->ic->acc->prpl->chat_msg( c, msg, flags );
	g_free( buf );
	
	return 1;
}

static char *imc_away_state_find( GList *gcm, char *away, char **message );

int imc_away_send_update( struct im_connection *ic )
{
	char *away, *msg = NULL;
	
	if( ic->acc->prpl->away_states == NULL ||
	    ic->acc->prpl->set_away == NULL )
		return 0;
	
	away = set_getstr( &ic->acc->set, "away" ) ?
	     : set_getstr( &ic->irc->set, "away" );
	if( away && *away )
	{
		GList *m = ic->acc->prpl->away_states( ic );
		msg = ic->acc->flags & ACC_FLAG_AWAY_MESSAGE ? away : NULL;
		away = imc_away_state_find( m, away, &msg ) ? : m->data;
	}
	else if( ic->acc->flags & ACC_FLAG_STATUS_MESSAGE )
	{
		away = NULL;
		msg = set_getstr( &ic->acc->set, "status" ) ?
		    : set_getstr( &ic->irc->set, "status" );
	}
	
	ic->acc->prpl->set_away( ic, away, msg );
	
	return 1;
}

static char *imc_away_alias_list[8][5] =
{
	{ "Away from computer", "Away", "Extended away", NULL },
	{ "NA", "N/A", "Not available", NULL },
	{ "Busy", "Do not disturb", "DND", "Occupied", NULL },
	{ "Be right back", "BRB", NULL },
	{ "On the phone", "Phone", "On phone", NULL },
	{ "Out to lunch", "Lunch", "Food", NULL },
	{ "Invisible", "Hidden" },
	{ NULL }
};

static char *imc_away_state_find( GList *gcm, char *away, char **message )
{
	GList *m;
	int i, j;
	
	for( m = gcm; m; m = m->next )
		if( g_strncasecmp( m->data, away, strlen( m->data ) ) == 0 )
		{
			/* At least the Yahoo! module works better if message
			   contains no data unless it adds something to what
			   we have in state already. */
			if( strlen( m->data ) == strlen( away ) )
				*message = NULL;
			
			return m->data;
		}
	
	for( i = 0; *imc_away_alias_list[i]; i ++ )
	{
		int keep_message;
		
		for( j = 0; imc_away_alias_list[i][j]; j ++ )
			if( g_strncasecmp( away, imc_away_alias_list[i][j], strlen( imc_away_alias_list[i][j] ) ) == 0 )
			{
				keep_message = strlen( away ) != strlen( imc_away_alias_list[i][j] );
				break;
			}
		
		if( !imc_away_alias_list[i][j] )	/* If we reach the end, this row */
			continue;			/* is not what we want. Next!    */
		
		/* Now find an entry in this row which exists in gcm */
		for( j = 0; imc_away_alias_list[i][j]; j ++ )
		{
			for( m = gcm; m; m = m->next )
				if( g_strcasecmp( imc_away_alias_list[i][j], m->data ) == 0 )
				{
					if( !keep_message )
						*message = NULL;
					
					return imc_away_alias_list[i][j];
				}
		}
		
		/* No need to look further, apparently this state doesn't
		   have any good alias for this protocol. */
		break;
	}
	
	return NULL;
}

void imc_add_allow( struct im_connection *ic, char *handle )
{
	if( g_slist_find_custom( ic->permit, handle, (GCompareFunc) ic->acc->prpl->handle_cmp ) == NULL )
	{
		ic->permit = g_slist_prepend( ic->permit, g_strdup( handle ) );
	}
	
	ic->acc->prpl->add_permit( ic, handle );
}

void imc_rem_allow( struct im_connection *ic, char *handle )
{
	GSList *l;
	
	if( ( l = g_slist_find_custom( ic->permit, handle, (GCompareFunc) ic->acc->prpl->handle_cmp ) ) )
	{
		g_free( l->data );
		ic->permit = g_slist_delete_link( ic->permit, l );
	}
	
	ic->acc->prpl->rem_permit( ic, handle );
}

void imc_add_block( struct im_connection *ic, char *handle )
{
	if( g_slist_find_custom( ic->deny, handle, (GCompareFunc) ic->acc->prpl->handle_cmp ) == NULL )
	{
		ic->deny = g_slist_prepend( ic->deny, g_strdup( handle ) );
	}
	
	ic->acc->prpl->add_deny( ic, handle );
}

void imc_rem_block( struct im_connection *ic, char *handle )
{
	GSList *l;
	
	if( ( l = g_slist_find_custom( ic->deny, handle, (GCompareFunc) ic->acc->prpl->handle_cmp ) ) )
	{
		g_free( l->data );
		ic->deny = g_slist_delete_link( ic->deny, l );
	}
	
	ic->acc->prpl->rem_deny( ic, handle );
}

void imcb_clean_handle( struct im_connection *ic, char *handle )
{
	/* Accepts a handle and does whatever is necessary to make it
	   BitlBee-friendly. Currently this means removing everything
	   outside 33-127 (ASCII printable excl spaces), @ (only one
	   is allowed) and ! and : */
	char out[strlen(handle)+1];
	int s, d;
	
	s = d = 0;
	while( handle[s] )
	{
		if( handle[s] > ' ' && handle[s] != '!' && handle[s] != ':' &&
		    ( handle[s] & 0x80 ) == 0 )
		{
			if( handle[s] == '@' )
			{
				/* See if we got an @ already? */
				out[d] = 0;
				if( strchr( out, '@' ) )
					continue;
			}
			
			out[d++] = handle[s];
		}
		s ++;
	}
	out[d] = handle[s];
	
	strcpy( handle, out );
}
