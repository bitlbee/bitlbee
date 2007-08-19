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

static gboolean skype_write_callback( gpointer data, gint fd, b_input_condition cond );
static gboolean skype_write_queue( struct im_connection *ic );

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
	gboolean ret;

	if( sd->tx_len == 0 )
	{
		sd->tx_len = len;
		sd->txq = g_memdup( buf, len );
		if( ( ret = skype_write_queue( ic ) ) && sd->tx_len > 0 )
			sd->w_inpa = b_input_add( sd->fd, GAIM_INPUT_WRITE, skype_write_callback, ic );
	}
	else
	{
		sd->txq = g_renew( char, sd->txq, sd->tx_len + len );
		memcpy( sd->txq + sd->tx_len, buf, len );
		sd->tx_len += len;
		ret = TRUE;
	}
	return ret;
}

static gboolean skype_write_callback( gpointer data, gint fd, b_input_condition cond )
{
	struct skype_data *sd = ((struct im_connection *)data)->proto_data;

	return sd->fd != -1 &&
		skype_write_queue( data ) &&
		sd->tx_len > 0;
}

static gboolean skype_write_queue( struct im_connection *ic )
{
	struct skype_data *sd = ic->proto_data;
	int st;

	st = write( sd->fd, sd->txq, sd->tx_len );

	if( st == sd->tx_len )
	{
		/* We wrote everything, clear the buffer. */
		g_free( sd->txq );
		sd->txq = NULL;
		sd->tx_len = 0;

		return TRUE;
	}
	else if( st == 0 || ( st < 0 && !sockerr_again() ) )
	{
		/* Set fd to -1 to make sure we won't write to it anymore. */
		closesocket( sd->fd );  /* Shouldn't be necessary after errors? */
		sd->fd = -1;

		imcb_error( ic, "Short write() to server" );
		imc_logout( ic, TRUE );
		return FALSE;
	}
	else if( st > 0 )
	{
		char *s;

		s = g_memdup( sd->txq + st, sd->tx_len - st );
		sd->tx_len -= st;
		g_free( sd->txq );
		sd->txq = s;

		return TRUE;
	}
	else
	{
		return TRUE;
	}
}

gboolean skype_start_stream( struct im_connection *ic )
{
	char *buf;
	int st;

	buf = g_strdup_printf("SEARCH FRIENDS");
	st = skype_write( ic, buf, strlen( buf ) );
	g_free(buf);
	return st;
}

gboolean skype_connected( gpointer data, gint source, b_input_condition cond )
{
	struct im_connection *ic = data;

	imcb_connected(ic);
	//imcb_add_buddy(ic, "vmiklos_dsd@skype.com", NULL);
	//imcb_buddy_status(ic, "vmiklos_dsd@skype.com", OPT_LOGGED_IN, NULL, NULL);
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
	if( sd->fd < 0 )
	{
		imcb_error( ic, "Could not connect to server" );
		imc_logout( ic, TRUE );
		return;
	}

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
	int i;

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
