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
#include <glib.h>

#define SKYPE_PORT_DEFAULT "2727"

/*
 * Enumerations
 */

typedef enum
{
	SKYPE_CALL_RINGING = 1,
	SKYPE_CALL_MISSED
} skype_call_status;

typedef enum
{
	SKYPE_FILETRANSFER_NEW = 1,
	SKYPE_FILETRANSFER_FAILED
} skype_filetransfer_status;

/*
 * Structures
 */

struct skype_data
{
	struct im_connection *ic;
	char *username;
	/* The effective file descriptor. We store it here so any function can
	 * write() to it. */
	int fd;
	/* File descriptor returned by bitlbee. we store it so we know when
	 * we're connected and when we aren't. */
	int bfd;
	/* When we receive a new message id, we query the properties, finally
	 * the chatname. Store the properties here so that we can use
	 * imcb_buddy_msg() when we got the chatname. */
	char *handle;
	char *body;
	char *type;
	/* This is necessary because we send a notification when we get the
	 * handle. So we store the state here and then we can send a
	 * notification about the handle is in a given status. */
	skype_call_status call_status;
	/* Same for file transfers. */
	skype_filetransfer_status filetransfer_status;
	/* Using /j #nick we want to have a groupchat with two people. Usually
	 * not (default). */
	char* groupchat_with;
	/* The user who invited us to the chat. */
	char* adder;
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

struct groupchat *skype_chat_by_name( struct im_connection *ic, char *name )
{
	struct groupchat *ret;

	for( ret = ic->conversations; ret; ret = ret->next )
	{
		if(strcmp(name, ret->title ) == 0 )
			break;
	}

	return ret;
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
						 * (3) Request its type
						 * (4) Query chatname
						 */
						g_snprintf(buf, 1024, "GET CHATMESSAGE %s FROM_HANDLE\n", id);
						skype_write( ic, buf, strlen( buf ) );
						g_snprintf(buf, 1024, "GET CHATMESSAGE %s BODY\n", id);
						skype_write( ic, buf, strlen( buf ) );
						g_snprintf(buf, 1024, "GET CHATMESSAGE %s TYPE\n", id);
						skype_write( ic, buf, strlen( buf ) );
						g_snprintf(buf, 1024, "GET CHATMESSAGE %s CHATNAME\n", id);
						skype_write( ic, buf, strlen( buf ) );
					}
					else if(!strncmp(info, "FROM_HANDLE ", 12))
					{
						info += 12;
						/* New from field value. Store
						 * it, then we can later use it
						 * when we got the message's
						 * body. */
						g_free(sd->handle);
						sd->handle = g_strdup_printf("%s@skype.com", info);
					}
					else if(!strncmp(info, "EDITED_BY ", 10))
					{
						info += 10;
						/* This is the same as
						 * FROM_HANDLE, except that we
						 * never request these lines
						 * from Skype, we just get
						 * them. */
						g_free(sd->handle);
						sd->handle = g_strdup_printf("%s@skype.com", info);
					}
					else if(!strncmp(info, "BODY ", 5))
					{
						info += 5;
						g_free(sd->body);
						sd->body = g_strdup(info);
					}
					else if(!strncmp(info, "TYPE ", 5))
					{
						info += 5;
						g_free(sd->type);
						sd->type = g_strdup(info);
					}
					else if(!strncmp(info, "CHATNAME ", 9))
					{
						info += 9;
						if(sd->handle && sd->body && sd->type)
						{
							struct groupchat *gc = skype_chat_by_name(ic, info);
							if(!strcmp(sd->type, "SAID"))
							{
								if(!gc)
									/* Private message */
									imcb_buddy_msg(ic, sd->handle, sd->body, 0, 0);
								else
									/* Groupchat message */
									imcb_chat_msg(gc, sd->handle, sd->body, 0, 0);
							}
							else if(!strcmp(sd->type, "SETTOPIC"))
							{
								if(gc)
									imcb_chat_topic(gc, sd->handle, sd->body);
							}
							else if(!strcmp(sd->type, "LEFT"))
							{
								if(gc)
									imcb_chat_remove_buddy(gc, sd->handle, NULL);
							}
						}
					}
				}
			}
			else if(!strncmp(line, "CALL ", 5))
			{
				char *id = strchr(line, ' ');
				if(++id)
				{
					char *info = strchr(id, ' ');
					*info = '\0';
					info++;
					if(!strcmp(info, "STATUS RINGING"))
					{
						g_snprintf(buf, 1024, "GET CALL %s PARTNER_HANDLE\n", id);
						skype_write( ic, buf, strlen( buf ) );
						sd->call_status = SKYPE_CALL_RINGING;
					}
					else if(!strcmp(info, "STATUS MISSED"))
					{
						g_snprintf(buf, 1024, "GET CALL %s PARTNER_HANDLE\n", id);
						skype_write( ic, buf, strlen( buf ) );
						sd->call_status = SKYPE_CALL_MISSED;
					}
					else if(!strncmp(info, "PARTNER_HANDLE ", 15))
					{
						info += 15;
						if(sd->call_status) {
							switch(sd->call_status)
							{
								case SKYPE_CALL_RINGING:
									imcb_log(ic, "The user %s is currently ringing you.", info);
									break;
								case SKYPE_CALL_MISSED:
									imcb_log(ic, "You have missed a call from user %s.", info);
									break;
							}
							sd->call_status = 0;
						}
					}
				}
			}
			else if(!strncmp(line, "FILETRANSFER ", 13))
			{
				char *id = strchr(line, ' ');
				if(++id)
				{
					char *info = strchr(id, ' ');
					*info = '\0';
					info++;
					if(!strcmp(info, "STATUS NEW"))
					{
						g_snprintf(buf, 1024, "GET FILETRANSFER %s PARTNER_HANDLE\n", id);
						skype_write( ic, buf, strlen( buf ) );
						sd->filetransfer_status = SKYPE_FILETRANSFER_NEW;
					}
					else if(!strcmp(info, "STATUS FAILED"))
					{
						g_snprintf(buf, 1024, "GET FILETRANSFER %s PARTNER_HANDLE\n", id);
						skype_write( ic, buf, strlen( buf ) );
						sd->filetransfer_status = SKYPE_FILETRANSFER_FAILED;
					}
					else if(!strncmp(info, "PARTNER_HANDLE ", 15))
					{
						info += 15;
						if(sd->filetransfer_status) {
							switch(sd->filetransfer_status)
							{
								case SKYPE_FILETRANSFER_NEW:
									imcb_log(ic, "The user %s offered a new file for you.", info);
									break;
								case SKYPE_FILETRANSFER_FAILED:
									imcb_log(ic, "Failed to transfer file from user %s.", info);
									break;
							}
							sd->filetransfer_status = 0;
						}
					}
				}
			}
			else if(!strncmp(line, "CHAT ", 5))
			{
				char *id = strchr(line, ' ');
				if(++id)
				{
					char *info = strchr(id, ' ');
					if(info)
						*info = '\0';
					info++;
					if(!strcmp(info, "STATUS MULTI_SUBSCRIBED"))
					{
						imcb_chat_new( ic, id );
						g_snprintf(buf, 1024, "GET CHAT %s ADDER\n", id);
						skype_write(ic, buf, strlen(buf));
						g_snprintf(buf, 1024, "GET CHAT %s TOPIC\n", id);
						skype_write(ic, buf, strlen(buf));
					}
					else if(!strcmp(info, "STATUS DIALOG") && sd->groupchat_with)
					{
						struct groupchat *gc = imcb_chat_new( ic, id );
						/* According to the docs this
						 * is necessary. However it
						 * does not seem the situation
						 * and it would open an extra
						 * window on our client, so
						 * just leave it out. */
						/*g_snprintf(buf, 1024, "OPEN CHAT %s\n", id);
						skype_write(ic, buf, strlen(buf));*/
						g_snprintf(buf, 1024, "%s@skype.com", sd->groupchat_with);
						imcb_chat_add_buddy(gc, buf);
						imcb_chat_add_buddy(gc, sd->username);
						g_free(sd->groupchat_with);
						sd->groupchat_with = NULL;
						g_snprintf(buf, 1024, "GET CHAT %s ADDER\n", id);
						skype_write(ic, buf, strlen(buf));
						g_snprintf(buf, 1024, "GET CHAT %s TOPIC\n", id);
						skype_write(ic, buf, strlen(buf));
					}
					else if(!strcmp(info, "STATUS UNSUBSCRIBED"))
					{
						struct groupchat *gc = skype_chat_by_name(ic, id);
						if(gc)
							gc->data = (void*)FALSE;
					}
					else if(!strncmp(info, "ADDER ", 6))
					{
						info += 6;
						g_free(sd->adder);
						sd->adder = g_strdup_printf("%s@skype.com", info);
					}
					else if(!strncmp(info, "TOPIC ", 6))
					{
						info += 6;
						struct groupchat *gc = skype_chat_by_name(ic, id);
						if(gc && sd->adder)
						{
							imcb_chat_topic(gc, sd->adder, info);
							g_free(sd->adder);
							sd->adder = NULL;
						}
					}
					else if(!strncmp(info, "ACTIVEMEMBERS ", 14))
					{
						info += 14;
						struct groupchat *gc = skype_chat_by_name(ic, id);
						/* Hack! We set ->data to TRUE
						 * while we're on the channel
						 * so that we won't rejoin
						 * after a /part. */
						if(gc && !gc->data)
						{
							char **members = g_strsplit(info, " ", 0);
							int i;
							for(i=0;members[i];i++)
							{
								if(!strcmp(members[i], sd->username))
									continue;
								g_snprintf(buf, 1024, "%s@skype.com", members[i]);
								if(!g_list_find_custom(gc->in_room, buf, (GCompareFunc)strcmp))
									imcb_chat_add_buddy(gc, buf);
							}
							imcb_chat_add_buddy(gc, sd->username);
							g_strfreev(members);
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

	if( sd->bfd <= 0 )
		sd->bfd = b_input_add( sd->fd, GAIM_INPUT_READ, skype_read_callback, ic );

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
	g_free(sd->handle);
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

void skype_chat_msg( struct groupchat *gc, char *message, int flags )
{
	struct im_connection *ic = gc->ic;
	char *buf;
	buf = g_strdup_printf("CHATMESSAGE %s %s\n", gc->title, message);
	skype_write( ic, buf, strlen( buf ) );
	g_free(buf);
}

void skype_chat_leave( struct groupchat *gc )
{
	struct im_connection *ic = gc->ic;
	char *buf;
	buf = g_strdup_printf("ALTER CHAT %s LEAVE\n", gc->title);
	skype_write( ic, buf, strlen( buf ) );
	g_free(buf);
	gc->data = (void*)TRUE;
}

void skype_chat_invite(struct groupchat *gc, char *who, char *message)
{
	struct im_connection *ic = gc->ic;
	char *buf, *ptr, *nick;
	nick = g_strdup(message);
	ptr = strchr(nick, '@');
	if(ptr)
		*ptr = '\0';
	buf = g_strdup_printf("ALTER CHAT %s ADDMEMBERS %s\n", gc->title, nick);
	skype_write( ic, buf, strlen( buf ) );
	g_free(buf);
	g_free(nick);
}

void skype_chat_topic(struct groupchat *gc, char *message)
{
	struct im_connection *ic = gc->ic;
	char *buf;
	buf = g_strdup_printf("ALTER CHAT %s SETTOPIC %s\n", gc->title, message);
	skype_write( ic, buf, strlen( buf ) );
	g_free(buf);
}

struct groupchat *skype_chat_with(struct im_connection *ic, char *who)
{
	struct skype_data *sd = ic->proto_data;
	char *ptr, *nick, *buf;
	nick = g_strdup(who);
	ptr = strchr(nick, '@');
	if(ptr)
		*ptr = '\0';
	buf = g_strdup_printf("CHAT CREATE %s\n", nick);
	skype_write(ic, buf, strlen(buf));
	g_free(buf);
	sd->groupchat_with = g_strdup(nick);
	g_free(nick);
	return(NULL);
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
	ret->chat_msg = skype_chat_msg;
	ret->chat_leave = skype_chat_leave;
	ret->chat_invite = skype_chat_invite;
	ret->chat_with = skype_chat_with;
	ret->handle_cmp = g_strcasecmp;
	ret->chat_topic = skype_chat_topic;
	register_protocol( ret );
}
