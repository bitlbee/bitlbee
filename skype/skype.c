/* 
 * This is the most simple possible BitlBee plugin. To use, compile it as 
 * a shared library and place it in the plugin directory: 
 *
 * gcc -o example.so -shared example.c `pkg-config --cflags bitlbee`
 * cp example.so /usr/local/lib/bitlbee
 */
#include <stdio.h>
#include <bitlbee.h>

#define SKYPE_PORT_DEFAULT "2727"

struct skype_data
{
	struct im_connection *ic;
	int fd;
	char *txq;
	int tx_len;
	int r_inpa, w_inpa;
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

	// TODO: error handling

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
				// TODO we should add the buddy even if the status is offline
				if((ptr = strrchr(line, ' ')) && strcmp(++ptr, "OFFLINE") != 0)
				{
					char *user = strchr(line, ' ');
					ptr = strchr(++user, ' ');
					*ptr = '\0';
					ptr = g_strdup_printf("%s@skype.com", user);
					imcb_add_buddy(ic, ptr, NULL);
					imcb_buddy_status(ic, ptr, OPT_LOGGED_IN, NULL, NULL);
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

	imcb_connected(ic);
	if( sd->fd < 0 )
	{
		imcb_error( ic, "Could not connect to server" );
		imc_logout( ic, TRUE );
		return FALSE;
	}
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

	sd->ic = ic;
}

static void skype_logout( struct im_connection *ic )
{
	struct skype_data *sd = ic->proto_data;
	g_free(sd);
}

static void skype_set_away( struct im_connection *ic, char *state_txt, char *message )
{
}

static GList *skype_away_states( struct im_connection *ic )
{
	static GList *l = NULL;

	l = g_list_append( l, (void*)"Online" );
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
	ret->away_states = skype_away_states;
	ret->add_buddy = skype_add_buddy;
	ret->remove_buddy = skype_remove_buddy;
	ret->away_states = skype_away_states;
	ret->set_away = skype_set_away;
	ret->handle_cmp = g_strcasecmp;
	register_protocol( ret );
}
