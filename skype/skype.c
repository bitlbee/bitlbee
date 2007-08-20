/* 
 * This is the most simple possible BitlBee plugin. To use, compile it as 
 * a shared library and place it in the plugin directory: 
 *
 * gcc -o example.so -shared example.c `pkg-config --cflags bitlbee`
 * cp example.so /usr/local/lib/bitlbee
 */
#include <stdio.h>
#include <poll.h>
#include <bitlbee.h>

#define SKYPE_PORT_DEFAULT "2727"

struct skype_data
{
	struct im_connection *ic;
	char *username;
	int fd;
	char *txq;
	int tx_len;
	int r_inpa, w_inpa;
	// when we receive a new message id, we query the handle, then the body
	// store the handle here
	// TODO: it would be nicer to use a hashmap for this or something
	char *handle;
};

struct skype_away_state
{
	char *code;
	char *full_name;
};

const struct skype_away_state skype_away_state_list[] =
{
	{ "ONLINE",  "Online" },
	{ "SKYPEME",  "Skype Me" },
	{ "AWAY",   "Away" },
	{ "NA",    "Not available" },
	{ "DND",      "Do Not Disturb" },
	{ "INVISIBLE",      "Invisible" },
	{ "OFFLINE",      "Offline" }
};

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

	printf("write(): %s", buf);
	write( sd->fd, buf, len );

	return TRUE;
}

static gboolean skype_read_callback( gpointer data, gint fd, b_input_condition cond )
{
	struct im_connection *ic = data;
	struct skype_data *sd = ic->proto_data;
	char buf[1024];
	int st;
	char **lines, **lineptr, *line, *ptr;

	if( sd->fd == -1 )
		return FALSE;
	st = read( sd->fd, buf, sizeof( buf ) );
	if( st > 0 )
	{
		buf[st] = '\0';
		printf("read(): '%s'\n", buf);
		lines = g_strsplit(buf, "\n", 0);
		lineptr = lines;
		while((line = *lineptr))
		{
			if(!strlen(line))
				break;
			printf("skype_read_callback() new line: '%s'\n", line);
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
				if(strcmp(user, sd->username) != 0 && strcmp(user, "echo123") != 0)
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
						// new message, request its body
						printf("new received message  #%s\n", id);
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
						// new handle
						sd->handle = g_strdup_printf("%s@skype.com", info);
						printf("new handle: '%s'\n", info);
					}
					else if(!strncmp(info, "BODY ", 5))
					{
						info += 5;
						// new body
						printf("<%s> %s\n", sd->handle, info);
						if(sd->handle)
							imcb_buddy_msg(ic, sd->handle, info, 0, 0);
						g_free(sd->handle);
						sd->handle = NULL;
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

	/* EAGAIN/etc or a successful read. */
	return TRUE;
}

gboolean skype_start_stream( struct im_connection *ic )
{
	struct skype_data *sd = ic->proto_data;
	char *buf;
	int st;

	if( sd->r_inpa <= 0 )
		sd->r_inpa = b_input_add( sd->fd, GAIM_INPUT_READ, skype_read_callback, ic );

	// download buddies
	buf = g_strdup_printf("SEARCH FRIENDS\n");
	st = skype_write( ic, buf, strlen( buf ) );
	g_free(buf);
	return st;
}

gboolean skype_connected( gpointer data, gint source, b_input_condition cond )
{
	struct im_connection *ic = data;
	struct skype_data *sd = ic->proto_data;
	struct pollfd pfd[1];

	pfd[0].fd = sd->fd;
	pfd[0].events = POLLOUT;

	poll(pfd, 1, 1000);
	if(pfd[0].revents & POLLHUP)
	{
		imcb_error( ic, "Could not connect to server" );
		imc_logout( ic, TRUE );
		return FALSE;
	}
	imcb_connected(ic);
	return skype_start_stream(ic);
}

static void skype_login( account_t *acc )
{
	struct im_connection *ic = imcb_new( acc );
	struct skype_data *sd = g_new0( struct skype_data, 1 );

	ic->proto_data = sd;

	imcb_log( ic, "Connecting" );
	printf("%s:%d\n", acc->server, set_getint( &acc->set, "port"));
	sd->fd = proxy_connect(acc->server, set_getint( &acc->set, "port" ), skype_connected, ic );
	printf("sd->fd: %d\n", sd->fd);
	/*imcb_add_buddy(ic, "test@skype.com", NULL);
	imcb_buddy_status(ic, "test@skype.com", OPT_LOGGED_IN, NULL, NULL);
	imcb_buddy_msg(ic, "test@skype.com", "test from skype plugin", 0, 0);*/
	sd->username = g_strdup( acc->user );

	sd->ic = ic;
}

static void skype_logout( struct im_connection *ic )
{
	struct skype_data *sd = ic->proto_data;
	g_free(sd->username);
	g_free(sd);
}

static int skype_buddy_msg( struct im_connection *ic, char *who, char *message, int flags )
{
	char *buf, *ptr, *nick;
	int st;

	nick = g_strdup_printf("%s", who);
	ptr = strchr(nick, '@');
	if(ptr)
		*ptr = '\0';

	buf = g_strdup_printf("MESSAGE %s %s\n", nick, message);
	g_free(nick);
	st = skype_write( ic, buf, strlen( buf ) );
	g_free(buf);

	return st;
}

static void skype_set_away( struct im_connection *ic, char *state_txt, char *message )
{
}

static GList *skype_away_states( struct im_connection *ic )
{
	static GList *l = NULL;
	int i;
	
	if( l == NULL )
		for( i = 0; skype_away_state_list[i].full_name; i ++ )
			l = g_list_append( l, (void*) skype_away_state_list[i].full_name );
	
	return l;
}

static void skype_add_buddy( struct im_connection *ic, char *who, char *group )
{
}

static void skype_remove_buddy( struct im_connection *ic, char *who, char *group )
{
}

void init_plugin(void)
{
	struct prpl *ret = g_new0( struct prpl, 1 );

	ret->name = "skype";
	ret->login = skype_login;
	ret->init = skype_init;
	ret->logout = skype_logout;
	ret->buddy_msg = skype_buddy_msg;
	ret->set_away = skype_set_away;
	ret->away_states = skype_away_states;
	ret->add_buddy = skype_add_buddy;
	ret->remove_buddy = skype_remove_buddy;
	ret->away_states = skype_away_states;
	ret->set_away = skype_set_away;
	ret->handle_cmp = g_strcasecmp;
	register_protocol( ret );
}
