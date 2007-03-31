  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2006 Wilmer van der Gaast and others                *
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
#include "nogaim.h"
#include <ctype.h>

static int remove_chat_buddy_silent( struct groupchat *b, char *handle );

GSList *connections;

#ifdef WITH_PLUGINS
gboolean load_plugin(char *path)
{
	void (*init_function) (void);
	
	GModule *mod = g_module_open(path, G_MODULE_BIND_LAZY);

	if(!mod) {
		log_message(LOGLVL_ERROR, "Can't find `%s', not loading", path);
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

#ifdef WITH_PLUGINS
	load_plugins();
#endif
}

GSList *get_connections() { return connections; }

/* multi.c */

struct im_connection *new_gaim_conn( account_t *acc )
{
	struct im_connection *ic;
	
	ic = g_new0( struct im_connection, 1 );
	
	/* Maybe we should get rid of this memory waste later. ;-) */
	g_snprintf( ic->username, sizeof( ic->username ), "%s", acc->user );
	g_snprintf( ic->password, sizeof( ic->password ), "%s", acc->pass );
	
	ic->irc = acc->irc;
	ic->acc = acc;
	acc->ic = ic;
	
	connections = g_slist_append( connections, ic );
	
	return( ic );
}

void destroy_gaim_conn( struct im_connection *ic )
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

void set_login_progress( struct im_connection *ic, int step, char *msg )
{
	serv_got_crap( ic, "Logging in: %s", msg );
}

/* Errors *while* logging in */
void hide_login_progress( struct im_connection *ic, char *msg )
{
	serv_got_crap( ic, "Login error: %s", msg );
}

/* Errors *after* logging in */
void hide_login_progress_error( struct im_connection *ic, char *msg )
{
	serv_got_crap( ic, "Logged out: %s", msg );
}

void serv_got_crap( struct im_connection *ic, char *format, ... )
{
	va_list params;
	char *text;
	account_t *a;
	
	va_start( params, format );
	text = g_strdup_vprintf( format, params );
	va_end( params );

	if( ( g_strcasecmp( set_getstr( &ic->irc->set, "strip_html" ), "always" ) == 0 ) ||
	    ( ( ic->flags & OPT_CONN_HTML ) && set_getbool( &ic->irc->set, "strip_html" ) ) )
		strip_html( text );
	
	/* Try to find a different connection on the same protocol. */
	for( a = ic->irc->accounts; a; a = a->next )
		if( a->prpl == ic->acc->prpl && a->ic != ic )
			break;
	
	/* If we found one, include the screenname in the message. */
	if( a )
		irc_usermsg( ic->irc, "%s(%s) - %s", ic->acc->prpl->name, ic->username, text );
	else
		irc_usermsg( ic->irc, "%s - %s", ic->acc->prpl->name, text );
	
	g_free( text );
}

static gboolean send_keepalive( gpointer d, gint fd, b_input_condition cond )
{
	struct im_connection *ic = d;
	
	if( ic->acc->prpl->keepalive )
		ic->acc->prpl->keepalive( ic );
	
	return TRUE;
}

void account_online( struct im_connection *ic )
{
	user_t *u;
	
	/* MSN servers sometimes redirect you to a different server and do
	   the whole login sequence again, so these "late" calls to this
	   function should be handled correctly. (IOW, ignored) */
	if( ic->flags & OPT_LOGGED_IN )
		return;
	
	u = user_find( ic->irc, ic->irc->nick );
	
	serv_got_crap( ic, "Logged in" );
	
	ic->keepalive = b_timeout_add( 60000, send_keepalive, ic );
	ic->flags |= OPT_LOGGED_IN;
	
	/* Also necessary when we're not away, at least for some of the
	   protocols. */
	bim_set_away( ic, u->away );
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
	/* while( b_event_remove_by_data( (gpointer) a ) ); */
	b_event_remove( a->reconnect );
	a->reconnect = 0;
}

void signoff( struct im_connection *ic )
{
	irc_t *irc = ic->irc;
	user_t *t, *u = irc->users;
	account_t *a;
	
	/* Nested calls might happen sometimes, this is probably the best
	   place to catch them. */
	if( ic->flags & OPT_LOGGING_OUT )
		return;
	else
		ic->flags |= OPT_LOGGING_OUT;
	
	serv_got_crap( ic, "Signing off.." );
	
	b_event_remove( ic->keepalive );
	ic->keepalive = 0;
	ic->acc->prpl->logout( ic );
	b_event_remove( ic->inpa );
	
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
	else if( !ic->wants_to_die && set_getbool( &irc->set, "auto_reconnect" ) &&
	         set_getbool( &a->set, "auto_reconnect" ) )
	{
		int delay = set_getint( &irc->set, "auto_reconnect_delay" );
		
		serv_got_crap( ic, "Reconnecting in %d seconds..", delay );
		a->reconnect = b_timeout_add( delay * 1000, auto_reconnect, a );
	}
	
	destroy_gaim_conn( ic );
}


/* dialogs.c */

void do_error_dialog( struct im_connection *ic, char *msg, char *title )
{
	if( msg && title )
		serv_got_crap( ic, "Error: %s: %s", title, msg );
	else if( msg )
		serv_got_crap( ic, "Error: %s", msg );
	else if( title )
		serv_got_crap( ic, "Error: %s", title );
	else
		serv_got_crap( ic, "Error" );
}

void do_ask_dialog( struct im_connection *ic, char *msg, void *data, void *doit, void *dont )
{
	query_add( ic->irc, ic, msg, doit, dont, data );
}


/* list.c */

void add_buddy( struct im_connection *ic, char *group, char *handle, char *realname )
{
	user_t *u;
	char nick[MAX_NICK_LENGTH+1];
	char *s;
	irc_t *irc = ic->irc;
	
	if( set_getbool( &irc->set, "debug" ) && 0 ) /* This message is too useless */
		serv_got_crap( ic, "Receiving user add from handle: %s", handle );
	
	if( user_findhandle( ic, handle ) )
	{
		if( set_getbool( &irc->set, "debug" ) )
			serv_got_crap( ic, "User already exists, ignoring add request: %s", handle );
		
		return;
		
		/* Buddy seems to exist already. Let's ignore this request then... */
	}
	
	memset( nick, 0, MAX_NICK_LENGTH + 1 );
	strcpy( nick, nick_get( ic->acc, handle, realname ) );
	
	u = user_add( ic->irc, nick );
	
	if( !realname || !*realname ) realname = nick;
	u->realname = g_strdup( realname );
	
	if( ( s = strchr( handle, '@' ) ) )
	{
		u->host = g_strdup( s + 1 );
		u->user = g_strndup( handle, s - handle );
	}
	else if( ic->acc->server )
	{
		char *colon;
		
		if( ( colon = strchr( ic->acc->server, ':' ) ) )
			u->host = g_strndup( ic->acc->server,
			                     colon - ic->acc->server );
		else
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

struct buddy *find_buddy( struct im_connection *ic, char *handle )
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

void signoff_blocked( struct im_connection *ic )
{
	return; /* Make all blocked users look invisible (TODO?) */
}


void serv_buddy_rename( struct im_connection *ic, char *handle, char *realname )
{
	user_t *u = user_findhandle( ic, handle );
	
	if( !u ) return;
	
	if( g_strcasecmp( u->realname, realname ) != 0 )
	{
		if( u->realname != u->nick ) g_free( u->realname );
		
		u->realname = g_strdup( realname );
		
		if( ( ic->flags & OPT_LOGGED_IN ) && set_getbool( &ic->irc->set, "display_namechanges" ) )
			serv_got_crap( ic, "User `%s' changed name to `%s'", u->nick, u->realname );
	}
}


/* prpl.c */

struct show_got_added_data
{
	struct im_connection *ic;
	char *handle;
};

void show_got_added_no( gpointer w, struct show_got_added_data *data )
{
	g_free( data->handle );
	g_free( data );
}

void show_got_added_yes( gpointer w, struct show_got_added_data *data )
{
	data->ic->acc->prpl->add_buddy( data->ic, data->handle, NULL );
	add_buddy( data->ic, NULL, data->handle, data->handle );
	
	return show_got_added_no( w, data );
}

void show_got_added( struct im_connection *ic, char *handle, const char *realname )
{
	struct show_got_added_data *data = g_new0( struct show_got_added_data, 1 );
	char *s;
	
	/* TODO: Make a setting for this! */
	if( user_findhandle( ic, handle ) != NULL )
		return;
	
	s = g_strdup_printf( "The user %s is not in your buddy list yet. Do you want to add him/her now?", handle );
	
	data->ic = ic;
	data->handle = g_strdup( handle );
	query_add( ic->irc, ic, s, show_got_added_yes, show_got_added_no, data );
}


/* server.c */                    

void serv_got_update( struct im_connection *ic, char *handle, int loggedin, int evil, time_t signon, time_t idle, int type, guint caps )
{
	user_t *u;
	int oa, oo;
	
	u = user_findhandle( ic, handle );
	
	if( !u )
	{
		if( g_strcasecmp( set_getstr( &ic->irc->set, "handle_unknown" ), "add" ) == 0 )
		{
			add_buddy( ic, NULL, handle, NULL );
			u = user_findhandle( ic, handle );
		}
		else
		{
			if( set_getbool( &ic->irc->set, "debug" ) || g_strcasecmp( set_getstr( &ic->irc->set, "handle_unknown" ), "ignore" ) != 0 )
			{
				serv_got_crap( ic, "serv_got_update() for handle %s:", handle );
				serv_got_crap( ic, "loggedin = %d, type = %d", loggedin, type );
			}
			
			return;
		}
		/* Why did we have this here....
		return; */
	}
	
	oa = u->away != NULL;
	oo = u->online;
	
	if( u->away )
	{
		g_free( u->away );
		u->away = NULL;
	}
	
	if( loggedin && !u->online )
	{
		irc_spawn( ic->irc, u );
		u->online = 1;
	}
	else if( !loggedin && u->online )
	{
		struct groupchat *c;
		
		irc_kill( ic->irc, u );
		u->online = 0;
		
		/* Remove him/her from the conversations to prevent PART messages after he/she QUIT already */
		for( c = ic->conversations; c; c = c->next )
			remove_chat_buddy_silent( c, handle );
	}
	
	if( ( type & UC_UNAVAILABLE ) && ( strcmp( ic->acc->prpl->name, "oscar" ) == 0 || strcmp( ic->acc->prpl->name, "icq" ) == 0 ) )
	{
		u->away = g_strdup( "Away" );
	}
	else if( ( type & UC_UNAVAILABLE ) && ( strcmp( ic->acc->prpl->name, "jabber" ) == 0 ) )
	{
		if( type & UC_DND )
			u->away = g_strdup( "Do Not Disturb" );
		else if( type & UC_XA )
			u->away = g_strdup( "Extended Away" );
		else // if( type & UC_AWAY )
			u->away = g_strdup( "Away" );
	}
	else if( ( type & UC_UNAVAILABLE ) && ic->acc->prpl->get_status_string )
	{
		u->away = g_strdup( ic->acc->prpl->get_status_string( ic, type ) );
	}
	else
		u->away = NULL;
	
	/* LISPy... */
	if( ( set_getbool( &ic->irc->set, "away_devoice" ) ) &&		/* Don't do a thing when user doesn't want it */
	    ( u->online ) &&						/* Don't touch offline people */
	    ( ( ( u->online != oo ) && !u->away ) ||			/* Voice joining people */
	      ( ( u->online == oo ) && ( oa == !u->away ) ) ) )		/* (De)voice people changing state */
	{
		irc_write( ic->irc, ":%s MODE %s %cv %s", ic->irc->myhost,
		                                                ic->irc->channel, u->away?'-':'+', u->nick );
	}
}

void serv_got_im( struct im_connection *ic, char *handle, char *msg, guint32 flags, time_t mtime, gint len )
{
	irc_t *irc = ic->irc;
	user_t *u;
	
	u = user_findhandle( ic, handle );
	
	if( !u )
	{
		char *h = set_getstr( &irc->set, "handle_unknown" );
		
		if( g_strcasecmp( h, "ignore" ) == 0 )
		{
			if( set_getbool( &irc->set, "debug" ) )
				serv_got_crap( ic, "Ignoring message from unknown handle %s", handle );
			
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
			
			add_buddy( ic, NULL, handle, NULL );
			u = user_findhandle( ic, handle );
			u->is_private = private;
		}
		else
		{
			serv_got_crap( ic, "Message from unknown handle %s:", handle );
			u = user_find( irc, irc->mynick );
		}
	}
	
	if( ( g_strcasecmp( set_getstr( &ic->irc->set, "strip_html" ), "always" ) == 0 ) ||
	    ( ( ic->flags & OPT_CONN_HTML ) && set_getbool( &ic->irc->set, "strip_html" ) ) )
		strip_html( msg );

	while( strlen( msg ) > 425 )
	{
		char tmp, *nl;
		
		tmp = msg[425];
		msg[425] = 0;
		
		/* If there's a newline/space in this string, split up there,
		   looks a bit prettier. */
		if( ( nl = strrchr( msg, '\n' ) ) || ( nl = strrchr( msg, ' ' ) ) )
		{
			msg[425] = tmp;
			tmp = *nl;
			*nl = 0;
		}
		
		irc_msgfrom( irc, u->nick, msg );
		
		/* Move on. */
		if( nl )
		{
			*nl = tmp;
			msg = nl + 1;
		}
		else
		{
			msg[425] = tmp;
			msg += 425;
		}
	}
	irc_msgfrom( irc, u->nick, msg );
}

void serv_got_typing( struct im_connection *ic, char *handle, int timeout, int type )
{
	user_t *u;
	
	if( !set_getbool( &ic->irc->set, "typing_notice" ) )
		return;
	
	if( ( u = user_findhandle( ic, handle ) ) ) {
		/* If type is:
		 * 0: user has stopped typing
		 * 1: user is actively typing
		 * 2: user has entered text, but is not actively typing
		 */
		if (type == 0 || type == 1 || type == 2) {
			char buf[256]; 
			g_snprintf(buf, 256, "\1TYPING %d\1", type); 
			irc_privmsg( ic->irc, u, "PRIVMSG", ic->irc->nick, NULL, buf );
		}
	}
}

void serv_got_chat_left( struct groupchat *c )
{
	struct im_connection *ic = c->ic;
	struct groupchat *l = NULL;
	GList *ir;
	
	if( set_getbool( &ic->irc->set, "debug" ) )
		serv_got_crap( ic, "You were removed from conversation 0x%x", (int) c );
	
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
		
		if( l )
			l->next = c->next;
		else
			ic->conversations = c->next;
		
		for( ir = c->in_room; ir; ir = ir->next )
			g_free( ir->data );
		g_list_free( c->in_room );
		g_free( c->channel );
		g_free( c->title );
		g_free( c );
	}
}

void serv_got_chat_in( struct groupchat *c, char *who, int whisper, char *msg, time_t mtime )
{
	struct im_connection *ic = c->ic;
	user_t *u;
	
	/* Gaim sends own messages through this too. IRC doesn't want this, so kill them */
	if( g_strcasecmp( who, ic->username ) == 0 )
		return;
	
	u = user_findhandle( ic, who );
	
	if( ( g_strcasecmp( set_getstr( &ic->irc->set, "strip_html" ), "always" ) == 0 ) ||
	    ( ( ic->flags & OPT_CONN_HTML ) && set_getbool( &ic->irc->set, "strip_html" ) ) )
		strip_html( msg );
	
	if( c && u )
		irc_privmsg( ic->irc, u, "PRIVMSG", c->channel, "", msg );
	else
		serv_got_crap( ic, "Message from/to conversation %s@0x%x (unknown conv/user): %s", who, (int) c, msg );
}

struct groupchat *serv_got_joined_chat( struct im_connection *ic, char *handle )
{
	struct groupchat *c;
	
	/* This one just creates the conversation structure, user won't see anything yet */
	
	if( ic->conversations )
	{
		for( c = ic->conversations; c->next; c = c->next );
		c = c->next = g_new0( struct groupchat, 1 );
	}
	else
		ic->conversations = c = g_new0( struct groupchat, 1 );
	
	c->ic = ic;
	c->title = g_strdup( handle );
	c->channel = g_strdup_printf( "&chat_%03d", ic->irc->c_id++ );
	
	if( set_getbool( &ic->irc->set, "debug" ) )
		serv_got_crap( ic, "Creating new conversation: (id=0x%x,handle=%s)", (int) c, handle );
	
	return c;
}


/* buddy_chat.c */

void add_chat_buddy( struct groupchat *b, char *handle )
{
	user_t *u = user_findhandle( b->ic, handle );
	int me = 0;
	
	if( set_getbool( &b->ic->irc->set, "debug" ) )
		serv_got_crap( b->ic, "User %s added to conversation 0x%x", handle, (int) b );
	
	/* It might be yourself! */
	if( b->ic->acc->prpl->handle_cmp( handle, b->ic->username ) == 0 )
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
		add_buddy( b->ic, NULL, handle, NULL );
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

void remove_chat_buddy( struct groupchat *b, char *handle, char *reason )
{
	user_t *u;
	int me = 0;
	
	if( set_getbool( &b->ic->irc->set, "debug" ) )
		serv_got_crap( b->ic, "User %s removed from conversation 0x%x (%s)", handle, (int) b, reason ? reason : "" );
	
	/* It might be yourself! */
	if( g_strcasecmp( handle, b->ic->username ) == 0 )
	{
		u = user_find( b->ic->irc, b->ic->irc->nick );
		b->joined = 0;
		me = 1;
	}
	else
	{
		u = user_findhandle( b->ic, handle );
	}
	
	if( remove_chat_buddy_silent( b, handle ) )
		if( ( b->joined || me ) && u )
			irc_part( b->ic->irc, u, b->channel );
}

static int remove_chat_buddy_silent( struct groupchat *b, char *handle )
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

struct groupchat *chat_by_channel( char *channel )
{
	struct im_connection *ic;
	struct groupchat *c;
	GSList *l;
	
	/* This finds the connection which has a conversation which belongs to this channel */
	for( l = connections; l; l = l->next )
	{
		ic = l->data;
		for( c = ic->conversations; c && g_strcasecmp( c->channel, channel ) != 0; c = c->next );
		if( c )
			return c;
	}
	
	return NULL;
}

char *set_eval_away_devoice( set_t *set, char *value )
{
	irc_t *irc = set->data;
	int st;
	
	if( ( g_strcasecmp( value, "true" ) == 0 ) || ( g_strcasecmp( value, "yes" ) == 0 ) || ( g_strcasecmp( value, "on" ) == 0 ) )
		st = 1;
	else if( ( g_strcasecmp( value, "false" ) == 0 ) || ( g_strcasecmp( value, "no" ) == 0 ) || ( g_strcasecmp( value, "off" ) == 0 ) )
		st = 0;
	else if( sscanf( value, "%d", &st ) != 1 )
		return( NULL );
	
	st = st != 0;
	
	/* Horror.... */
	
	if( st != set_getbool( &irc->set, "away_devoice" ) )
	{
		char list[80] = "";
		user_t *u = irc->users;
		int i = 0, count = 0;
		char pm;
		char v[80];
		
		if( st )
			pm = '+';
		else
			pm = '-';
		
		while( u )
		{
			if( u->ic && u->online && !u->away )
			{
				if( ( strlen( list ) + strlen( u->nick ) ) >= 79 )
				{
					for( i = 0; i < count; v[i++] = 'v' ); v[i] = 0;
					irc_write( irc, ":%s MODE %s %c%s%s",
					           irc->myhost,
		        			   irc->channel, pm, v, list );
					
					*list = 0;
					count = 0;
				}
				
				sprintf( list + strlen( list ), " %s", u->nick );
				count ++;
			}
			u = u->next;
		}
		
		/* $v = 'v' x $i */
		for( i = 0; i < count; v[i++] = 'v' ); v[i] = 0;
		irc_write( irc, ":%s MODE %s %c%s%s", irc->myhost,
		                                            irc->channel, pm, v, list );
	}
	
	return( set_eval_bool( set, value ) );
}




/* The plan is to not allow straight calls to prpl functions anymore, but do
   them all from some wrappers. We'll start to define some down here: */

int bim_buddy_msg( struct im_connection *ic, char *handle, char *msg, int flags )
{
	char *buf = NULL;
	int st;
	
	if( ( ic->flags & OPT_CONN_HTML ) && ( g_strncasecmp( msg, "<html>", 6 ) != 0 ) )
	{
		buf = escape_html( msg );
		msg = buf;
	}
	
	st = ic->acc->prpl->send_im( ic, handle, msg, flags );
	g_free( buf );
	
	return st;
}

int bim_chat_msg( struct groupchat *c, char *msg, int flags )
{
	char *buf = NULL;
	int st;
	
	if( ( c->ic->flags & OPT_CONN_HTML ) && ( g_strncasecmp( msg, "<html>", 6 ) != 0 ) )
	{
		buf = escape_html( msg );
		msg = buf;
	}
	
	c->ic->acc->prpl->chat_send( c, msg, flags );
	g_free( buf );
	
	return 1;
}

static char *bim_away_alias_find( GList *gcm, char *away );

int bim_set_away( struct im_connection *ic, char *away )
{
	GList *m, *ms;
	char *s;
	
	if( !away ) away = "";
	ms = m = ic->acc->prpl->away_states( ic );
	
	while( m )
	{
		if( *away )
		{
			if( g_strncasecmp( m->data, away, strlen( m->data ) ) == 0 )
				break;
		}
		else
		{
			if( g_strcasecmp( m->data, "Available" ) == 0 )
				break;
			if( g_strcasecmp( m->data, "Online" ) == 0 )
				break;
		}
		m = m->next;
	}
	
	if( m )
	{
		ic->acc->prpl->set_away( ic, m->data, *away ? away : NULL );
	}
	else
	{
		s = bim_away_alias_find( ms, away );
		if( s )
		{
			ic->acc->prpl->set_away( ic, s, away );
			if( set_getbool( &ic->irc->set, "debug" ) )
				serv_got_crap( ic, "Setting away state to %s", s );
		}
		else
			ic->acc->prpl->set_away( ic, GAIM_AWAY_CUSTOM, away );
	}
	
	return( 1 );
}

static char *bim_away_alias_list[8][5] =
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

static char *bim_away_alias_find( GList *gcm, char *away )
{
	GList *m;
	int i, j;
	
	for( i = 0; *bim_away_alias_list[i]; i ++ )
	{
		for( j = 0; bim_away_alias_list[i][j]; j ++ )
			if( g_strncasecmp( away, bim_away_alias_list[i][j], strlen( bim_away_alias_list[i][j] ) ) == 0 )
				break;
		
		if( !bim_away_alias_list[i][j] )	/* If we reach the end, this row */
			continue;			/* is not what we want. Next!    */
		
		/* Now find an entry in this row which exists in gcm */
		for( j = 0; bim_away_alias_list[i][j]; j ++ )
		{
			m = gcm;
			while( m )
			{
				if( g_strcasecmp( bim_away_alias_list[i][j], m->data ) == 0 )
					return( bim_away_alias_list[i][j] );
				m = m->next;
			}
		}
	}
	
	return( NULL );
}

void bim_add_allow( struct im_connection *ic, char *handle )
{
	if( g_slist_find_custom( ic->permit, handle, (GCompareFunc) ic->acc->prpl->handle_cmp ) == NULL )
	{
		ic->permit = g_slist_prepend( ic->permit, g_strdup( handle ) );
	}
	
	ic->acc->prpl->add_permit( ic, handle );
}

void bim_rem_allow( struct im_connection *ic, char *handle )
{
	GSList *l;
	
	if( ( l = g_slist_find_custom( ic->permit, handle, (GCompareFunc) ic->acc->prpl->handle_cmp ) ) )
	{
		g_free( l->data );
		ic->permit = g_slist_delete_link( ic->permit, l );
	}
	
	ic->acc->prpl->rem_permit( ic, handle );
}

void bim_add_block( struct im_connection *ic, char *handle )
{
	if( g_slist_find_custom( ic->deny, handle, (GCompareFunc) ic->acc->prpl->handle_cmp ) == NULL )
	{
		ic->deny = g_slist_prepend( ic->deny, g_strdup( handle ) );
	}
	
	ic->acc->prpl->add_deny( ic, handle );
}

void bim_rem_block( struct im_connection *ic, char *handle )
{
	GSList *l;
	
	if( ( l = g_slist_find_custom( ic->deny, handle, (GCompareFunc) ic->acc->prpl->handle_cmp ) ) )
	{
		g_free( l->data );
		ic->deny = g_slist_delete_link( ic->deny, l );
	}
	
	ic->acc->prpl->rem_deny( ic, handle );
}
