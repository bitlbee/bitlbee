/*
 * libyahoo2 wrapper to BitlBee
 *
 * Mostly Copyright 2004 Wilmer van der Gaast <wilmer@gaast.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */


#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sys/stat.h>
#include <ctype.h>
#include "nogaim.h"
#include "yahoo2.h"
#include "yahoo2_callbacks.h"

#define BYAHOO_DEFAULT_GROUP "Buddies"

/* A hack to handle removal of buddies not in the group "Buddies" correctly */
struct byahoo_buddygroups
{
	char *buddy;
	char *group;
};

struct byahoo_data
{
	int y2_id;
	int current_status;
	gboolean logged_in;
	GSList *buddygroups;
};

struct byahoo_input_data
{
	int h;
	void *d;
};

struct byahoo_conf_invitation
{
	char *name;
	struct conversation *c;
	int yid;
	YList *members;
	struct gaim_connection *gc;
};

static GSList *byahoo_inputs = NULL;
static int byahoo_chat_id = 0;

static char *byahoo_strip( char *in )
{
	int len;
	
	/* This should get rid of HTML tags at the beginning of the string. */
	while( *in )
	{
		if( g_strncasecmp( in, "<font", 5 ) == 0 ||
		    g_strncasecmp( in, "<fade", 5 ) == 0 ||
		    g_strncasecmp( in, "<alt", 4 ) == 0 )
		{
			char *s = strchr( in, '>' );
			if( !s )
				break;
			
			in = s + 1;
		}
		else if( strncmp( in, "\e[", 2 ) == 0 )
		{
			char *s;
			
			for( s = in + 2; *s && *s != 'm'; s ++ );
			
			if( *s != 'm' )
				break;
			
			in = s + 1;
		}
		else
		{
			break;
		}
	}
	
	/* This is supposed to get rid of the closing HTML tags at the end of the line. */
	len = strlen( in );
	while( len > 0 && in[len-1] == '>' )
	{
		int blen = len;
		
		len --;
		while( len > 0 && ( in[len] != '<' || in[len+1] != '/' ) )
			len --;
		
		if( len == 0 && ( in[len] != '<' || in[len+1] != '/' ) )
		{
			len = blen;
			break;
		}
	}
	
	return( g_strndup( in, len ) );
}

static void byahoo_login( struct aim_user *user )
{
	struct gaim_connection *gc = new_gaim_conn( user );
	struct byahoo_data *yd = gc->proto_data = g_new0( struct byahoo_data, 1 );
	
	yd->logged_in = FALSE;
	yd->current_status = YAHOO_STATUS_AVAILABLE;
	
	set_login_progress( gc, 1, "Connecting" );
	yd->y2_id = yahoo_init( user->username, user->password );
	yahoo_login( yd->y2_id, yd->current_status );
}

static void byahoo_close( struct gaim_connection *gc )
{
	struct byahoo_data *yd = (struct byahoo_data *) gc->proto_data;
	GSList *l;
	
	while( gc->conversations )
		serv_got_chat_left( gc, gc->conversations->id );
	
	for( l = yd->buddygroups; l; l = l->next )
	{
		struct byahoo_buddygroups *bg = l->data;
		
		g_free( bg->buddy );
		g_free( bg->group );
		g_free( bg );
	}
	g_slist_free( yd->buddygroups );
	
	if( yd->logged_in )
		yahoo_logoff( yd->y2_id );
	else
		yahoo_close( yd->y2_id );
	
	g_free( yd );
}

static void byahoo_get_info(struct gaim_connection *gc, char *who) 
{
	/* Just make an URL and let the user fetch the info */
	serv_got_crap(gc, "%s\n%s: %s%s", _("User Info"), 
			_("For now, fetch yourself"), yahoo_get_profile_url(),
			who);
}

static int byahoo_send_im( struct gaim_connection *gc, char *who, char *what, int len, int flags )
{
	struct byahoo_data *yd = gc->proto_data;
	
	yahoo_send_im( yd->y2_id, NULL, who, what, 1 );
	
	return 1;
}

static int byahoo_send_typing( struct gaim_connection *gc, char *who, int typing )
{
	struct byahoo_data *yd = gc->proto_data;
	
	yahoo_send_typing( yd->y2_id, NULL, who, typing );
	
	return 1;
}

static void byahoo_set_away( struct gaim_connection *gc, char *state, char *msg )
{
	struct byahoo_data *yd = (struct byahoo_data *) gc->proto_data;
	
	gc->away = NULL;
	
	if( msg )
	{
		yd->current_status = YAHOO_STATUS_CUSTOM;
		gc->away = "";
	}
	if( state )
	{
		gc->away = "";
		if( g_strcasecmp( state, "Available" ) == 0 )
		{
			yd->current_status = YAHOO_STATUS_AVAILABLE;
			gc->away = NULL;
		}
		else if( g_strcasecmp( state, "Be Right Back" ) == 0 )
			yd->current_status = YAHOO_STATUS_BRB;
		else if( g_strcasecmp( state, "Busy" ) == 0 )
			yd->current_status = YAHOO_STATUS_BUSY;
		else if( g_strcasecmp( state, "Not At Home" ) == 0 )
			yd->current_status = YAHOO_STATUS_NOTATHOME;
		else if( g_strcasecmp( state, "Not At Desk" ) == 0 )
			yd->current_status = YAHOO_STATUS_NOTATDESK;
		else if( g_strcasecmp( state, "Not In Office" ) == 0 )
			yd->current_status = YAHOO_STATUS_NOTINOFFICE;
		else if( g_strcasecmp( state, "On Phone" ) == 0 )
			yd->current_status = YAHOO_STATUS_ONPHONE;
		else if( g_strcasecmp( state, "On Vacation" ) == 0 )
			yd->current_status = YAHOO_STATUS_ONVACATION;
		else if( g_strcasecmp( state, "Out To Lunch" ) == 0 )
			yd->current_status = YAHOO_STATUS_OUTTOLUNCH;
		else if( g_strcasecmp( state, "Stepped Out" ) == 0 )
			yd->current_status = YAHOO_STATUS_STEPPEDOUT;
		else if( g_strcasecmp( state, "Invisible" ) == 0 )
			yd->current_status = YAHOO_STATUS_INVISIBLE;
		else if( g_strcasecmp( state, GAIM_AWAY_CUSTOM ) == 0 )
		{
			yd->current_status = YAHOO_STATUS_AVAILABLE;
			
			gc->away = NULL;
		}
	}
	else
		yd->current_status = YAHOO_STATUS_AVAILABLE;
	
	if( yd->current_status == YAHOO_STATUS_INVISIBLE )
		yahoo_set_away( yd->y2_id, yd->current_status, NULL, gc->away != NULL );
	else
		yahoo_set_away( yd->y2_id, yd->current_status, msg, gc->away != NULL );
}

static GList *byahoo_away_states( struct gaim_connection *gc )
{
	GList *m = NULL;

	m = g_list_append( m, "Available" );
	m = g_list_append( m, "Be Right Back" );
	m = g_list_append( m, "Busy" );
	m = g_list_append( m, "Not At Home" );
	m = g_list_append( m, "Not At Desk" );
	m = g_list_append( m, "Not In Office" );
	m = g_list_append( m, "On Phone" );
	m = g_list_append( m, "On Vacation" );
	m = g_list_append( m, "Out To Lunch" );
	m = g_list_append( m, "Stepped Out" );
	m = g_list_append( m, "Invisible" );
	m = g_list_append( m, GAIM_AWAY_CUSTOM );
	
	return m;
}

static void byahoo_keepalive( struct gaim_connection *gc )
{
	struct byahoo_data *yd = gc->proto_data;
	
	yahoo_keepalive( yd->y2_id );
}

static void byahoo_add_buddy( struct gaim_connection *gc, char *who )
{
	struct byahoo_data *yd = (struct byahoo_data *) gc->proto_data;
	
	yahoo_add_buddy( yd->y2_id, who, BYAHOO_DEFAULT_GROUP );
}

static void byahoo_remove_buddy( struct gaim_connection *gc, char *who, char *group )
{
	struct byahoo_data *yd = (struct byahoo_data *) gc->proto_data;
	GSList *bgl;
	
	yahoo_remove_buddy( yd->y2_id, who, BYAHOO_DEFAULT_GROUP );
	
	for( bgl = yd->buddygroups; bgl; bgl = bgl->next )
	{
		struct byahoo_buddygroups *bg = bgl->data;
		
		if( g_strcasecmp( bg->buddy, who ) == 0 )
			yahoo_remove_buddy( yd->y2_id, who, bg->group );
	}
}

static char *byahoo_get_status_string( struct gaim_connection *gc, int stat )
{
	enum yahoo_status a = stat >> 1;
	
	switch (a)
	{
	case YAHOO_STATUS_BRB:
		return "Be Right Back";
	case YAHOO_STATUS_BUSY:
		return "Busy";
	case YAHOO_STATUS_NOTATHOME:
		return "Not At Home";
	case YAHOO_STATUS_NOTATDESK:
		return "Not At Desk";
	case YAHOO_STATUS_NOTINOFFICE:
		return "Not In Office";
	case YAHOO_STATUS_ONPHONE:
		return "On Phone";
	case YAHOO_STATUS_ONVACATION:
		return "On Vacation";
	case YAHOO_STATUS_OUTTOLUNCH:
		return "Out To Lunch";
	case YAHOO_STATUS_STEPPEDOUT:
		return "Stepped Out";
	case YAHOO_STATUS_INVISIBLE:
		return "Invisible";
	case YAHOO_STATUS_CUSTOM:
		return "Away";
	case YAHOO_STATUS_IDLE:
		return "Idle";
	case YAHOO_STATUS_OFFLINE:
		return "Offline";
	case YAHOO_STATUS_NOTIFY:
		return "Notify";
	default:
		return "Away";
	}
}

static int byahoo_chat_send( struct gaim_connection *gc, int id, char *message )
{
	struct byahoo_data *yd = (struct byahoo_data *) gc->proto_data;
	struct conversation *c;
	
	for( c = gc->conversations; c && c->id != id; c = c->next );

	yahoo_conference_message( yd->y2_id, NULL, c->data, c->title, message, 1 );
	
	return( 0 );
}

static void byahoo_chat_invite( struct gaim_connection *gc, int id, char *msg, char *who )
{
	struct byahoo_data *yd = (struct byahoo_data *) gc->proto_data;
	struct conversation *c;
	
	for( c = gc->conversations; c && c->id != id; c = c->next );
	
	yahoo_conference_invite( yd->y2_id, NULL, c->data, c->title, msg );
}

static void byahoo_chat_leave( struct gaim_connection *gc, int id )
{
	struct byahoo_data *yd = (struct byahoo_data *) gc->proto_data;
	struct conversation *c;
	
	for( c = gc->conversations; c && c->id != id; c = c->next );
	
	yahoo_conference_logoff( yd->y2_id, NULL, c->data, c->title );
	serv_got_chat_left( gc, c->id );
}

static int byahoo_chat_open( struct gaim_connection *gc, char *who )
{
	struct byahoo_data *yd = (struct byahoo_data *) gc->proto_data;
	struct conversation *c;
	char *roomname;
	YList *members;
	
	roomname = g_new0( char, strlen( gc->username ) + 16 );
	g_snprintf( roomname, strlen( gc->username ) + 16, "%s-Bee-%d", gc->username, byahoo_chat_id );
	
	c = serv_got_joined_chat( gc, ++byahoo_chat_id, roomname );
	add_chat_buddy( c, gc->username );
	
	/* FIXME: Free this thing when the chat's destroyed. We can't *always*
	          do this because it's not always created here. */
	c->data = members = g_new0( YList, 1 );
	members->data = g_strdup( who );
	
	yahoo_conference_invite( yd->y2_id, NULL, members, roomname, "Please join my groupchat..." );
	
	g_free( roomname );
	
	return( 1 );
}

void byahoo_init( )
{
	struct prpl *ret = g_new0(struct prpl, 1);
	ret->name = "yahoo";
	
	ret->login = byahoo_login;
	ret->close = byahoo_close;
	ret->send_im = byahoo_send_im;
	ret->get_info = byahoo_get_info;
	ret->away_states = byahoo_away_states;
	ret->set_away = byahoo_set_away;
	ret->keepalive = byahoo_keepalive;
	ret->add_buddy = byahoo_add_buddy;
	ret->remove_buddy = byahoo_remove_buddy;
	ret->get_status_string = byahoo_get_status_string;
	ret->send_typing = byahoo_send_typing;
	
	ret->chat_send = byahoo_chat_send;
	ret->chat_invite = byahoo_chat_invite;
	ret->chat_leave = byahoo_chat_leave;
	ret->chat_open = byahoo_chat_open;
	ret->cmp_buddynames = g_strcasecmp;
	
	register_protocol(ret);
}

static struct gaim_connection *byahoo_get_gc_by_id( int id )
{
	GSList *l;
	struct gaim_connection *gc;
	struct byahoo_data *yd;
	
	for( l = get_connections(); l; l = l->next )
	{
		gc = l->data;
		yd = gc->proto_data;
		
		if( !strcmp(gc->prpl->name, "yahoo") && yd->y2_id == id )
			return( gc );
	}
	
	return( NULL );
}


/* Now it's callback time! */

struct byahoo_connect_callback_data
{
	int fd;
	yahoo_connect_callback callback;
	gpointer data;
	int id;
};

void byahoo_connect_callback( gpointer data, gint source, b_input_condition cond )
{
	struct byahoo_connect_callback_data *d = data;
	
	if( !byahoo_get_gc_by_id( d->id ) )
	{
		g_free( d );
		return;
	}
	
	d->callback( d->fd, 0, d->data );
	g_free( d );
}

struct byahoo_read_ready_data
{
	int id;
	int fd;
	int tag;
	gpointer data;
};

gboolean byahoo_read_ready_callback( gpointer data, gint source, b_input_condition cond )
{
	struct byahoo_read_ready_data *d = data;
	
	if( !byahoo_get_gc_by_id( d->id ) )
		/* WTF doesn't libyahoo clean this up? */
		return FALSE;
	
	yahoo_read_ready( d->id, d->fd, d->data );
	
	return TRUE;
}

struct byahoo_write_ready_data
{
	int id;
	int fd;
	int tag;
	gpointer data;
};

gboolean byahoo_write_ready_callback( gpointer data, gint source, b_input_condition cond )
{
	struct byahoo_write_ready_data *d = data;
	
	if( !byahoo_get_gc_by_id( d->id ) )
		/* WTF doesn't libyahoo clean this up? */
		return FALSE;
	
	yahoo_write_ready( d->id, d->fd, d->data );
	
	return FALSE;
}

void ext_yahoo_login_response( int id, int succ, char *url )
{
	struct gaim_connection *gc = byahoo_get_gc_by_id( id );
	struct byahoo_data *yd = NULL;
	
	if( gc == NULL )
	{
		/* libyahoo2 seems to call this one twice when something
		   went wrong sometimes. Don't know why. Because we clean
		   up the connection on the first failure, the second
		   should be ignored. */
		
		return;
	}
	
	yd = (struct byahoo_data *) gc->proto_data;
	
	if( succ == YAHOO_LOGIN_OK )
	{
		account_online( gc );
		
		yd->logged_in = TRUE;
	}
	else
	{
		char *errstr;
		char *s;
		
		yd->logged_in = FALSE;
		
		if( succ == YAHOO_LOGIN_UNAME )
			errstr = "Incorrect Yahoo! username";
		else if( succ == YAHOO_LOGIN_PASSWD )
			errstr = "Incorrect Yahoo! password";
		else if( succ == YAHOO_LOGIN_LOCK )
			errstr = "Yahoo! account locked";
		else if( succ == YAHOO_LOGIN_DUPL )
		{
			errstr = "Logged in on a different machine or device";
			gc->wants_to_die = TRUE;
		}
		else if( succ == YAHOO_LOGIN_SOCK )
			errstr = "Socket problem";
		else
			errstr = "Unknown error";
		
		if( url && *url )
		{
			s = g_malloc( strlen( "Error %d (%s). See %s for more information." ) + strlen( url ) + strlen( errstr ) + 16 );
			sprintf( s, "Error %d (%s). See %s for more information.", succ, errstr, url );
		}
		else
		{
			s = g_malloc( strlen( "Error %d (%s)" ) + strlen( errstr ) + 16 );
			sprintf( s, "Error %d (%s)", succ, errstr );
		}
		
		if( yd->logged_in )
			hide_login_progress_error( gc, s );
		else
			hide_login_progress( gc, s );
		
		g_free( s );
		
		signoff( gc );
	}
}

void ext_yahoo_got_buddies( int id, YList *buds )
{
	struct gaim_connection *gc = byahoo_get_gc_by_id( id );
	struct byahoo_data *yd = gc->proto_data;
	YList *bl = buds;
	
	while( bl )
	{
		struct yahoo_buddy *b = bl->data;
		struct byahoo_buddygroups *bg;
		
		if( strcmp( b->group, BYAHOO_DEFAULT_GROUP ) != 0 )
		{
			bg = g_new0( struct byahoo_buddygroups, 1 );
			
			bg->buddy = g_strdup( b->id );
			bg->group = g_strdup( b->group );
			yd->buddygroups = g_slist_append( yd->buddygroups, bg );
		}
		
		add_buddy( gc, b->group, b->id, b->real_name );
		bl = bl->next;
	}
}

void ext_yahoo_got_ignore( int id, YList *igns )
{
}

void ext_yahoo_got_identities( int id, YList *ids )
{
}

void ext_yahoo_got_cookies( int id )
{
}

void ext_yahoo_status_changed( int id, char *who, int stat, char *msg, int away )
{
	struct gaim_connection *gc = byahoo_get_gc_by_id( id );
	
	serv_got_update( gc, who, stat != YAHOO_STATUS_OFFLINE, 0, 0, 0,
	                 ( stat != YAHOO_STATUS_AVAILABLE ) | ( stat << 1 ), 0 );
}

void ext_yahoo_got_im( int id, char *who, char *msg, long tm, int stat, int utf8 )
{
	struct gaim_connection *gc = byahoo_get_gc_by_id( id );
	char *m = byahoo_strip( msg );
	
	serv_got_im( gc, who, m, 0, 0, strlen( m ) );
	g_free( m );
}

void ext_yahoo_got_file( int id, char *who, char *url, long expires, char *msg, char *fname, unsigned long fesize )
{
	struct gaim_connection *gc = byahoo_get_gc_by_id( id );
	
	serv_got_crap( gc, "Got a file transfer (file = %s) from %s. Ignoring for now due to lack of support.", fname, who );
}

void ext_yahoo_typing_notify( int id, char *who, int stat )
{
	struct gaim_connection *gc = byahoo_get_gc_by_id( id );
	if (stat == 1) {
		/* User is typing */
		serv_got_typing( gc, who, 1, 1 );
	}
	else {
		/* User stopped typing */
		serv_got_typing( gc, who, 1, 0 );
	}
}

void ext_yahoo_system_message( int id, char *msg )
{
	struct gaim_connection *gc = byahoo_get_gc_by_id( id );
	
	serv_got_crap( gc, "Yahoo! system message: %s", msg );
}

void ext_yahoo_webcam_invite( int id, char *from )
{
	struct gaim_connection *gc = byahoo_get_gc_by_id( id );
	
	serv_got_crap( gc, "Got a webcam invitation from %s. IRC+webcams is a no-no though...", from );
}

void ext_yahoo_error( int id, char *err, int fatal )
{
	struct gaim_connection *gc = byahoo_get_gc_by_id( id );
	
	if( fatal )
	{
		hide_login_progress_error( gc, err );
		signoff( gc );
	}
	else
	{
		do_error_dialog( gc, err, "Yahoo! error" );
	}
}

/* TODO: Clear up the mess of inp and d structures */
int ext_yahoo_add_handler( int id, int fd, yahoo_input_condition cond, void *data )
{
	struct byahoo_input_data *inp = g_new0( struct byahoo_input_data, 1 );
	
	if( cond == YAHOO_INPUT_READ )
	{
		struct byahoo_read_ready_data *d = g_new0( struct byahoo_read_ready_data, 1 );
		
		d->id = id;
		d->fd = fd;
		d->data = data;
		
		inp->d = d;
		d->tag = inp->h = b_input_add( fd, GAIM_INPUT_READ, (b_event_handler) byahoo_read_ready_callback, (gpointer) d );
	}
	else if( cond == YAHOO_INPUT_WRITE )
	{
		struct byahoo_write_ready_data *d = g_new0( struct byahoo_write_ready_data, 1 );
		
		d->id = id;
		d->fd = fd;
		d->data = data;
		
		inp->d = d;
		d->tag = inp->h = b_input_add( fd, GAIM_INPUT_WRITE, (b_event_handler) byahoo_write_ready_callback, (gpointer) d );
	}
	else
	{
		g_free( inp );
		return( -1 );
		/* Panic... */
	}
	
	byahoo_inputs = g_slist_append( byahoo_inputs, inp );
	return( inp->h );
}

void ext_yahoo_remove_handler( int id, int tag )
{
	struct byahoo_input_data *inp;
	GSList *l = byahoo_inputs;
	
	while( l )
	{
		inp = l->data;
		if( inp->h == tag )
		{
			g_free( inp->d );
			g_free( inp );
			byahoo_inputs = g_slist_remove( byahoo_inputs, inp );
			break;
		}
		l = l->next;
	}
	
	b_event_remove( tag );
}

int ext_yahoo_connect_async( int id, char *host, int port, yahoo_connect_callback callback, void *data )
{
	struct byahoo_connect_callback_data *d;
	int fd;
	
	d = g_new0( struct byahoo_connect_callback_data, 1 );
	if( ( fd = proxy_connect( host, port, (b_event_handler) byahoo_connect_callback, (gpointer) d ) ) < 0 )
	{
		g_free( d );
		return( fd );
	}
	d->fd = fd;
	d->callback = callback;
	d->data = data;
	d->id = id;
	
	return( fd );
}

/* Because we don't want asynchronous connects in BitlBee, and because
   libyahoo doesn't seem to use this one anyway, this one is now defunct. */
int ext_yahoo_connect(char *host, int port)
{
#if 0
	struct sockaddr_in serv_addr;
	static struct hostent *server;
	static char last_host[256];
	int servfd;
	char **p;

	if(last_host[0] || g_strcasecmp(last_host, host)!=0) {
		if(!(server = gethostbyname(host))) {
			return -1;
		}
		strncpy(last_host, host, 255);
	}

	if((servfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		return -1;
	}

	for (p = server->h_addr_list; *p; p++)
	{
		memset(&serv_addr, 0, sizeof(serv_addr));
		serv_addr.sin_family = AF_INET;
		memcpy(&serv_addr.sin_addr.s_addr, *p, server->h_length);
		serv_addr.sin_port = htons(port);

		if(connect(servfd, (struct sockaddr *) &serv_addr, 
					sizeof(serv_addr)) == -1) {
			return -1;
		} else {
			return servfd;
		}
	}

	closesocket(servfd);
#endif
	return -1;
}

static void byahoo_accept_conf( gpointer w, struct byahoo_conf_invitation *inv )
{
	yahoo_conference_logon( inv->yid, NULL, inv->members, inv->name );
	add_chat_buddy( inv->c, inv->gc->username );
	g_free( inv->name );
	g_free( inv );
}

static void byahoo_reject_conf( gpointer w, struct byahoo_conf_invitation *inv )
{
	yahoo_conference_decline( inv->yid, NULL, inv->members, inv->name, "User rejected groupchat" );
	serv_got_chat_left( inv->gc, inv->c->id );
	g_free( inv->name );
	g_free( inv );
}

void ext_yahoo_got_conf_invite( int id, char *who, char *room, char *msg, YList *members )
{
	struct gaim_connection *gc = byahoo_get_gc_by_id( id );
	struct byahoo_conf_invitation *inv;
	char txt[1024];
	YList *m;
	
	inv = g_malloc( sizeof( struct byahoo_conf_invitation ) );
	memset( inv, 0, sizeof( struct byahoo_conf_invitation ) );
	inv->name = g_strdup( room );
	inv->c = serv_got_joined_chat( gc, ++byahoo_chat_id, room );
	inv->c->data = members;
	inv->yid = id;
	inv->members = members;
	inv->gc = gc;
	
	for( m = members; m; m = m->next )
		if( g_strcasecmp( m->data, gc->username ) != 0 )
			add_chat_buddy( inv->c, m->data );
	
	g_snprintf( txt, 1024, "Got an invitation to chatroom %s from %s: %s", room, who, msg );
	
	do_ask_dialog( gc, txt, inv, byahoo_accept_conf, byahoo_reject_conf );
}

void ext_yahoo_conf_userdecline( int id, char *who, char *room, char *msg )
{
	struct gaim_connection *gc = byahoo_get_gc_by_id( id );
	
	serv_got_crap( gc, "Invite to chatroom %s rejected by %s: %s", room, who, msg );
}

void ext_yahoo_conf_userjoin( int id, char *who, char *room )
{
	struct gaim_connection *gc = byahoo_get_gc_by_id( id );
	struct conversation *c;
	
	for( c = gc->conversations; c && strcmp( c->title, room ) != 0; c = c->next );
	
	if( c )
		add_chat_buddy( c, who );
}

void ext_yahoo_conf_userleave( int id, char *who, char *room )
{
	struct gaim_connection *gc = byahoo_get_gc_by_id( id );
	struct conversation *c;
	
	for( c = gc->conversations; c && strcmp( c->title, room ) != 0; c = c->next );
	
	if( c )
		remove_chat_buddy( c, who, "" );
}

void ext_yahoo_conf_message( int id, char *who, char *room, char *msg, int utf8 )
{
	struct gaim_connection *gc = byahoo_get_gc_by_id( id );
	char *m = byahoo_strip( msg );
	struct conversation *c;
	
	for( c = gc->conversations; c && strcmp( c->title, room ) != 0; c = c->next );
	
	serv_got_chat_in( gc, c ? c->id : 0, who, 0, m, 0 );
	g_free( m );
}

void ext_yahoo_chat_cat_xml( int id, char *xml )
{
}

void ext_yahoo_chat_join( int id, char *room, char *topic, YList *members, int fd )
{
}

void ext_yahoo_chat_userjoin( int id, char *room, struct yahoo_chat_member *who )
{
}

void ext_yahoo_chat_userleave( int id, char *room, char *who )
{
}

void ext_yahoo_chat_message( int id, char *who, char *room, char *msg, int msgtype, int utf8 )
{
}

void ext_yahoo_chat_yahoologout( int id )
{
}

void ext_yahoo_chat_yahooerror( int id )
{
}

void ext_yahoo_contact_added( int id, char *myid, char *who, char *msg )
{
}

void ext_yahoo_rejected( int id, char *who, char *msg )
{
}

void ext_yahoo_game_notify( int id, char *who, int stat )
{
}

void ext_yahoo_mail_notify( int id, char *from, char *subj, int cnt )
{
	struct gaim_connection *gc = byahoo_get_gc_by_id( id );
	
	if( from && subj )
		serv_got_crap( gc, "Received e-mail message from %s with subject `%s'", from, subj );
	else if( cnt > 0 )
		serv_got_crap( gc, "Received %d new e-mails", cnt );
}

void ext_yahoo_webcam_invite_reply( int id, char *from, int accept )
{
}

void ext_yahoo_webcam_closed( int id, char *who, int reason )
{
}

void ext_yahoo_got_search_result( int id, int found, int start, int total, YList *contacts )
{
}

void ext_yahoo_webcam_viewer( int id, char *who, int connect )
{
}

void ext_yahoo_webcam_data_request( int id, int send )
{
}

int ext_yahoo_log( char *fmt, ... )
{
	return( 0 );
}

void ext_yahoo_got_webcam_image( int id, const char * who, const unsigned char *image, unsigned int image_size, unsigned int real_size, unsigned int timestamp )
{
}
