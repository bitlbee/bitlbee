/*
 *  skype.c - Skype plugin for BitlBee
 * 
 *  Copyright (c) 2007 by Miklos Vajna <vmiklos@frugalware.org>
 *
 *  Several ideas are used from the BitlBee Jabber plugin, which is
 *
 *  Copyright (c) 2006 by Wilmer van der Gaast <wilmer@gaast.net>
 * 
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, 
 *  USA.
 */

#include <stdio.h>
#include <poll.h>
#include <bitlbee.h>

#define SKYPE_PORT_DEFAULT "2727"

/*
 * Structures
 */

struct skype_data
{
	struct im_connection *ic;
	char *username;
	int fd;
	char *txq;
	int tx_len;
	int r_inpa, w_inpa;
	/* When we receive a new message id, we query the handle, then the
	 * body. Store the handle here so that we imcb_buddy_msg() when we got
	 * the body. */
	char *handle;
};

struct skype_away_state
{
	char *code;
	char *full_name;
};

struct skype_buddy_ask_data
{
	struct im_connection *ic;
	char *handle;
};

/*
 * Tables
 */

const struct skype_away_state skype_away_state_list[] =
{
	{ "ONLINE",  "Online" },
	{ "SKYPEME",  "Skype Me" },
	{ "AWAY",   "Away" },
	{ "NA",    "Not available" },
	{ "DND",      "Do Not Disturb" },
	{ "INVISIBLE",      "Invisible" },
	{ "OFFLINE",      "Offline" },
	{ NULL, NULL}
};

/*
 * Functions
 */

static void skype_init( account_t *acc )
{
	set_t *s;

	s = set_add( &acc->set, "port", SKYPE_PORT_DEFAULT, set_eval_int, acc );
	s->flags |= ACC_SET_OFFLINE_ONLY;

	s = set_add( &acc->set, "server", NULL, set_eval_account, acc );
	s->flags |= ACC_SET_NOSAVE | ACC_SET_OFFLINE_ONLY;
}

int skype_write( struct im_connection *ic, char *buf, int len )
{
	struct skype_data *sd = ic->proto_data;
	struct pollfd pfd[1];

	pfd[0].fd = sd->fd;
	pfd[0].events = POLLOUT;

	/* This poll is necessary or we'll get a SIGPIPE when we write() to
	 * sd->fd. */
	poll(pfd, 1, 1000);
	if(pfd[0].revents & POLLHUP)
	{
		imcb_error( ic, "Could not connect to server" );
		imc_logout( ic, TRUE );
		return FALSE;
	}
	write( sd->fd, buf, len );

	return TRUE;
}

static void skype_buddy_ask_yes( gpointer w, struct skype_buddy_ask_data *bla )
{
	char *buf = g_strdup_printf("SET USER %s ISAUTHORIZED TRUE", bla->handle);
	skype_write( bla->ic, buf, strlen( buf ) );
	g_free(buf);
	g_free(bla->handle);
	g_free(bla);
}

static void skype_buddy_ask_no( gpointer w, struct skype_buddy_ask_data *bla )
{
	char *buf = g_strdup_printf("SET USER %s ISAUTHORIZED FALSE", bla->handle);
	skype_write( bla->ic, buf, strlen( buf ) );
	g_free(buf);
	g_free(bla->handle);
	g_free(bla);
}

void skype_buddy_ask( struct im_connection *ic, char *handle, char *message)
{
	struct skype_buddy_ask_data *bla = g_new0( struct skype_buddy_ask_data, 1 );
	char *buf;

	bla->ic = ic;
	bla->handle = g_strdup(handle);

	buf = g_strdup_printf( "The user %s wants to add you to his/her buddy list, saying: '%s'.", handle, message);
	imcb_ask( ic, buf, bla, skype_buddy_ask_yes, skype_buddy_ask_no );
	g_free( buf );
}

static gboolean skype_read_callback( gpointer data, gint fd, b_input_condition cond )
{
	struct im_connection *ic = data;
	struct skype_data *sd = ic->proto_data;
	char buf[1024];
	int st;
	char **lines, **lineptr, *line, *ptr;

	if( !sd || sd->fd == -1 )
		return FALSE;
	/* Read the whole data. */
	st = read( sd->fd, buf, sizeof( buf ) );
	if( st > 0 )
	{
		buf[st] = '\0';
		/* Then split it up to lines. */
		lines = g_strsplit(buf, "\n", 0);
		lineptr = lines;
		while((line = *lineptr))
		{
			if(!strlen(line))
				break;
			if(!strncmp(line, "USERS ", 6))
			{
				char **i;
				char **nicks;

				nicks = g_strsplit(line + 6, ", ", 0);
				i = nicks;
				while(*i)
				{
					g_snprintf(buf, 1024, "GET USER %s ONLINESTATUS\n", *i);
					skype_write( ic, buf, strlen( buf ) );
					i++;
				}
				g_strfreev(nicks);
			}
			else if(!strncmp(line, "USER ", 5))
			{
				int flags = 0;
				char *status = strrchr(line, ' ');
				char *user = strchr(line, ' ');
				status++;
				ptr = strchr(++user, ' ');
				*ptr = '\0';
				ptr++;
				if(!strncmp(ptr, "ONLINESTATUS ", 13) &&
						strcmp(user, sd->username) != 0
						&& strcmp(user, "echo123") != 0)
				{
					ptr = g_strdup_printf("%s@skype.com", user);
					imcb_add_buddy(ic, ptr, NULL);
					if(strcmp(status, "OFFLINE") != 0)
						flags |= OPT_LOGGED_IN;
					if(strcmp(status, "ONLINE") != 0 && strcmp(status, "SKYPEME") != 0)
						flags |= OPT_AWAY;
					imcb_buddy_status(ic, ptr, flags, NULL, NULL);
					g_free(ptr);
				}
				else if(!strncmp(ptr, "RECEIVEDAUTHREQUEST ", 20))
				{
					char *message = ptr + 20;
					if(strlen(message))
						skype_buddy_ask(ic, user, message);
				}
				else if(!strncmp(ptr, "BUDDYSTATUS ", 12))
				{
					char *st = ptr + 12;
					if(!strcmp(st, "3"))
					{
						char *buf = g_strdup_printf("%s@skype.com", user);
						imcb_add_buddy(ic, buf, NULL);
						g_free(buf);
					}
				}
			}
			else if(!strncmp(line, "CHATMESSAGE ", 12))
			{
				char *id = strchr(line, ' ');
				if(++id)
				{
					char *info = strchr(id, ' ');
					*info = '\0';
					info++;
					if(!strcmp(info, "STATUS RECEIVED"))
					{
						/* New message ID:
						 * (1) Request its from field
						 * (2) Request its body
						 * (3) Mark it as seen
						 */
						g_snprintf(buf, 1024, "GET CHATMESSAGE %s FROM_HANDLE\n", id);
						skype_write( ic, buf, strlen( buf ) );
						g_snprintf(buf, 1024, "GET CHATMESSAGE %s BODY\n", id);
						skype_write( ic, buf, strlen( buf ) );
						g_snprintf(buf, 1024, "SET CHATMESSAGE %s SEEN\n", id);
						skype_write( ic, buf, strlen( buf ) );
					}
					else if(!strncmp(info, "FROM_HANDLE ", 12))
					{
						info += 12;
						/* New from field value. Store
						 * it, then we can later use it
						 * when we got the message's
						 * body. */
						sd->handle = g_strdup_printf("%s@skype.com", info);
					}
					else if(!strncmp(info, "BODY ", 5))
					{
						info += 5;
						if(sd->handle)
						{
							/* New body, we have everything to use imcb_buddy_msg() now! */
							imcb_buddy_msg(ic, sd->handle, info, 0, 0);
							g_free(sd->handle);
							sd->handle = NULL;
						}
					}
				}
			}
			lineptr++;
		}
		g_strfreev(lines);
	}
	else if( st == 0 || ( st < 0 && !sockerr_again() ) )
	{
		closesocket( sd->fd );
		sd->fd = -1;

		imcb_error( ic, "Error while reading from server" );
		imc_logout( ic, TRUE );
		return FALSE;
	}
	return TRUE;
}

gboolean skype_start_stream( struct im_connection *ic )
{
	struct skype_data *sd = ic->proto_data;
	char *buf;
	int st;

	if(!sd)
		return FALSE;

	if( sd->r_inpa <= 0 )
		sd->r_inpa = b_input_add( sd->fd, GAIM_INPUT_READ, skype_read_callback, ic );

	/* This will download all buddies. */
	buf = g_strdup_printf("SEARCH FRIENDS\n");
	st = skype_write( ic, buf, strlen( buf ) );
	g_free(buf);
	buf = g_strdup_printf("SET USERSTATUS ONLINE\n");
	skype_write( ic, buf, strlen( buf ) );
	g_free(buf);
	return st;
}

gboolean skype_connected( gpointer data, gint source, b_input_condition cond )
{
	struct im_connection *ic = data;
	imcb_connected(ic);
	return skype_start_stream(ic);
}

static void skype_login( account_t *acc )
{
	struct im_connection *ic = imcb_new( acc );
	struct skype_data *sd = g_new0( struct skype_data, 1 );

	ic->proto_data = sd;

	imcb_log( ic, "Connecting" );
	sd->fd = proxy_connect(acc->server, set_getint( &acc->set, "port" ), skype_connected, ic );
	sd->username = g_strdup( acc->user );

	sd->ic = ic;
}

static void skype_logout( struct im_connection *ic )
{
	struct skype_data *sd = ic->proto_data;
	char *buf;

	buf = g_strdup_printf("SET USERSTATUS OFFLINE\n");
	skype_write( ic, buf, strlen( buf ) );
	g_free(buf);

	g_free(sd->username);
	g_free(sd);
	ic->proto_data = NULL;
}

static int skype_buddy_msg( struct im_connection *ic, char *who, char *message, int flags )
{
	char *buf, *ptr, *nick;
	int st;

	nick = g_strdup(who);
	ptr = strchr(nick, '@');
	if(ptr)
		*ptr = '\0';

	buf = g_strdup_printf("MESSAGE %s %s\n", nick, message);
	g_free(nick);
	st = skype_write( ic, buf, strlen( buf ) );
	g_free(buf);

	return st;
}

const struct skype_away_state *skype_away_state_by_name( char *name )
{
	int i;

	for( i = 0; skype_away_state_list[i].full_name; i ++ )
		if( g_strcasecmp( skype_away_state_list[i].full_name, name ) == 0 )
			return( skype_away_state_list + i );

	return NULL;
}

static void skype_set_away( struct im_connection *ic, char *state_txt, char *message )
{
	const struct skype_away_state *state;
	char *buf;

	if( strcmp( state_txt, GAIM_AWAY_CUSTOM ) == 0 )
		state = skype_away_state_by_name( "Away" );
	else
		state = skype_away_state_by_name( state_txt );
	buf = g_strdup_printf("SET USERSTATUS %s\n", state->code);
	skype_write( ic, buf, strlen( buf ) );
	g_free(buf);
}

static GList *skype_away_states( struct im_connection *ic )
{
	GList *l = NULL;
	int i;
	
	for( i = 0; skype_away_state_list[i].full_name; i ++ )
		l = g_list_append( l, (void*) skype_away_state_list[i].full_name );
	
	return l;
}

static void skype_add_buddy( struct im_connection *ic, char *who, char *group )
{
	char *buf, *nick, *ptr;

	nick = g_strdup(who);
	ptr = strchr(nick, '@');
	if(ptr)
		*ptr = '\0';
	buf = g_strdup_printf("SET USER %s BUDDYSTATUS 2 Please authorize me\n", nick);
	skype_write( ic, buf, strlen( buf ) );
	g_free(nick);
}

static void skype_remove_buddy( struct im_connection *ic, char *who, char *group )
{
	char *buf, *nick, *ptr;

	nick = g_strdup(who);
	ptr = strchr(nick, '@');
	if(ptr)
		*ptr = '\0';
	buf = g_strdup_printf("SET USER %s BUDDYSTATUS 1\n", nick);
	skype_write( ic, buf, strlen( buf ) );
	g_free(nick);
}

void init_plugin(void)
{
	struct prpl *ret = g_new0( struct prpl, 1 );

	ret->name = "skype";
	ret->login = skype_login;
	ret->init = skype_init;
	ret->logout = skype_logout;
	ret->buddy_msg = skype_buddy_msg;
	ret->away_states = skype_away_states;
	ret->set_away = skype_set_away;
	ret->add_buddy = skype_add_buddy;
	ret->remove_buddy = skype_remove_buddy;
	ret->handle_cmp = g_strcasecmp;
	register_protocol( ret );
}
