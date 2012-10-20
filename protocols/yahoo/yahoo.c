/*
 * libyahoo2 wrapper to BitlBee
 *
 * Mostly Copyright 2004-2010 Wilmer van der Gaast <wilmer@gaast.net>
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
	struct groupchat *c;
	int yid;
	YList *members;
	struct im_connection *ic;
};

static GSList *byahoo_inputs = NULL;
static int byahoo_chat_id = 0;

static char *byahoo_strip( const char *in )
{
	int len;
	
	/* This should get rid of the markup noise at the beginning of the string. */
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
			const char *s;
			
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
	
	/* This is supposed to get rid of the noise at the end of the line. */
	len = strlen( in );
	while( len > 0 && ( in[len-1] == '>' || in[len-1] == 'm' ) )
	{
		int blen = len;
		const char *search;
		
		if( in[len-1] == '>' )
			search = "</";
		else
			search = "\e[";
		
		len -= 3;
		while( len > 0 && strncmp( in + len, search, 2 ) != 0 )
			len --;
		
		if( len <= 0 && strncmp( in, search, 2 ) != 0 )
		{
			len = blen;
			break;
		}
	}
	
	return( g_strndup( in, len ) );
}

static void byahoo_init( account_t *acc )
{
	set_add( &acc->set, "mail_notifications", "false", set_eval_bool, acc );
	
	acc->flags |= ACC_FLAG_AWAY_MESSAGE | ACC_FLAG_STATUS_MESSAGE;
}

static void byahoo_login( account_t *acc )
{
	struct im_connection *ic = imcb_new( acc );
	struct byahoo_data *yd = ic->proto_data = g_new0( struct byahoo_data, 1 );
	char *s;
	
	yd->logged_in = FALSE;
	yd->current_status = YAHOO_STATUS_AVAILABLE;
	
	if( ( s = strchr( acc->user, '@' ) ) && g_strcasecmp( s, "@yahoo.com" ) == 0 )
		imcb_error( ic, "Your Yahoo! username should just be a username. "
		                "Do not include any @domain part." );
	
	imcb_log( ic, "Connecting" );
	yd->y2_id = yahoo_init( acc->user, acc->pass );
	yahoo_login( yd->y2_id, yd->current_status );
}

static void byahoo_logout( struct im_connection *ic )
{
	struct byahoo_data *yd = (struct byahoo_data *) ic->proto_data;
	GSList *l;
	
	while( ic->groupchats )
		imcb_chat_free( ic->groupchats->data );
	
	for( l = yd->buddygroups; l; l = l->next )
	{
		struct byahoo_buddygroups *bg = l->data;
		
		g_free( bg->buddy );
		g_free( bg->group );
		g_free( bg );
	}
	g_slist_free( yd->buddygroups );
	
	yahoo_logoff( yd->y2_id );
	
	g_free( yd );
}

static void byahoo_get_info(struct im_connection *ic, char *who) 
{
	/* Just make an URL and let the user fetch the info */
	imcb_log(ic, "%s\n%s: %s%s", _("User Info"), 
			_("For now, fetch yourself"), yahoo_get_profile_url(),
			who);
}

static int byahoo_buddy_msg( struct im_connection *ic, char *who, char *what, int flags )
{
	struct byahoo_data *yd = ic->proto_data;
	
	yahoo_send_im( yd->y2_id, NULL, who, what, 1, 0 );
	
	return 1;
}

static int byahoo_send_typing( struct im_connection *ic, char *who, int typing )
{
	struct byahoo_data *yd = ic->proto_data;
	
	yahoo_send_typing( yd->y2_id, NULL, who, ( typing & OPT_TYPING ) != 0 );
	
	return 1;
}

static void byahoo_set_away( struct im_connection *ic, char *state, char *msg )
{
	struct byahoo_data *yd = (struct byahoo_data *) ic->proto_data;
	
	if( state && msg == NULL )
	{
		/* Use these states only if msg doesn't contain additional
		   info since away messages are only supported with CUSTOM. */
		if( g_strcasecmp( state, "Be Right Back" ) == 0 )
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
		else
			yd->current_status = YAHOO_STATUS_CUSTOM;
	}
	else if( msg )
		yd->current_status = YAHOO_STATUS_CUSTOM;
	else
		yd->current_status = YAHOO_STATUS_AVAILABLE;
	
	yahoo_set_away( yd->y2_id, yd->current_status, msg, state ? 2 : 0 );
}

static GList *byahoo_away_states( struct im_connection *ic )
{
	static GList *m = NULL;

	if( m == NULL )
	{
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
	}
	
	return m;
}

static void byahoo_keepalive( struct im_connection *ic )
{
	struct byahoo_data *yd = ic->proto_data;
	
	yahoo_keepalive( yd->y2_id );
}

static void byahoo_add_buddy( struct im_connection *ic, char *who, char *group )
{
	struct byahoo_data *yd = (struct byahoo_data *) ic->proto_data;
	bee_user_t *bu;
	
	if( group && ( bu = bee_user_by_handle( ic->bee, ic, who ) ) && bu->group )
	{
		GSList *bgl;
		
		/* If the person is in our list already, this is a group change. */
		yahoo_change_buddy_group( yd->y2_id, who, bu->group->name, group );
		
		/* No idea how often people have people in multiple groups and
		   BitlBee doesn't currently support this anyway .. but keep
		   this struct up-to-date for now. */
		for( bgl = yd->buddygroups; bgl; bgl = bgl->next )
		{
			struct byahoo_buddygroups *bg = bgl->data;
			
			if( g_strcasecmp( bg->buddy, who ) == 0 &&
			    g_strcasecmp( bg->group, bu->group->name ) == 0 )
			{
				g_free( bg->group );
				bg->group = g_strdup( group );
			}
		}
	}
	else
		yahoo_add_buddy( yd->y2_id, who, group ? group : BYAHOO_DEFAULT_GROUP, NULL );
}

static void byahoo_remove_buddy( struct im_connection *ic, char *who, char *group )
{
	struct byahoo_data *yd = (struct byahoo_data *) ic->proto_data;
	GSList *bgl;
	
	yahoo_remove_buddy( yd->y2_id, who, BYAHOO_DEFAULT_GROUP );
	
	for( bgl = yd->buddygroups; bgl; bgl = bgl->next )
	{
		struct byahoo_buddygroups *bg = bgl->data;
		
		if( g_strcasecmp( bg->buddy, who ) == 0 )
			yahoo_remove_buddy( yd->y2_id, who, bg->group );
	}
}

static void byahoo_chat_msg( struct groupchat *c, char *message, int flags )
{
	struct byahoo_data *yd = (struct byahoo_data *) c->ic->proto_data;
	
	yahoo_conference_message( yd->y2_id, NULL, c->data, c->title, message, 1 );
}

static void byahoo_chat_invite( struct groupchat *c, char *who, char *msg )
{
	struct byahoo_data *yd = (struct byahoo_data *) c->ic->proto_data;
	
	yahoo_conference_invite( yd->y2_id, NULL, c->data, c->title, msg ? msg : "" );
}

static void byahoo_chat_leave( struct groupchat *c )
{
	struct byahoo_data *yd = (struct byahoo_data *) c->ic->proto_data;
	
	yahoo_conference_logoff( yd->y2_id, NULL, c->data, c->title );
	imcb_chat_free( c );
}

static struct groupchat *byahoo_chat_with( struct im_connection *ic, char *who )
{
	struct byahoo_data *yd = (struct byahoo_data *) ic->proto_data;
	struct groupchat *c;
	char *roomname;
	YList *members;
	
	roomname = g_strdup_printf( "%s-Bee-%d", ic->acc->user, byahoo_chat_id );
	
	c = imcb_chat_new( ic, roomname );
	imcb_chat_add_buddy( c, ic->acc->user );
	
	/* FIXME: Free this thing when the chat's destroyed. We can't *always*
	          do this because it's not always created here. */
	c->data = members = g_new0( YList, 1 );
	members->data = g_strdup( who );
	
	yahoo_conference_invite( yd->y2_id, NULL, members, roomname, "Please join my groupchat..." );
	
	g_free( roomname );
	
	return c;
}

static void byahoo_auth_allow( struct im_connection *ic, const char *who )
{
	struct byahoo_data *yd = (struct byahoo_data *) ic->proto_data;
	
	yahoo_confirm_buddy( yd->y2_id, who, 0, "" );
}

static void byahoo_auth_deny( struct im_connection *ic, const char *who )
{
	struct byahoo_data *yd = (struct byahoo_data *) ic->proto_data;
	
	yahoo_confirm_buddy( yd->y2_id, who, 1, "" );
}

void byahoo_initmodule( )
{
	struct prpl *ret = g_new0(struct prpl, 1);
	ret->name = "yahoo";
    ret->mms = 832;           /* this guess taken from libotr UPGRADING file */
	ret->init = byahoo_init;
	
	ret->login = byahoo_login;
	ret->keepalive = byahoo_keepalive;
	ret->logout = byahoo_logout;
	
	ret->buddy_msg = byahoo_buddy_msg;
	ret->get_info = byahoo_get_info;
	ret->away_states = byahoo_away_states;
	ret->set_away = byahoo_set_away;
	ret->add_buddy = byahoo_add_buddy;
	ret->remove_buddy = byahoo_remove_buddy;
	ret->send_typing = byahoo_send_typing;
	
	ret->chat_msg = byahoo_chat_msg;
	ret->chat_invite = byahoo_chat_invite;
	ret->chat_leave = byahoo_chat_leave;
	ret->chat_with = byahoo_chat_with;

	ret->handle_cmp = g_strcasecmp;
	
	ret->auth_allow = byahoo_auth_allow;
	ret->auth_deny = byahoo_auth_deny;
	
	register_protocol(ret);
}

static struct im_connection *byahoo_get_ic_by_id( int id )
{
	GSList *l;
	struct im_connection *ic;
	struct byahoo_data *yd;
	
	for( l = get_connections(); l; l = l->next )
	{
		ic = l->data;
		yd = ic->proto_data;
		
		if( strcmp( ic->acc->prpl->name, "yahoo" ) == 0 && yd->y2_id == id )
			return( ic );
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
	
	if( !byahoo_get_ic_by_id( d->id ) )
	{
		g_free( d );
		return;
	}
	
	d->callback( NULL + d->fd, 0, d->data );
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
	
	if( !byahoo_get_ic_by_id( d->id ) )
		/* WTF doesn't libyahoo clean this up? */
		return FALSE;
	
	yahoo_read_ready( d->id, NULL + d->fd, d->data );
	
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
	
	return yahoo_write_ready( d->id, NULL + d->fd, d->data );
}

void ext_yahoo_login_response( int id, int succ, const char *url )
{
	struct im_connection *ic = byahoo_get_ic_by_id( id );
	struct byahoo_data *yd = NULL;
	
	if( ic == NULL )
	{
		/* libyahoo2 seems to call this one twice when something
		   went wrong sometimes. Don't know why. Because we clean
		   up the connection on the first failure, the second
		   should be ignored. */
		
		return;
	}
	
	yd = (struct byahoo_data *) ic->proto_data;
	
	if( succ == YAHOO_LOGIN_OK )
	{
		imcb_connected( ic );
		
		yd->logged_in = TRUE;
	}
	else
	{
		char *errstr;
		int allow_reconnect = FALSE;
		
		yd->logged_in = FALSE;
		
		if( succ == YAHOO_LOGIN_UNAME )
			errstr = "Incorrect Yahoo! username";
		else if( succ == YAHOO_LOGIN_PASSWD )
			errstr = "Incorrect Yahoo! password";
		else if( succ == YAHOO_LOGIN_LOCK )
			errstr = "Yahoo! account locked";
		else if( succ == 1236 )
			errstr = "Yahoo! account locked or machine temporarily banned";
		else if( succ == YAHOO_LOGIN_DUPL )
			errstr = "Logged in on a different machine or device";
		else if( succ == YAHOO_LOGIN_SOCK )
		{
			errstr = "Socket problem";
			allow_reconnect = TRUE;
		}
		else
			errstr = "Unknown error";
		
		if( url && *url )
			imcb_error( ic, "Error %d (%s). See %s for more information.", succ, errstr, url );
		else
			imcb_error( ic, "Error %d (%s)", succ, errstr );
		
		imc_logout( ic, allow_reconnect );
	}
}

void ext_yahoo_got_buddies( int id, YList *buds )
{
	struct im_connection *ic = byahoo_get_ic_by_id( id );
	struct byahoo_data *yd = ic->proto_data;
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
		
		imcb_add_buddy( ic, b->id, b->group );
		imcb_rename_buddy( ic, b->id, b->real_name );
		
		bl = bl->next;
	}
}

void ext_yahoo_got_identities( int id, YList *ids )
{
}

void ext_yahoo_got_cookies( int id )
{
}

void ext_yahoo_status_changed( int id, const char *who, int stat, const char *msg, int away, int idle, int mobile )
{
	struct im_connection *ic = byahoo_get_ic_by_id( id );
	char *state_string = NULL;
	int flags = OPT_LOGGED_IN;
	
	if( away )
		flags |= OPT_AWAY;
	if( mobile )
		flags |= OPT_MOBILE;
	
	switch (stat)
	{
	case YAHOO_STATUS_BRB:
		state_string = "Be Right Back";
		break;
	case YAHOO_STATUS_BUSY:
		state_string = "Busy";
		break;
	case YAHOO_STATUS_NOTATHOME:
		state_string = "Not At Home";
		break;
	case YAHOO_STATUS_NOTATDESK:
		state_string = "Not At Desk";
		break;
	case YAHOO_STATUS_NOTINOFFICE:
		state_string = "Not In Office";
		break;
	case YAHOO_STATUS_ONPHONE:
		state_string = "On Phone";
		break;
	case YAHOO_STATUS_ONVACATION:
		state_string = "On Vacation";
		break;
	case YAHOO_STATUS_OUTTOLUNCH:
		state_string = "Out To Lunch";
		break;
	case YAHOO_STATUS_STEPPEDOUT:
		state_string = "Stepped Out";
		break;
	case YAHOO_STATUS_INVISIBLE:
		state_string = "Invisible";
		break;
	case YAHOO_STATUS_CUSTOM:
		state_string = "Away";
		break;
	case YAHOO_STATUS_IDLE:
		state_string = "Idle";
		break;
	case YAHOO_STATUS_OFFLINE:
		state_string = "Offline";
		flags = 0;
		break;
	}
	
	imcb_buddy_status( ic, who, flags, state_string, msg );
	
	if( stat == YAHOO_STATUS_IDLE )
		imcb_buddy_times( ic, who, 0, idle );
}

void ext_yahoo_got_buzz( int id, const char *me, const char *who, long tm )
{
}

void ext_yahoo_got_im( int id, const char *me, const char *who, const char *msg, long tm, int stat, int utf8 )
{
	struct im_connection *ic = byahoo_get_ic_by_id( id );
	char *m;
	
	if( msg )
	{
		m = byahoo_strip( msg );
		imcb_buddy_msg( ic, (char*) who, (char*) m, 0, 0 );
		g_free( m );
	}
}

void ext_yahoo_got_file( int id, const char *ignored, const char *who, const char *msg,
                         const char *fname, unsigned long fesize, char *trid )
{
	struct im_connection *ic = byahoo_get_ic_by_id( id );
	
	imcb_log( ic, "Got a file transfer (file = %s) from %s. Ignoring for now due to lack of support.", fname, who );
}

void ext_yahoo_got_ft_data( int id, const unsigned char *in, int len, void *data )
{
}

void ext_yahoo_file_transfer_done( int id, int result, void *data )
{
}

void ext_yahoo_typing_notify( int id, const char *ignored, const char *who, int stat )
{
	struct im_connection *ic = byahoo_get_ic_by_id( id );
	
	if( stat == 1 )
		imcb_buddy_typing( ic, (char*) who, OPT_TYPING );
	else
		imcb_buddy_typing( ic, (char*) who, 0 );
}

void ext_yahoo_system_message( int id, const char *me, const char *who, const char *msg )
{
	struct im_connection *ic = byahoo_get_ic_by_id( id );
	
	imcb_log( ic, "Yahoo! system message: %s", msg );
}

void ext_yahoo_webcam_invite( int id, const char *ignored, const char *from )
{
	struct im_connection *ic = byahoo_get_ic_by_id( id );
	
	imcb_log( ic, "Got a webcam invitation from %s. IRC+webcams is a no-no though...", from );
}

void ext_yahoo_error( int id, const char *err, int fatal, int num )
{
	struct im_connection *ic = byahoo_get_ic_by_id( id );
	
	imcb_error( ic, "%s", err );
}

/* TODO: Clear up the mess of inp and d structures */
int ext_yahoo_add_handler( int id, void *fd_, yahoo_input_condition cond, void *data )
{
	struct byahoo_input_data *inp = g_new0( struct byahoo_input_data, 1 );
	int fd = (long) fd_;
	
	if( cond == YAHOO_INPUT_READ )
	{
		struct byahoo_read_ready_data *d = g_new0( struct byahoo_read_ready_data, 1 );
		
		d->id = id;
		d->fd = fd;
		d->data = data;
		
		inp->d = d;
		d->tag = inp->h = b_input_add( fd, B_EV_IO_READ, (b_event_handler) byahoo_read_ready_callback, (gpointer) d );
	}
	else if( cond == YAHOO_INPUT_WRITE )
	{
		struct byahoo_write_ready_data *d = g_new0( struct byahoo_write_ready_data, 1 );
		
		d->id = id;
		d->fd = fd;
		d->data = data;
		
		inp->d = d;
		d->tag = inp->h = b_input_add( fd, B_EV_IO_WRITE, (b_event_handler) byahoo_write_ready_callback, (gpointer) d );
	}
	else
	{
		g_free( inp );
		return -1;
		/* Panic... */
	}
	
	byahoo_inputs = g_slist_append( byahoo_inputs, inp );
	return inp->h;
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

int ext_yahoo_connect_async( int id, const char *host, int port, yahoo_connect_callback callback, void *data, int use_ssl )
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
	
	return fd;
}

char *ext_yahoo_get_ip_addr( const char *domain )
{
	return NULL;
}

int ext_yahoo_write( void *fd, char *buf, int len )
{
	return write( (long) fd, buf, len );
}

int ext_yahoo_read( void *fd, char *buf, int len )
{
	return read( (long) fd, buf, len );
}

void ext_yahoo_close( void *fd )
{
	close( (long) fd );
}

void ext_yahoo_got_buddy_change_group( int id, const char *me, const char *who,
                                       const char *old_group, const char *new_group )
{
	struct im_connection *ic = byahoo_get_ic_by_id( id );
	
	imcb_add_buddy( ic, who, new_group );
}

static void byahoo_accept_conf( void *data )
{
	struct byahoo_conf_invitation *inv = data;
	struct groupchat *b = NULL;
	GSList *l;
	
	for( l = inv->ic->groupchats; l; l = l->next )
	{
		b = l->data;
		if( b == inv->c )
			break;
	}
	
	if( b != NULL )
	{
		yahoo_conference_logon( inv->yid, NULL, inv->members, inv->name );
		imcb_chat_add_buddy( inv->c, inv->ic->acc->user );
	}
	else
	{
		imcb_log( inv->ic, "Duplicate/corrupted invitation to `%s'.", inv->name );
	}
	
	g_free( inv->name );
	g_free( inv );
}

static void byahoo_reject_conf( void *data )
{
	struct byahoo_conf_invitation *inv = data;
	
	yahoo_conference_decline( inv->yid, NULL, inv->members, inv->name, "User rejected groupchat" );
	imcb_chat_free( inv->c );
	g_free( inv->name );
	g_free( inv );
}

void ext_yahoo_got_conf_invite( int id, const char *ignored,
                                const char *who, const char *room, const char *msg, YList *members )
{
	struct im_connection *ic = byahoo_get_ic_by_id( id );
	struct byahoo_conf_invitation *inv;
	char txt[1024];
	YList *m;
	
	if( g_strcasecmp( who, ic->acc->user ) == 0 )
		/* WTF, Yahoo! seems to echo these now? */
		return;
	
	inv = g_malloc( sizeof( struct byahoo_conf_invitation ) );
	memset( inv, 0, sizeof( struct byahoo_conf_invitation ) );
	inv->name = g_strdup( room );
	inv->c = imcb_chat_new( ic, (char*) room );
	inv->c->data = members;
	inv->yid = id;
	inv->members = members;
	inv->ic = ic;
	
	for( m = members; m; m = m->next )
		if( g_strcasecmp( m->data, ic->acc->user ) != 0 )
			imcb_chat_add_buddy( inv->c, m->data );
	
	g_snprintf( txt, 1024, "Got an invitation to chatroom %s from %s: %s", room, who, msg );
	
	imcb_ask( ic, txt, inv, byahoo_accept_conf, byahoo_reject_conf );
}

void ext_yahoo_conf_userdecline( int id, const char *ignored, const char *who, const char *room, const char *msg )
{
	struct im_connection *ic = byahoo_get_ic_by_id( id );
	
	imcb_log( ic, "Invite to chatroom %s rejected by %s: %s", room, who, msg );
}

void ext_yahoo_conf_userjoin( int id, const char *ignored, const char *who, const char *room )
{
	struct im_connection *ic = byahoo_get_ic_by_id( id );
	struct groupchat *c = bee_chat_by_title( ic->bee, ic, room );
	
	if( c )
		imcb_chat_add_buddy( c, (char*) who );
}

void ext_yahoo_conf_userleave( int id, const char *ignored, const char *who, const char *room )

{
	struct im_connection *ic = byahoo_get_ic_by_id( id );
	struct groupchat *c = bee_chat_by_title( ic->bee, ic, room );
	
	if( c )
		imcb_chat_remove_buddy( c, (char*) who, "" );
}

void ext_yahoo_conf_message( int id, const char *ignored, const char *who, const char *room, const char *msg, int utf8 )
{
	struct im_connection *ic = byahoo_get_ic_by_id( id );
	char *m = byahoo_strip( msg );
	struct groupchat *c = bee_chat_by_title( ic->bee, ic, room );
	
	if( c )
		imcb_chat_msg( c, (char*) who, (char*) m, 0, 0 );
	g_free( m );
}

void ext_yahoo_chat_cat_xml( int id, const char *xml )
{
}

void ext_yahoo_chat_join( int id, const char *who, const char *room, const char *topic, YList *members, void *fd )
{
}

void ext_yahoo_chat_userjoin( int id, const char *me, const char *room, struct yahoo_chat_member *who )
{
	free(who->id);
	free(who->alias);
	free(who->location);
        free(who);
}

void ext_yahoo_chat_userleave( int id, const char *me, const char *room, const char *who )
{
}

void ext_yahoo_chat_message( int id, const char *me, const char *who, const char *room, const char *msg, int msgtype, int utf8 )
{
}

void ext_yahoo_chat_yahoologout( int id, const char *me )
{
}

void ext_yahoo_chat_yahooerror( int id, const char *me )
{
}

void ext_yahoo_contact_added( int id, const char *myid, const char *who, const char *msg )
{
	struct im_connection *ic = byahoo_get_ic_by_id( id );
	
	imcb_ask_auth( ic, who, msg );
}

void ext_yahoo_rejected( int id, const char *who, const char *msg )
{
}

void ext_yahoo_game_notify( int id, const char *me, const char *who, int stat, const char *msg )
{
}

void ext_yahoo_mail_notify( int id, const char *from, const char *subj, int cnt )
{
	struct im_connection *ic = byahoo_get_ic_by_id( id );
	
	if( !set_getbool( &ic->acc->set, "mail_notifications" ) )
		; /* The user doesn't care. */
	else if( from && subj )
		imcb_log( ic, "Received e-mail message from %s with subject `%s'", from, subj );
	else if( cnt > 0 )
		imcb_log( ic, "Received %d new e-mails", cnt );
}

void ext_yahoo_webcam_invite_reply( int id, const char *me, const char *from, int accept )
{
}

void ext_yahoo_webcam_closed( int id, const char *who, int reason )
{
}

void ext_yahoo_got_search_result( int id, int found, int start, int total, YList *contacts )
{
}

void ext_yahoo_webcam_viewer( int id, const char *who, int connect )
{
}

void ext_yahoo_webcam_data_request( int id, int send )
{
}

int ext_yahoo_log( const char *fmt, ... )
{
	return( 0 );
}

void ext_yahoo_got_webcam_image( int id, const char * who, const unsigned char *image, unsigned int image_size, unsigned int real_size, unsigned int timestamp )
{
}

void ext_yahoo_got_ping( int id, const char *msg )
{
}

void ext_yahoo_got_buddyicon (int id, const char *me, const char *who, const char *url, int checksum) {}
void ext_yahoo_got_buddyicon_checksum (int id, const char *me,const char *who, int checksum) {}

void ext_yahoo_got_buddyicon_request(int id, const char *me, const char *who){}
void ext_yahoo_buddyicon_uploaded(int id, const char *url){}
