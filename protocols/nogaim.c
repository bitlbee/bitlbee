  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2004 Wilmer van der Gaast and others                *
  \********************************************************************/

/*
 * nogaim
 *
 * Gaim without gaim - for BitlBee
 *
 * This file contains functions called by the Gaim IM-modules. It's written
 * from scratch for BitlBee and doesn't contain any code from Gaim anymore
 * (except for the function names).
 *
 * Copyright 2002-2004 Wilmer van der Gaast <lintux@lintux.cx>
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
#include <iconv.h>

struct prpl *proto_prpl[PROTO_MAX];
char proto_name[PROTO_MAX][8] = { "TOC", "OSCAR", "YAHOO", "ICQ", "MSN", "", "", "", "JABBER", "", "", "", "", "", "", "" };

static char *proto_away_alias[7][5] =
{
	{ "Away from computer", "Away", "Extended away", NULL },
	{ "NA", "N/A", "Not available", NULL },
	{ "Busy", "Do not disturb", "DND", "Occupied", NULL },
	{ "Be right back", "BRB", NULL },
	{ "On the phone", "Phone", "On phone", NULL },
	{ "Out to lunch", "Lunch", "Food", NULL },
	{ NULL }
};
static char *proto_away_alias_find( GList *gcm, char *away );

static int remove_chat_buddy_silent( struct conversation *b, char *handle );

GSList *connections;


/* nogaim.c */

void nogaim_init()
{
	proto_prpl[PROTO_MSN] = g_new0 ( struct prpl, 1 );
#ifdef WITH_MSN
	msn_init( proto_prpl[PROTO_MSN] );
#endif

	proto_prpl[PROTO_OSCAR] = g_new0( struct prpl, 1 );
#ifdef WITH_OSCAR
	oscar_init( proto_prpl[PROTO_OSCAR] );
#endif
	
	proto_prpl[PROTO_YAHOO] = g_new0( struct prpl, 1 );
#ifdef WITH_YAHOO
	byahoo_init( proto_prpl[PROTO_YAHOO] );
#endif
	
	proto_prpl[PROTO_JABBER] = g_new0( struct prpl, 1 );
#ifdef WITH_JABBER
	jabber_init( proto_prpl[PROTO_JABBER] );
#endif
}

GSList *get_connections() { return connections; }

int proto_away( struct gaim_connection *gc, char *away )
{
	GList *m, *ms;
	char *s;
	
	if( !away ) away = "";
	ms = m = gc->prpl->away_states( gc );
	
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
		gc->prpl->set_away( gc, m->data, *away ? away : NULL );
	}
	else
	{
		s = proto_away_alias_find( ms, away );
		if( s )
		{
			gc->prpl->set_away( gc, s, away );
			if( set_getint( gc->irc, "debug" ) )
				serv_got_crap( gc, "Setting away state to %s", s );
		}
		else
			gc->prpl->set_away( gc, GAIM_AWAY_CUSTOM, away );
	}
	
	g_list_free( ms );
	
	return( 1 );
}

static char *proto_away_alias_find( GList *gcm, char *away )
{
	GList *m;
	int i, j;
	
	for( i = 0; *proto_away_alias[i]; i ++ )
	{
		for( j = 0; proto_away_alias[i][j]; j ++ )
			if( g_strncasecmp( away, proto_away_alias[i][j], strlen( proto_away_alias[i][j] ) ) == 0 )
				break;
		
		if( !proto_away_alias[i][j] )	/* If we reach the end, this row */
			continue;		/* is not what we want. Next!    */
		
		/* Now find an entry in this row which exists in gcm */
		for( j = 0; proto_away_alias[i][j]; j ++ )
		{
			m = gcm;
			while( m )
			{
				if( g_strcasecmp( proto_away_alias[i][j], m->data ) == 0 )
					return( proto_away_alias[i][j] );
				m = m->next;
			}
		}
	}
	
	return( NULL );
}

/* multi.c */

struct gaim_connection *new_gaim_conn( struct aim_user *user )
{
	struct gaim_connection *gc;
	account_t *a;
	
	gc = g_new0( struct gaim_connection, 1 );
	
	gc->protocol = user->protocol;
	gc->prpl = proto_prpl[gc->protocol];
	g_snprintf( gc->username, sizeof( gc->username ), "%s", user->username );
	g_snprintf( gc->password, sizeof( gc->password ), "%s", user->password );
	/* [MD]	BUGFIX: don't set gc->irc to the global IRC, but use the one from the struct aim_user.
	 * This fixes daemon mode breakage where IRC doesn't point to the currently active connection.
	 */
	gc->irc=user->irc;
	
	connections = g_slist_append( connections, gc );
	
	user->gc = gc;
	gc->user = user;
	
	// Find the account_t so we can set its gc pointer
	for( a = gc->irc->accounts; a; a = a->next )
		if( ( struct aim_user * ) a->gc == user )
		{
			a->gc = gc;
			break;
		}
	
	return( gc );
}

void destroy_gaim_conn( struct gaim_connection *gc )
{
	account_t *a;
	
	/* Destroy the pointer to this connection from the account list */
	for( a = gc->irc->accounts; a; a = a->next )
		if( a->gc == gc )
		{
			a->gc = NULL;
			break;
		}
	
	connections = g_slist_remove( connections, gc );
	g_free( gc->user );
	g_free( gc );
}

void set_login_progress( struct gaim_connection *gc, int step, char *msg )
{
	serv_got_crap( gc, "Logging in: %s", msg );
}

/* Errors *while* logging in */
void hide_login_progress( struct gaim_connection *gc, char *msg )
{
	serv_got_crap( gc, "Login error: %s", msg );
}

/* Errors *after* logging in */
void hide_login_progress_error( struct gaim_connection *gc, char *msg )
{
	serv_got_crap( gc, "Logged out: %s", msg );
}

void serv_got_crap( struct gaim_connection *gc, char *format, ... )
{
	va_list params;
	char text[1024], buf[1024], acc_id[33];
	char *msg;
	account_t *a;
	
	va_start( params, format );
	g_vsnprintf( text, sizeof( text ), format, params );
	va_end( params );

	if( g_strncasecmp( set_getstr( gc->irc, "charset" ), "none", 4 ) != 0 &&
	    do_iconv( "UTF8", set_getstr( gc->irc, "charset" ), text, buf, 0, 1024 ) != -1 )
		msg = buf;
	else
		msg = text;
	
	if( ( g_strcasecmp( set_getstr( gc->irc, "strip_html" ), "always" ) == 0 ) ||
	    ( ( gc->flags & OPT_CONN_HTML ) && set_getint( gc->irc, "strip_html" ) ) )
		strip_html( msg );
	
	/* Try to find a different connection on the same protocol. */
	for( a = gc->irc->accounts; a; a = a->next )
		if( proto_prpl[a->protocol] == gc->prpl && a->gc != gc )
			break;
	
	/* If we found one, add the screenname to the acc_id. */
	if( a )
		g_snprintf( acc_id, 32, "%s(%s)", proto_name[gc->protocol], gc->username );
	else
		g_snprintf( acc_id, 32, "%s", proto_name[gc->protocol] );
	
	irc_usermsg( gc->irc, "%s - %s", acc_id, msg );
}

static gboolean send_keepalive( gpointer d )
{
	struct gaim_connection *gc = d;
	
	if( gc->prpl && gc->prpl->keepalive )
		gc->prpl->keepalive( gc );
	
	return TRUE;
}

void account_online( struct gaim_connection *gc )
{
	user_t *u;
	
	/* MSN servers sometimes redirect you to a different server and do
	   the whole login sequence again, so subsequent calls to this
	   function should be handled correctly. (IOW, ignored) */
	if( gc->flags & OPT_LOGGED_IN )
		return;
	
	u = user_find( gc->irc, gc->irc->nick );
	
	serv_got_crap( gc, "Logged in" );
	
	gc->keepalive = g_timeout_add( 60000, send_keepalive, gc );
	gc->flags |= OPT_LOGGED_IN;
	
	if( u && u->away ) proto_away( gc, u->away );
	
	if( gc->protocol == PROTO_ICQ )
	{
		for( u = gc->irc->users; u; u = u->next )
			if( u->gc == gc )
				break;
		
		if( u == NULL )
			serv_got_crap( gc, "\x02""***\x02"" BitlBee now supports ICQ server-side contact lists. "
			                      "See \x02""help import_buddies\x02"" for more information." );
	}
}

gboolean auto_reconnect( gpointer data )
{
	account_t *a = data;
	
	a->reconnect = 0;
	account_on( a->irc, a );
	
	return( FALSE );	/* Only have to run the timeout once */
}

void cancel_auto_reconnect( account_t *a )
{
	while( g_source_remove_by_user_data( (gpointer) a ) );
	a->reconnect = 0;
}

void account_offline( struct gaim_connection *gc )
{
	gc->wants_to_die = TRUE;
	signoff( gc );
}

void signoff( struct gaim_connection *gc )
{
	irc_t *irc = gc->irc;
	user_t *t, *u = irc->users;
	account_t *a;
	
	serv_got_crap( gc, "Signing off.." );

	gaim_input_remove( gc->keepalive );
	gc->keepalive = 0;
	gc->prpl->close( gc );
	gaim_input_remove( gc->inpa );
	
	while( u )
	{
		if( u->gc == gc )
		{
			t = u->next;
			user_del( irc, u->nick );
			u = t;
		}
		else
			u = u->next;
	}
	
	query_del_by_gc( gc->irc, gc );
	
	for( a = irc->accounts; a; a = a->next )
		if( a->gc == gc )
			break;
	
	if( !a )
	{
		/* Uhm... This is very sick. */
	}
	else if( !gc->wants_to_die && set_getint( irc, "auto_reconnect" ) )
	{
		int delay = set_getint( irc, "auto_reconnect_delay" );
		serv_got_crap( gc, "Reconnecting in %d seconds..", delay );
		
		a->reconnect = 1;
		g_timeout_add( delay * 1000, auto_reconnect, a );
	}
	
	destroy_gaim_conn( gc );
}


/* dialogs.c */

void do_error_dialog( struct gaim_connection *gc, char *msg, char *title )
{
	serv_got_crap( gc, "Error: %s", msg );
}

void do_ask_dialog( struct gaim_connection *gc, char *msg, void *data, void *doit, void *dont )
{
	query_add( gc->irc, gc, msg, doit, dont, data );
}


/* list.c */

int bud_list_cache_exists( struct gaim_connection *gc )
{
	return( 0 );
}

void do_import( struct gaim_connection *gc, void *null )
{
	return;
}

void add_buddy( struct gaim_connection *gc, char *group, char *handle, char *realname )
{
	user_t *u;
	char nick[MAX_NICK_LENGTH+1];
	char *s;
	irc_t *irc = gc->irc;
	
	if( set_getint( irc, "debug" ) && 0 ) /* This message is too useless */
		serv_got_crap( gc, "Receiving user add from handle: %s", handle );
	
	if( user_findhandle( gc, handle ) )
	{
		if( set_getint( irc, "debug" ) )
			serv_got_crap( gc, "User already exists, ignoring add request: %s", handle );
		
		return;
		
		/* Buddy seems to exist already. Let's ignore this request then... */
	}
	
	memset( nick, 0, MAX_NICK_LENGTH + 1 );
	strcpy( nick, nick_get( gc->irc, handle, gc->protocol, realname ) );
	
	u = user_add( gc->irc, nick );
	
	if( !realname || !*realname ) realname = nick;
	u->realname = g_strdup( realname );
	
	if( ( s = strchr( handle, '@' ) ) )
	{
		u->host = g_strdup( s + 1 );
		u->user = g_strndup( handle, s - handle );
	}
	else if( gc->user->proto_opt[0] && *gc->user->proto_opt[0] )
	{
		u->host = g_strdup( gc->user->proto_opt[0] );
		u->user = g_strdup( handle );
		
		/* s/ /_/ ... important for AOL screennames */
		for( s = u->user; *s; s ++ )
			if( *s == ' ' )
				*s = '_';
	}
	else
	{
		u->host = g_strdup( proto_name[gc->user->protocol] );
		u->user = g_strdup( handle );
	}
	
	u->gc = gc;
	u->handle = g_strdup( handle );
	u->send_handler = buddy_send_handler;
	u->last_typing_notice = 0;
}

struct buddy *find_buddy( struct gaim_connection *gc, char *handle )
{
	static struct buddy b[1];
	user_t *u;
	
	u = user_findhandle( gc, handle );
	
	if( !u )
		return( NULL );

	memset( b, 0, sizeof( b ) );
	strncpy( b->name, handle, 80 );
	strncpy( b->show, u->realname, BUDDY_ALIAS_MAXLEN );
	b->present = u->online;
	b->gc = u->gc;
	
	return( b );
}

void do_export( struct gaim_connection *gc )
{
	return;
}

void signoff_blocked( struct gaim_connection *gc )
{
	return; /* Make all blocked users look invisible (TODO?) */
}


void serv_buddy_rename( struct gaim_connection *gc, char *handle, char *realname )
{
	user_t *u = user_findhandle( gc, handle );
	char *name, buf[1024];
	
	if( !u ) return;
	
	/* Convert all UTF-8 */
	if( g_strncasecmp( set_getstr( gc->irc, "charset" ), "none", 4 ) != 0 &&
	    do_iconv( "UTF-8", set_getstr( gc->irc, "charset" ), realname, buf, 0, sizeof( buf ) ) != -1 )
		name = buf;
	else
		name = realname;
	
	if( g_strcasecmp( u->realname, name ) != 0 )
	{
		if( u->realname != u->nick ) g_free( u->realname );
		
		u->realname = g_strdup( name );
		
		if( ( gc->flags & OPT_LOGGED_IN ) && set_getint( gc->irc, "display_namechanges" ) )
			serv_got_crap( gc, "User `%s' changed name to `%s'", u->nick, u->realname );
	}
}


/* prpl.c */

void show_got_added( struct gaim_connection *gc, char *id, char *handle, const char *realname, const char *msg )
{
	return;
}


/* server.c */                    

void serv_got_update( struct gaim_connection *gc, char *handle, int loggedin, int evil, time_t signon, time_t idle, int type, guint caps )
{
	user_t *u;
	int oa, oo;
	
	u = user_findhandle( gc, handle );
	
	if( !u )
	{
		if( g_strcasecmp( set_getstr( gc->irc, "handle_unknown" ), "add" ) == 0 )
		{
			add_buddy( gc, NULL, handle, NULL );
			u = user_findhandle( gc, handle );
		}
		else
		{
			if( set_getint( gc->irc, "debug" ) || g_strcasecmp( set_getstr( gc->irc, "handle_unknown" ), "ignore" ) != 0 )
			{
				serv_got_crap( gc, "serv_got_update() for handle %s:", handle );
				serv_got_crap( gc, "loggedin = %d, type = %d", loggedin, type );
			}
			
			return;
		}
		return;
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
		irc_spawn( gc->irc, u );
		u->online = 1;
	}
	else if( !loggedin && u->online )
	{
		struct conversation *c;
		
		irc_kill( gc->irc, u );
		u->online = 0;
		
		/* Remove him/her from the conversations to prevent PART messages after he/she QUIT already */
		for( c = gc->conversations; c; c = c->next )
			remove_chat_buddy_silent( c, handle );
	}
	
	if( ( type & UC_UNAVAILABLE ) && ( gc->protocol == PROTO_OSCAR || gc->protocol == PROTO_TOC ) )
	{
		u->away = g_strdup( "Away" );
	}
	else if( ( type & UC_UNAVAILABLE ) && ( gc->protocol == PROTO_JABBER ) )
	{
		if( type & UC_DND )
			u->away = g_strdup( "Do Not Disturb" );
		else if( type & UC_XA )
			u->away = g_strdup( "Extended Away" );
		else // if( type & UC_AWAY )
			u->away = g_strdup( "Away" );
	}
	else if( ( type & UC_UNAVAILABLE ) && gc->prpl->get_status_string )
	{
		u->away = g_strdup( gc->prpl->get_status_string( gc, type ) );
	}
	else
		u->away = NULL;
	
	/* LISPy... */
	if( ( set_getint( gc->irc, "away_devoice" ) ) &&		/* Don't do a thing when user doesn't want it */
	    ( u->online ) &&						/* Don't touch offline people */
	    ( ( ( u->online != oo ) && !u->away ) ||			/* Voice joining people */
	      ( ( u->online == oo ) && ( oa == !u->away ) ) ) )		/* (De)voice people changing state */
	{
		irc_write( gc->irc, ":%s!%s@%s MODE %s %cv %s", gc->irc->mynick, gc->irc->mynick, gc->irc->myhost,
		                                                gc->irc->channel, u->away?'-':'+', u->nick );
	}
}

void serv_got_im( struct gaim_connection *gc, char *handle, char *msg, guint32 flags, time_t mtime, gint len )
{
	irc_t *irc = gc->irc;
	user_t *u;
	char buf[8192];
	
	u = user_findhandle( gc, handle );
	
	if( !u )
	{
		char *h = set_getstr( irc, "handle_unknown" );
		
		if( g_strcasecmp( h, "ignore" ) == 0 )
		{
			if( set_getint( irc, "debug" ) )
				serv_got_crap( gc, "Ignoring message from unknown handle %s", handle );
			
			return;
		}
		else if( g_strncasecmp( h, "add", 3 ) == 0 )
		{
			int private = set_getint( irc, "private" );
			
			if( h[3] )
			{
				if( g_strcasecmp( h + 3, "_private" ) == 0 )
					private = 1;
				else if( g_strcasecmp( h + 3, "_channel" ) == 0 )
					private = 0;
			}
			
			add_buddy( gc, NULL, handle, NULL );
			u = user_findhandle( gc, handle );
			u->is_private = private;
		}
		else
		{
			serv_got_crap( gc, "Message from unknown handle %s:", handle );
			u = user_find( irc, irc->mynick );
		}
	}
	
	if( ( g_strcasecmp( set_getstr( gc->irc, "strip_html" ), "always" ) == 0 ) ||
	    ( ( gc->flags & OPT_CONN_HTML ) && set_getint( gc->irc, "strip_html" ) ) )
		strip_html( msg );

	if( g_strncasecmp( set_getstr( irc, "charset" ), "none", 4 ) != 0 &&
	    do_iconv( "UTF-8", set_getstr( irc, "charset" ), msg, buf, 0, 8192 ) != -1 )
		msg = buf;
	
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

void serv_got_typing( struct gaim_connection *gc, char *handle, int timeout )
{
	user_t *u;
	
	if( !set_getint( gc->irc, "typing_notice" ) )
		return;
	
	if( ( u = user_findhandle( gc, handle ) ) )
		irc_msgfrom( gc->irc, u->nick, "\1TYPING 1\1" );
}

void serv_got_chat_left( struct gaim_connection *gc, int id )
{
	struct conversation *c, *l = NULL;
	GList *ir;
	
	if( set_getint( gc->irc, "debug" ) )
		serv_got_crap( gc, "You were removed from conversation %d", (int) id );
	
	for( c = gc->conversations; c && c->id != id; c = (l=c)->next );
	
	if( c )
	{
		if( c->joined )
		{
			user_t *u, *r;
			
			r = user_find( gc->irc, gc->irc->mynick );
			irc_privmsg( gc->irc, r, "PRIVMSG", c->channel, "", "Cleaning up channel, bye!" );
			
			u = user_find( gc->irc, gc->irc->nick );
			irc_kick( gc->irc, u, c->channel, r );
			/* irc_part( gc->irc, u, c->channel ); */
		}
		
		if( l )
			l->next = c->next;
		else
			gc->conversations = c->next;
		
		for( ir = c->in_room; ir; ir = ir->next )
			g_free( ir->data );
		g_list_free( c->in_room );
		g_free( c->channel );
		g_free( c->title );
		g_free( c );
	}
}

void serv_got_chat_in( struct gaim_connection *gc, int id, char *who, int whisper, char *msg, time_t mtime )
{
	struct conversation *c;
	user_t *u;
	char buf[8192];
	
	/* Gaim sends own messages through this too. IRC doesn't want this, so kill them */
	if( g_strcasecmp( who, gc->user->username ) == 0 )
		return;
	
	u = user_findhandle( gc, who );
	for( c = gc->conversations; c && c->id != id; c = c->next );
	
	if( ( g_strcasecmp( set_getstr( gc->irc, "strip_html" ), "always" ) == 0 ) ||
	    ( ( gc->flags & OPT_CONN_HTML ) && set_getint( gc->irc, "strip_html" ) ) )
		strip_html( msg );
	
	if( g_strncasecmp( set_getstr( gc->irc, "charset" ), "none", 4 ) != 0 &&
	    do_iconv( "UTF-8", set_getstr( gc->irc, "charset" ), msg, buf, 0, 8192 ) != -1 )
		msg = buf;
	
	if( c && u )
		irc_privmsg( gc->irc, u, "PRIVMSG", c->channel, "", msg );
	else
		serv_got_crap( gc, "Message from/to conversation %s@%d (unknown conv/user): %s", who, id, msg );
}

struct conversation *serv_got_joined_chat( struct gaim_connection *gc, int id, char *handle )
{
	struct conversation *c;
	char *s;
	
	/* This one just creates the conversation structure, user won't see anything yet */
	
	if( gc->conversations )
	{
		for( c = gc->conversations; c->next; c = c->next );
		c = c->next = g_new0( struct conversation, 1 );
	}
	else
		gc->conversations = c = g_new0( struct conversation, 1);
	
	c->id = id;
	c->gc = gc;
	c->title = g_strdup( handle );
	
	s = g_new( char, 16 );
	sprintf( s, "&chat_%03d", gc->irc->c_id++ );
	c->channel = g_strdup( s );
	g_free( s );
	
	if( set_getint( gc->irc, "debug" ) )
		serv_got_crap( gc, "Creating new conversation: (id=%d,handle=%s)", id, handle );
	
	return( c );
}

void serv_finish_login( struct gaim_connection *gc )
{
	return;
}


/* buddy_chat.c */

void add_chat_buddy( struct conversation *b, char *handle )
{
	user_t *u = user_findhandle( b->gc, handle );
	int me = 0;
	
	if( set_getint( b->gc->irc, "debug" ) )
		serv_got_crap( b->gc, "User %s added to conversation %d", handle, b->id );
	
	/* It might be yourself! */
	if( b->gc->prpl->cmp_buddynames( handle, b->gc->user->username ) == 0 )
	{
		u = user_find( b->gc->irc, b->gc->irc->nick );
		if( !b->joined )
			irc_join( b->gc->irc, u, b->channel );
		b->joined = me = 1;
	}
	
	/* Most protocols allow people to join, even when they're not in
	   your contact list. Try to handle that here */
	if( !u )
	{
		add_buddy( b->gc, NULL, handle, NULL );
		u = user_findhandle( b->gc, handle );
	}
	
	/* Add the handle to the room userlist, if it's not 'me' */
	if( !me )
	{
		if( b->joined )
			irc_join( b->gc->irc, u, b->channel );
		b->in_room = g_list_append( b->in_room, g_strdup( handle ) );
	}
}

void remove_chat_buddy( struct conversation *b, char *handle, char *reason )
{
	user_t *u;
	int me = 0;
	
	if( set_getint( b->gc->irc, "debug" ) )
		serv_got_crap( b->gc, "User %s removed from conversation %d (%s)", handle, b->id, reason ? reason : "" );
	
	/* It might be yourself! */
	if( g_strcasecmp( handle, b->gc->user->username ) == 0 )
	{
		u = user_find( b->gc->irc, b->gc->irc->nick );
		b->joined = 0;
		me = 1;
	}
	else
	{
		u = user_findhandle( b->gc, handle );
	}
	
	if( remove_chat_buddy_silent( b, handle ) )
		if( ( b->joined || me ) && u )
			irc_part( b->gc->irc, u, b->channel );
}

static int remove_chat_buddy_silent( struct conversation *b, char *handle )
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


/* prefs.c */

/* Necessary? */
void build_block_list()
{
	return;
}

void build_allow_list()
{
	return;
}


/* Misc. BitlBee stuff which shouldn't really be here */

struct conversation *conv_findchannel( char *channel )
{
	struct gaim_connection *gc;
	struct conversation *c;
	GSList *l;
	
	/* This finds the connection which has a conversation which belongs to this channel */
	for( l = connections; l; l = l->next )
	{
		gc = l->data;
		for( c = gc->conversations; c && g_strcasecmp( c->channel, channel ) != 0; c = c->next );
		if( c )
			return( c );
	}
	
	return( NULL );
}

char *set_eval_away_devoice( irc_t *irc, set_t *set, char *value )
{
	int st;
	
	if( ( g_strcasecmp( value, "true" ) == 0 ) || ( g_strcasecmp( value, "yes" ) == 0 ) || ( g_strcasecmp( value, "on" ) == 0 ) )
		st = 1;
	else if( ( g_strcasecmp( value, "false" ) == 0 ) || ( g_strcasecmp( value, "no" ) == 0 ) || ( g_strcasecmp( value, "off" ) == 0 ) )
		st = 0;
	else if( sscanf( value, "%d", &st ) != 1 )
		return( NULL );
	
	st = st != 0;
	
	/* Horror.... */
	
	if( st != set_getint( irc, "away_devoice" ) )
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
			if( u->gc && u->online && !u->away )
			{
				if( ( strlen( list ) + strlen( u->nick ) ) >= 79 )
				{
					for( i = 0; i < count; v[i++] = 'v' ); v[i] = 0;
					irc_write( irc, ":%s!%s@%s MODE %s %c%s%s",
					           irc->mynick, irc->mynick, irc->myhost,
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
		irc_write( irc, ":%s!%s@%s MODE %s %c%s%s", irc->mynick, irc->mynick, irc->myhost,
		                                            irc->channel, pm, v, list );
	}
	
	return( set_eval_bool( irc, set, value ) );
}

int serv_send_im( irc_t *irc, user_t *u, char *msg, int flags )
{
	char buf[8192];
	
	if( g_strncasecmp( set_getstr( irc, "charset" ), "none", 4 ) != 0 &&
	    do_iconv( set_getstr( irc, "charset" ), "UTF-8", msg, buf, 0, 8192 ) != -1 )
		msg = buf;

	if( ( u->gc->flags & OPT_CONN_HTML ) && ( g_strncasecmp( msg, "<html>", 6 ) != 0 ) )
	{
		char *html;
		
		html = escape_html( msg );
		strncpy( buf, html, 8192 );
		g_free( html );
		
		msg = buf;
	}
	
	return( ((struct gaim_connection *)u->gc)->prpl->send_im( u->gc, u->handle, msg, strlen( msg ), flags ) );
}

int serv_send_chat( irc_t *irc, struct gaim_connection *gc, int id, char *msg )
{
	char buf[8192];
	
	if( g_strncasecmp( set_getstr( irc, "charset" ), "none", 4 ) != 0 &&
	    do_iconv( set_getstr( irc, "charset" ), "UTF-8", msg, buf, 0, 8192 ) != -1 )
		msg = buf;

	if( gc->flags & OPT_CONN_HTML) {
		char * html = escape_html(msg);
		strncpy(buf, html, 8192);
		g_free(html);
	}
	
	return( gc->prpl->chat_send( gc, id, msg ) );
}

/* Convert from one charset to another.
   
   from_cs, to_cs: Source and destination charsets
   src, dst: Source and destination strings
   size: Size if src. 0 == use strlen(). strlen() is not reliable for UNICODE/UTF16 strings though.
   maxbuf: Maximum number of bytes to write to dst
   
   Returns the number of bytes written to maxbuf or -1 on an error.
*/
signed int do_iconv( char *from_cs, char *to_cs, char *src, char *dst, size_t size, size_t maxbuf )
{
	iconv_t cd;
	size_t res;
	size_t inbytesleft, outbytesleft;
	char *inbuf = src;
	char *outbuf = dst;
	
	cd = iconv_open( to_cs, from_cs );
	if( cd == (iconv_t) -1 )
		return( -1 );
	
	inbytesleft = size ? size : strlen( src );
	outbytesleft = maxbuf - 1;
	res = iconv( cd, &inbuf, &inbytesleft, &outbuf, &outbytesleft );
	*outbuf = '\0';
	iconv_close( cd );
	
	if( res == (size_t) -1 )
		return( -1 );
	else
		return( outbuf - dst );
}

char *set_eval_charset( irc_t *irc, set_t *set, char *value )
{
	iconv_t cd;

	if ( g_strncasecmp( value, "none", 4 ) == 0 )
		return( value );

	cd = iconv_open( "UTF-8", value );
	if( cd == (iconv_t) -1 )
		return( NULL );

	iconv_close( cd );
	return( value );
}
