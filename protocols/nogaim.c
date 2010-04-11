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

struct im_connection *imcb_new( account_t *acc )
{
	struct im_connection *ic;
	
	ic = g_new0( struct im_connection, 1 );
	
	ic->bee = acc->bee;
	ic->acc = acc;
	acc->ic = ic;
	
	connections = g_slist_append( connections, ic );
	
	return( ic );
}

void imc_free( struct im_connection *ic )
{
	account_t *a;
	
	/* Destroy the pointer to this connection from the account list */
	for( a = ic->bee->accounts; a; a = a->next )
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

	if( ( g_strcasecmp( set_getstr( &ic->bee->set, "strip_html" ), "always" ) == 0 ) ||
	    ( ( ic->flags & OPT_DOES_HTML ) && set_getbool( &ic->bee->set, "strip_html" ) ) )
		strip_html( text );
	
	/* Try to find a different connection on the same protocol. */
	for( a = ic->bee->accounts; a; a = a->next )
		if( a->prpl == ic->acc->prpl && a->ic != ic )
			break;
	
	/* If we found one, include the screenname in the message. */
	if( a )
		/* FIXME(wilmer): ui_log callback or so */
		irc_usermsg( ic->bee->ui_data, "%s(%s) - %s", ic->acc->prpl->name, ic->acc->user, text );
	else
		irc_usermsg( ic->bee->ui_data, "%s - %s", ic->acc->prpl->name, text );
	
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
	/* MSN servers sometimes redirect you to a different server and do
	   the whole login sequence again, so these "late" calls to this
	   function should be handled correctly. (IOW, ignored) */
	if( ic->flags & OPT_LOGGED_IN )
		return;
	
	imcb_log( ic, "Logged in" );
	
	ic->keepalive = b_timeout_add( 60000, send_keepalive, ic );
	ic->flags |= OPT_LOGGED_IN;
	
	/* Necessary to send initial presence status, even if we're not away. */
	imc_away_send_update( ic );
	
	/* Apparently we're connected successfully, so reset the
	   exponential backoff timer. */
	ic->acc->auto_reconnect_delay = 0;
	
	/*
	for( c = irc->chatrooms; c; c = c->next )
	{
		if( c->acc != ic->acc )
			continue;
		
		if( set_getbool( &c->set, "auto_join" ) )
			chat_join( irc, c, NULL );
	}
	*/
}

gboolean auto_reconnect( gpointer data, gint fd, b_input_condition cond )
{
	account_t *a = data;
	
	a->reconnect = 0;
	account_on( a->bee, a );
	
	return( FALSE );	/* Only have to run the timeout once */
}

void cancel_auto_reconnect( account_t *a )
{
	b_event_remove( a->reconnect );
	a->reconnect = 0;
}

void imc_logout( struct im_connection *ic, int allow_reconnect )
{
	bee_t *bee = ic->bee;
	account_t *a;
	GSList *l;
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
	
	for( l = bee->users; l; l = l->next )
	{
		bee_user_t *bu = l->data;
		
		if( bu->ic == ic )
			bee_user_free( bee, ic, bu->handle );
	}
	
	//query_del_by_conn( ic->irc, ic );
	
	for( a = bee->accounts; a; a = a->next )
		if( a->ic == ic )
			break;
	
	if( !a )
	{
		/* Uhm... This is very sick. */
	}
	else if( allow_reconnect && set_getbool( &bee->set, "auto_reconnect" ) &&
	         set_getbool( &a->set, "auto_reconnect" ) &&
	         ( delay = account_reconnect_delay( a ) ) > 0 )
	{
		imcb_log( ic, "Reconnecting in %d seconds..", delay );
		a->reconnect = b_timeout_add( delay * 1000, auto_reconnect, a );
	}
	
	imc_free( ic );
}

void imcb_ask( struct im_connection *ic, char *msg, void *data,
               query_callback doit, query_callback dont )
{
	query_add( (irc_t *) ic->bee->ui_data, ic, msg, doit, dont, data );
}

void imcb_add_buddy( struct im_connection *ic, const char *handle, const char *group )
{
	bee_user_t *bu;
	bee_t *bee = ic->bee;
	
	if( bee_user_by_handle( bee, ic, handle ) )
	{
		if( set_getbool( &bee->set, "debug" ) )
			imcb_log( ic, "User already exists, ignoring add request: %s", handle );
		
		return;
		
		/* Buddy seems to exist already. Let's ignore this request then...
		   Eventually subsequent calls to this function *should* be possible
		   when a buddy is in multiple groups. But for now BitlBee doesn't
		   even support groups so let's silently ignore this for now. */
	}
	
	bu = bee_user_new( bee, ic, handle );
	bu->group = g_strdup( group );
}

void imcb_rename_buddy( struct im_connection *ic, const char *handle, const char *fullname )
{
	bee_t *bee = ic->bee;
	bee_user_t *bu = bee_user_by_handle( bee, ic, handle );
	
	if( !bu || !fullname ) return;
	
	if( !bu->fullname || strcmp( bu->fullname, fullname ) != 0 )
	{
		g_free( bu->fullname );
		bu->fullname = g_strdup( fullname );
		
		if( bee->ui->user_fullname )
			bee->ui->user_fullname( bee, bu );
	}
}

void imcb_remove_buddy( struct im_connection *ic, const char *handle, char *group )
{
	bee_user_free( ic->bee, ic, handle );
}

/* Mainly meant for ICQ (and now also for Jabber conferences) to allow IM
   modules to suggest a nickname for a handle. */
void imcb_buddy_nick_hint( struct im_connection *ic, const char *handle, const char *nick )
{
#if 0
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
		if( set_getbool( &ic->bee->set, "lcnicks" ) )
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
#endif
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
	query_add( (irc_t *) ic->bee->ui_data, ic, s,
	           imcb_ask_auth_cb_yes, imcb_ask_auth_cb_no, data );
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
	if( bee_user_by_handle( ic->bee, ic, handle ) != NULL )
		return;
	
	s = g_strdup_printf( "The user %s is not in your buddy list yet. Do you want to add him/her now?", handle );
	
	data->ic = ic;
	data->handle = g_strdup( handle );
	query_add( (irc_t *) ic->bee->ui_data, ic, s,
	           imcb_ask_add_cb_yes, imcb_ask_add_cb_no, data );
}

void imcb_buddy_typing( struct im_connection *ic, char *handle, uint32_t flags )
{
#if 0
	user_t *u;
	
	if( !set_getbool( &ic->bee->set, "typing_notice" ) )
		return;
	
	if( ( u = user_findhandle( ic, handle ) ) )
	{
		char buf[256]; 
		
		g_snprintf( buf, 256, "\1TYPING %d\1", ( flags >> 8 ) & 3 );
		irc_privmsg( ic->irc, u, "PRIVMSG", ic->irc->nick, NULL, buf );
	}
#endif
}

struct bee_user *imcb_buddy_by_handle( struct im_connection *ic, const char *handle )
{
	return bee_user_by_handle( ic->bee, ic, handle );
}

struct groupchat *imcb_chat_new( struct im_connection *ic, const char *handle )
{
#if 0
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
	
	if( set_getbool( &ic->bee->set, "debug" ) )
		imcb_log( ic, "Creating new conversation: (id=%p,handle=%s)", c, handle );
	
	return c;
#endif
	return NULL;
}

void imcb_chat_free( struct groupchat *c )
{
#if 0
	struct im_connection *ic = c->ic;
	struct groupchat *l;
	GList *ir;
	
	if( set_getbool( &ic->bee->set, "debug" ) )
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
#endif
}

void imcb_chat_msg( struct groupchat *c, const char *who, char *msg, uint32_t flags, time_t sent_at )
{
#if 0
	struct im_connection *ic = c->ic;
	char *wrapped;
	user_t *u;
	
	/* Gaim sends own messages through this too. IRC doesn't want this, so kill them */
	if( g_strcasecmp( who, ic->acc->user ) == 0 )
		return;
	
	u = user_findhandle( ic, who );
	
	if( ( g_strcasecmp( set_getstr( &ic->bee->set, "strip_html" ), "always" ) == 0 ) ||
	    ( ( ic->flags & OPT_DOES_HTML ) && set_getbool( &ic->bee->set, "strip_html" ) ) )
		strip_html( msg );
	
	wrapped = word_wrap( msg, 425 );
	if( c && u )
	{
		irc_privmsg( ic->irc, u, "PRIVMSG", c->channel, "", wrapped );
	}
	else
	{
		imcb_log( ic, "Message from/to conversation %s@%p (unknown conv/user): %s", who, c, wrapped );
	}
	g_free( wrapped );
#endif
}

void imcb_chat_log( struct groupchat *c, char *format, ... )
{
#if 0
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
#endif
}

void imcb_chat_topic( struct groupchat *c, char *who, char *topic, time_t set_at )
{
#if 0
	struct im_connection *ic = c->ic;
	user_t *u = NULL;
	
	if( who == NULL)
		u = user_find( ic->irc, ic->irc->mynick );
	else if( g_strcasecmp( who, ic->acc->user ) == 0 )
		u = user_find( ic->irc, ic->irc->nick );
	else
		u = user_findhandle( ic, who );
	
	if( ( g_strcasecmp( set_getstr( &ic->bee->set, "strip_html" ), "always" ) == 0 ) ||
	    ( ( ic->flags & OPT_DOES_HTML ) && set_getbool( &ic->bee->set, "strip_html" ) ) )
		strip_html( topic );
	
	g_free( c->topic );
	c->topic = g_strdup( topic );
	
	if( c->joined && u )
		irc_write( ic->irc, ":%s!%s@%s TOPIC %s :%s", u->nick, u->user, u->host, c->channel, topic );
#endif
}

void imcb_chat_add_buddy( struct groupchat *b, const char *handle )
{
#if 0
	user_t *u = user_findhandle( b->ic, handle );
	int me = 0;
	
	if( set_getbool( &b->ic->bee->set, "debug" ) )
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
#endif
}

/* This function is one BIG hack... :-( EREWRITE */
void imcb_chat_remove_buddy( struct groupchat *b, const char *handle, const char *reason )
{
#if 0
	user_t *u;
	int me = 0;
	
	if( set_getbool( &b->ic->bee->set, "debug" ) )
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
#endif
}

#if 0
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
	
	return 0;
}
#endif


/* Misc. BitlBee stuff which shouldn't really be here */
#if 0
char *set_eval_away_devoice( set_t *set, char *value )
{
	irc_t *irc = set->data;
	int st;
	
	if( !is_bool( value ) )
		return SET_INVALID;
	
	st = bool2int( value );
	
	/* Horror.... */
	
	if( st != set_getbool( &irc->b->set, "away_devoice" ) )
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
	
	return value;
}
#endif



/* The plan is to not allow straight calls to prpl functions anymore, but do
   them all from some wrappers. We'll start to define some down here: */

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
	
	away = set_getstr( &ic->acc->set, "away" ) ?
	     : set_getstr( &ic->bee->set, "away" );
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
		    : set_getstr( &ic->bee->set, "status" );
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
