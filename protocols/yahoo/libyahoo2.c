/*
 * libyahoo2: libyahoo2.c
 *
 * Some code copyright (C) 2002-2004, Philip S Tellis <philip.tellis AT gmx.net>
 * YMSG16 code copyright (C) 2009,
 *              Siddhesh Poyarekar <siddhesh dot poyarekar at gmail dot com>
 *
 * Yahoo Search copyright (C) 2003, Konstantin Klyagin <konst AT konst.org.ua>
 *
 * Much of this code was taken and adapted from the yahoo module for
 * gaim released under the GNU GPL.  This code is also released under the
 * GNU GPL.
 *
 * This code is derivative of Gaim <http://gaim.sourceforge.net>
 * copyright (C) 1998-1999, Mark Spencer <markster@marko.net>
 *	       1998-1999, Adam Fritzler <afritz@marko.net>
 *	       1998-2002, Rob Flynn <rob@marko.net>
 *	       2000-2002, Eric Warmenhoven <eric@warmenhoven.org>
 *	       2001-2002, Brian Macke <macke@strangelove.net>
 *		    2001, Anand Biligiri S <abiligiri@users.sf.net>
 *		    2001, Valdis Kletnieks
 *		    2002, Sean Egan <bj91704@binghamton.edu>
 *		    2002, Toby Gray <toby.gray@ntlworld.com>
 *
 * This library also uses code from other libraries, namely:
 *     Portions from libfaim copyright 1998, 1999 Adam Fritzler
 *     <afritz@auk.cx>
 *     Portions of Sylpheed copyright 2000-2002 Hiroyuki Yamamoto
 *     <hiro-y@kcn.ne.jp>
 *
 * YMSG16 authentication code based mostly on write-up at:
 *      http://www.carbonize.co.uk/ymsg16.html
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>

#if STDC_HEADERS
# include <string.h>
#else
# if !HAVE_STRCHR
#  define strchr index
#  define strrchr rindex
# endif
char *strchr(), *strrchr();
# if !HAVE_MEMCPY
#  define memcpy(d, s, n) bcopy((s), (d), (n))
#  define memmove(d, s, n) bcopy((s), (d), (n))
# endif
#endif

#include <sys/types.h>

#ifdef __MINGW32__
# include <winsock2.h>
#endif

#include <stdlib.h>
#include <ctype.h>

#include "sha1.h"
#include "md5.h"
#include "yahoo2.h"
#include "yahoo_httplib.h"
#include "yahoo_util.h"

#include "yahoo2_callbacks.h"
#include "yahoo_debug.h"
#if defined(__MINGW32__) && !defined(HAVE_GLIB)
#define snprintf _snprintf
#define vsnprintf _vsnprintf
#endif

#include "base64.h"
#include "http_client.h"

#ifdef USE_STRUCT_CALLBACKS
struct yahoo_callbacks *yc = NULL;

void yahoo_register_callbacks(struct yahoo_callbacks *tyc)
{
	yc = tyc;
}

#define YAHOO_CALLBACK(x)       yc->x
#else
#define YAHOO_CALLBACK(x)       x
#endif

static int yahoo_send_data(void *fd, void *data, int len);
static void _yahoo_http_connected(int id, void *fd, int error, void *data);
static void yahoo_connected(void *fd, int error, void *data);

int yahoo_log_message(char *fmt, ...)
{
	char out[1024];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(out, sizeof(out), fmt, ap);
	va_end(ap);
	return YAHOO_CALLBACK (ext_yahoo_log) ("%s", out);
}

static enum yahoo_log_level log_level = YAHOO_LOG_NONE;

enum yahoo_log_level yahoo_get_log_level()
{
	return log_level;
}

int yahoo_set_log_level(enum yahoo_log_level level)
{
	enum yahoo_log_level l = log_level;

	log_level = level;
	return l;
}

/* default values for servers */
static char *default_pager_hosts[] = {  "scs.msg.yahoo.com",
	                                "scsa.msg.yahoo.com",
	                                "scsb.msg.yahoo.com",
	                                "scsc.msg.yahoo.com",
	                                NULL };

static int pager_port = 5050;
static int fallback_ports[] = { 23, 25, 80, 20, 119, 8001, 8002, 5050, 0 };

static char filetransfer_host[] = "filetransfer.msg.yahoo.com";
static int filetransfer_port = 80;
static char webcam_host[] = "webcam.yahoo.com";
static int webcam_port = 5100;
static char webcam_description[] = "";
static char local_host[] = "";
static int conn_type = Y_WCM_DSL;

static char profile_url[] = "http://profiles.yahoo.com/";

struct connect_callback_data {
	struct yahoo_data *yd;
	int tag;
	int i;
	int server_i;
};

struct yahoo_pair {
	int key;
	char *value;
};

struct yahoo_packet {
	unsigned short int service;
	unsigned int status;
	unsigned int id;
	YList *hash;
};

struct yahoo_search_state {
	int lsearch_type;
	char *lsearch_text;
	int lsearch_gender;
	int lsearch_agerange;
	int lsearch_photo;
	int lsearch_yahoo_only;
	int lsearch_nstart;
	int lsearch_nfound;
	int lsearch_ntotal;
};

struct data_queue {
	unsigned char *queue;
	int len;
};

struct yahoo_input_data {
	struct yahoo_data *yd;
	struct yahoo_webcam *wcm;
	struct yahoo_webcam_data *wcd;
	struct yahoo_search_state *ys;

	void *fd;
	enum yahoo_connection_type type;

	unsigned char *rxqueue;
	int rxlen;
	int read_tag;

	YList *txqueues;
	int write_tag;
};

struct yahoo_server_settings {
	char *pager_host;
	int pager_port;
	char *filetransfer_host;
	int filetransfer_port;
	char *webcam_host;
	int webcam_port;
	char *webcam_description;
	char *local_host;
	int conn_type;
	char **pager_host_list;
};

static void yahoo_process_ft_connection(struct yahoo_input_data *yid, int over);

static void yahoo_process_filetransfer(struct yahoo_input_data *yid,
                                       struct yahoo_packet *pkt);
static void yahoo_process_filetransferinfo(struct yahoo_input_data *yid,
                                           struct yahoo_packet *pkt);
static void yahoo_process_filetransferaccept(struct yahoo_input_data *yid,
                                             struct yahoo_packet *pkt);

static void yahoo_https_auth(struct yahoo_input_data *yid, const char *seed, const char *sn);

static void *_yahoo_default_server_settings()
{
	struct yahoo_server_settings *yss =
	        y_new0(struct yahoo_server_settings, 1);

	/* Give preference to the default host list
	 * Make sure that only one of the two is set at any time
	 */
	yss->pager_host = NULL;
	yss->pager_host_list = default_pager_hosts;

	yss->pager_port = pager_port;
	yss->filetransfer_host = strdup(filetransfer_host);
	yss->filetransfer_port = filetransfer_port;
	yss->webcam_host = strdup(webcam_host);
	yss->webcam_port = webcam_port;
	yss->webcam_description = strdup(webcam_description);
	yss->local_host = strdup(local_host);
	yss->conn_type = conn_type;

	return yss;
}

static void *_yahoo_assign_server_settings(va_list ap)
{
	struct yahoo_server_settings *yss = _yahoo_default_server_settings();
	char *key;
	char *svalue;
	int nvalue;
	char **pvalue;

	while (1) {
		key = va_arg(ap, char *);
		if (key == NULL) {
			break;
		}

		if (!strcmp(key, "pager_host")) {
			svalue = va_arg(ap, char *);
			free(yss->pager_host);
			yss->pager_host = strdup(svalue);
			yss->pager_host_list = NULL;
		} else if (!strcmp(key, "pager_host_list")) {
			pvalue = va_arg(ap, char **);
			yss->pager_host_list = pvalue;
			free(yss->pager_host);
			yss->pager_host = NULL;
		} else if (!strcmp(key, "pager_port")) {
			nvalue = va_arg(ap, int);
			yss->pager_port = nvalue;
		} else if (!strcmp(key, "filetransfer_host")) {
			svalue = va_arg(ap, char *);
			free(yss->filetransfer_host);
			yss->filetransfer_host = strdup(svalue);
		} else if (!strcmp(key, "filetransfer_port")) {
			nvalue = va_arg(ap, int);
			yss->filetransfer_port = nvalue;
		} else if (!strcmp(key, "webcam_host")) {
			svalue = va_arg(ap, char *);
			free(yss->webcam_host);
			yss->webcam_host = strdup(svalue);
		} else if (!strcmp(key, "webcam_port")) {
			nvalue = va_arg(ap, int);
			yss->webcam_port = nvalue;
		} else if (!strcmp(key, "webcam_description")) {
			svalue = va_arg(ap, char *);
			free(yss->webcam_description);
			yss->webcam_description = strdup(svalue);
		} else if (!strcmp(key, "local_host")) {
			svalue = va_arg(ap, char *);
			free(yss->local_host);
			yss->local_host = strdup(svalue);
		} else if (!strcmp(key, "conn_type")) {
			nvalue = va_arg(ap, int);
			yss->conn_type = nvalue;
		} else {
			WARNING(("Unknown key passed to yahoo_init, "
			         "perhaps you didn't terminate the list "
			         "with NULL"));
		}
	}

	return yss;
}

static void yahoo_free_server_settings(struct yahoo_server_settings *yss)
{
	if (!yss) {
		return;
	}

	free(yss->pager_host);
	free(yss->filetransfer_host);
	free(yss->webcam_host);
	free(yss->webcam_description);
	free(yss->local_host);

	free(yss);
}

static YList *conns = NULL;
static YList *inputs = NULL;
static int last_id = 0;

static void add_to_list(struct yahoo_data *yd)
{
	conns = y_list_prepend(conns, yd);
}

static struct yahoo_data *find_conn_by_id(int id)
{
	YList *l;

	for (l = conns; l; l = y_list_next(l)) {
		struct yahoo_data *yd = l->data;
		if (yd->client_id == id) {
			return yd;
		}
	}
	return NULL;
}

static void del_from_list(struct yahoo_data *yd)
{
	conns = y_list_remove(conns, yd);
}

/* call repeatedly to get the next one */
/*
static struct yahoo_input_data * find_input_by_id(int id)
{
        YList *l;
        for(l = inputs; l; l = y_list_next(l)) {
                struct yahoo_input_data *yid = l->data;
                if(yid->yd->client_id == id)
                        return yid;
        }
        return NULL;
}
*/

#if 0
static struct yahoo_input_data *find_input_by_id_and_webcam_user(int id,
                                                                 const char *who)
{
	YList *l;

	LOG(("find_input_by_id_and_webcam_user"));
	for (l = inputs; l; l = y_list_next(l)) {
		struct yahoo_input_data *yid = l->data;
		if (yid->type == YAHOO_CONNECTION_WEBCAM
		    && yid->yd->client_id == id && yid->wcm && ((who
		                                                 && yid->wcm->user
		                                                 && !strcmp(who, yid->wcm->user))
		                                                || !(yid->wcm->user && !who))) {
			return yid;
		}
	}
	return NULL;
}
#endif

static struct yahoo_input_data *find_input_by_id_and_type(int id,
                                                          enum yahoo_connection_type type)
{
	YList *l;

	LOG(("find_input_by_id_and_type"));
	for (l = inputs; l; l = y_list_next(l)) {
		struct yahoo_input_data *yid = l->data;
		if (yid->type == type && yid->yd->client_id == id) {
			return yid;
		}
	}
	return NULL;
}

static struct yahoo_input_data *find_input_by_id_and_fd(int id, void *fd)
{
	YList *l;

	LOG(("find_input_by_id_and_fd"));
	for (l = inputs; l; l = y_list_next(l)) {
		struct yahoo_input_data *yid = l->data;
		if (yid->fd == fd && yid->yd->client_id == id) {
			return yid;
		}
	}
	return NULL;
}

static int count_inputs_with_id(int id)
{
	int c = 0;
	YList *l;

	LOG(("counting %d", id));
	for (l = inputs; l; l = y_list_next(l)) {
		struct yahoo_input_data *yid = l->data;
		if (yid->yd->client_id == id) {
			c++;
		}
	}
	LOG(("%d", c));
	return c;
}

/* Free a buddy list */
static void yahoo_free_buddies(YList *list)
{
	YList *l;

	for (l = list; l; l = l->next) {
		struct yahoo_buddy *bud = l->data;
		if (!bud) {
			continue;
		}

		FREE(bud->group);
		FREE(bud->id);
		FREE(bud->real_name);
		if (bud->yab_entry) {
			FREE(bud->yab_entry->fname);
			FREE(bud->yab_entry->lname);
			FREE(bud->yab_entry->nname);
			FREE(bud->yab_entry->id);
			FREE(bud->yab_entry->email);
			FREE(bud->yab_entry->hphone);
			FREE(bud->yab_entry->wphone);
			FREE(bud->yab_entry->mphone);
			FREE(bud->yab_entry);
		}
		FREE(bud);
		l->data = bud = NULL;
	}

	y_list_free(list);
}

/* Free an identities list */
static void yahoo_free_identities(YList *list)
{
	while (list) {
		YList *n = list;
		FREE(list->data);
		list = y_list_remove_link(list, list);
		y_list_free_1(n);
	}
}

/* Free webcam data */
static void yahoo_free_webcam(struct yahoo_webcam *wcm)
{
	if (wcm) {
		FREE(wcm->user);
		FREE(wcm->server);
		FREE(wcm->key);
		FREE(wcm->description);
		FREE(wcm->my_ip);
	}
	FREE(wcm);
}

static void yahoo_free_data(struct yahoo_data *yd)
{
	FREE(yd->user);
	FREE(yd->password);
	FREE(yd->cookie_y);
	FREE(yd->cookie_t);
	FREE(yd->cookie_b);
	FREE(yd->cookie_c);
	FREE(yd->login_cookie);
	FREE(yd->login_id);

	yahoo_free_buddies(yd->buddies);
	yahoo_free_buddies(yd->ignore);
	yahoo_free_identities(yd->identities);

	yahoo_free_server_settings(yd->server_settings);

	FREE(yd);
}

#define YAHOO_PACKET_HDRLEN (4 + 2 + 2 + 2 + 2 + 4 + 4)

static struct yahoo_packet *yahoo_packet_new(enum yahoo_service service,
                                             enum ypacket_status status, int id)
{
	struct yahoo_packet *pkt = y_new0(struct yahoo_packet, 1);

	pkt->service = service;
	pkt->status = status;
	pkt->id = id;

	return pkt;
}

static void yahoo_packet_hash(struct yahoo_packet *pkt, int key,
                              const char *value)
{
	struct yahoo_pair *pair = y_new0(struct yahoo_pair, 1);

	pair->key = key;
	pair->value = strdup(value);
	pkt->hash = y_list_append(pkt->hash, pair);
}

static int yahoo_packet_length(struct yahoo_packet *pkt)
{
	YList *l;

	int len = 0;

	for (l = pkt->hash; l; l = l->next) {
		struct yahoo_pair *pair = l->data;
		int tmp = pair->key;
		do {
			tmp /= 10;
			len++;
		} while (tmp);
		len += 2;
		len += strlen(pair->value);
		len += 2;
	}

	return len;
}

#define yahoo_put16(buf, data) ( \
	        (*(buf) = (unsigned char) ((data) >> 8) & 0xff), \
	        (*((buf) + 1) = (unsigned char) (data) & 0xff),  \
	        2)
#define yahoo_get16(buf) ((((*(buf)) & 0xff) << 8) + ((*((buf) + 1)) & 0xff))
#define yahoo_put32(buf, data) ( \
	        (*((buf)) = (unsigned char) ((data) >> 24) & 0xff), \
	        (*((buf) + 1) = (unsigned char) ((data) >> 16) & 0xff), \
	        (*((buf) + 2) = (unsigned char) ((data) >> 8) & 0xff), \
	        (*((buf) + 3) = (unsigned char) (data) & 0xff), \
	        4)
#define yahoo_get32(buf) ((((*(buf)) & 0xff) << 24) + \
	                  (((*((buf) + 1)) & 0xff) << 16) + \
	                  (((*((buf) + 2)) & 0xff) << 8) + \
	                  (((*((buf) + 3)) & 0xff)))

static void yahoo_packet_read(struct yahoo_packet *pkt, unsigned char *data,
                              int len)
{
	int pos = 0;

	while (pos + 1 < len) {
		char *key, *value = NULL;
		int accept;
		int x;

		struct yahoo_pair *pair = y_new0(struct yahoo_pair, 1);

		key = malloc(len + 1);
		x = 0;
		while (pos + 1 < len) {
			if (data[pos] == 0xc0 && data[pos + 1] == 0x80) {
				break;
			}
			key[x++] = data[pos++];
		}
		key[x] = 0;
		pos += 2;
		pair->key = strtol(key, NULL, 10);
		free(key);

		/* Libyahoo2 developer(s) don't seem to have the time to fix
		   this problem, so for now try to work around it:

		   Sometimes we receive an invalid packet with not any more
		   data at this point. I don't know how to handle this in a
		   clean way, but let's hope this is clean enough: */

		if (pos + 1 < len) {
			accept = x;
			/* if x is 0 there was no key, so don't accept it */
			if (accept) {
				value = malloc(len - pos + 1);
			}
			x = 0;
			while (pos + 1 < len) {
				if (data[pos] == 0xc0 && data[pos + 1] == 0x80) {
					break;
				}
				if (accept) {
					value[x++] = data[pos++];
				}
			}
			if (accept) {
				value[x] = 0;
			}
			pos += 2;
		} else {
			accept = 0;
		}

		if (accept) {
			pair->value = strdup(value);
			FREE(value);
			pkt->hash = y_list_append(pkt->hash, pair);
			DEBUG_MSG(("Key: %d  \tValue: %s", pair->key,
			           pair->value));
		} else {
			FREE(pair);
		}
	}
}

static void yahoo_packet_write(struct yahoo_packet *pkt, unsigned char *data)
{
	YList *l;
	int pos = 0;

	for (l = pkt->hash; l; l = l->next) {
		struct yahoo_pair *pair = l->data;
		unsigned char buf[100];

		snprintf((char *) buf, sizeof(buf), "%d", pair->key);
		strcpy((char *) data + pos, (char *) buf);
		pos += strlen((char *) buf);
		data[pos++] = 0xc0;
		data[pos++] = 0x80;

		strcpy((char *) data + pos, pair->value);
		pos += strlen(pair->value);
		data[pos++] = 0xc0;
		data[pos++] = 0x80;
	}
}

static void yahoo_dump_unhandled(struct yahoo_packet *pkt)
{
	YList *l;

	NOTICE(("Service: 0x%02x\tStatus: %d", pkt->service, pkt->status));
	for (l = pkt->hash; l; l = l->next) {
		struct yahoo_pair *pair = l->data;
		NOTICE(("\t%d => %s", pair->key, pair->value));
	}
}

static void yahoo_packet_dump(unsigned char *data, int len)
{
	if (yahoo_get_log_level() >= YAHOO_LOG_DEBUG) {
		int i;
		for (i = 0; i < len; i++) {
			if ((i % 8 == 0) && i) {
				YAHOO_CALLBACK (ext_yahoo_log) (" ");
			}
			if ((i % 16 == 0) && i) {
				YAHOO_CALLBACK (ext_yahoo_log) ("\n");
			}
			YAHOO_CALLBACK (ext_yahoo_log) ("%02x ", data[i]);
		}
		YAHOO_CALLBACK (ext_yahoo_log) ("\n");
		for (i = 0; i < len; i++) {
			if ((i % 8 == 0) && i) {
				YAHOO_CALLBACK (ext_yahoo_log) (" ");
			}
			if ((i % 16 == 0) && i) {
				YAHOO_CALLBACK (ext_yahoo_log) ("\n");
			}
			if (isprint(data[i])) {
				YAHOO_CALLBACK (ext_yahoo_log) (" %c ", data[i]);
			} else {
				YAHOO_CALLBACK (ext_yahoo_log) (" . ");
			}
		}
		YAHOO_CALLBACK (ext_yahoo_log) ("\n");
	}
}

/* yahoo's variant of base64 */
static void to_y64(unsigned char *out, const unsigned char *in, int inlen)
{
	char *encoded = base64_encode(in, inlen);
	int i = 0;

	do {
		if (encoded[i] == '+') {
			out[i] = '.';
		} else if (encoded[i] == '/') {
			out[i] = '_';
		} else if (encoded[i] == '=') {
			out[i] = '-';
		} else {
			out[i] = encoded[i];
		}
	} while (encoded[i++]);

	g_free(encoded);
}

static void yahoo_add_to_send_queue(struct yahoo_input_data *yid, void *data,
                                    int length)
{
	struct data_queue *tx = y_new0(struct data_queue, 1);

	tx->queue = y_new0(unsigned char, length);
	tx->len = length;
	memcpy(tx->queue, data, length);

	yid->txqueues = y_list_append(yid->txqueues, tx);

	if (!yid->write_tag) {
		yid->write_tag =
		        YAHOO_CALLBACK (ext_yahoo_add_handler) (yid->yd->
		                                                client_id, yid->fd, YAHOO_INPUT_WRITE, yid);
	}
}

static void yahoo_send_packet(struct yahoo_input_data *yid,
                              struct yahoo_packet *pkt, int extra_pad)
{
	int pktlen = yahoo_packet_length(pkt);
	int len = YAHOO_PACKET_HDRLEN + pktlen;
	unsigned char *data;
	int pos = 0;

	if (yid->fd < 0) {
		return;
	}

	data = y_new0(unsigned char, len + 1);

	memcpy(data + pos, "YMSG", 4);
	pos += 4;
	pos += yahoo_put16(data + pos, YAHOO_PROTO_VER);        /* version [latest 12 0x000c] */
	pos += yahoo_put16(data + pos, 0x0000); /* HIWORD pkt length??? */
	pos += yahoo_put16(data + pos, pktlen + extra_pad);     /* LOWORD pkt length? */
	pos += yahoo_put16(data + pos, pkt->service);   /* service */
	pos += yahoo_put32(data + pos, pkt->status);    /* status [4bytes] */
	pos += yahoo_put32(data + pos, pkt->id);        /* session [4bytes] */

	yahoo_packet_write(pkt, data + pos);

	yahoo_packet_dump(data, len);

	if (yid->type == YAHOO_CONNECTION_FT) {
		yahoo_send_data(yid->fd, data, len);
	} else {
		yahoo_add_to_send_queue(yid, data, len);
	}
	FREE(data);
}

static void yahoo_packet_free(struct yahoo_packet *pkt)
{
	while (pkt->hash) {
		struct yahoo_pair *pair = pkt->hash->data;
		YList *tmp;
		FREE(pair->value);
		FREE(pair);
		tmp = pkt->hash;
		pkt->hash = y_list_remove_link(pkt->hash, pkt->hash);
		y_list_free_1(tmp);
	}
	FREE(pkt);
}

static int yahoo_send_data(void *fd, void *data, int len)
{
	int ret;
	int e;

	if (fd == NULL) {
		return -1;
	}

	yahoo_packet_dump(data, len);

	do {
		ret = YAHOO_CALLBACK (ext_yahoo_write) (fd, data, len);
	} while (ret == -1 && errno == EINTR);
	e = errno;

	if (ret == -1) {
		LOG(("wrote data: ERR %s", strerror(errno)));
	} else {
		LOG(("wrote data: OK"));
	}

	errno = e;
	return ret;
}

void yahoo_close(int id)
{
	struct yahoo_data *yd = find_conn_by_id(id);

	if (!yd) {
		return;
	}

	del_from_list(yd);

	yahoo_free_data(yd);
	if (id == last_id) {
		last_id--;
	}
}

static void yahoo_input_close(struct yahoo_input_data *yid)
{
	inputs = y_list_remove(inputs, yid);

	LOG(("yahoo_input_close(read)"));
	YAHOO_CALLBACK (ext_yahoo_remove_handler) (yid->yd->client_id,
	                                           yid->read_tag);
	LOG(("yahoo_input_close(write)"));
	YAHOO_CALLBACK (ext_yahoo_remove_handler) (yid->yd->client_id,
	                                           yid->write_tag);
	yid->read_tag = yid->write_tag = 0;
	if (yid->fd) {
		YAHOO_CALLBACK (ext_yahoo_close) (yid->fd);
	}
	yid->fd = 0;
	FREE(yid->rxqueue);
	if (count_inputs_with_id(yid->yd->client_id) == 0) {
		LOG(("closing %d", yid->yd->client_id));
		yahoo_close(yid->yd->client_id);
	}
	yahoo_free_webcam(yid->wcm);
	if (yid->wcd) {
		FREE(yid->wcd);
	}
	if (yid->ys) {
		FREE(yid->ys->lsearch_text);
		FREE(yid->ys);
	}
	FREE(yid);
}

static int is_same_bud(const void *a, const void *b)
{
	const struct yahoo_buddy *subject = a;
	const struct yahoo_buddy *object = b;

	return strcmp(subject->id, object->id);
}

static char *getcookie(char *rawcookie)
{
	char *cookie = NULL;
	char *tmpcookie;
	char *cookieend;

	if (strlen(rawcookie) < 2) {
		return NULL;
	}

	tmpcookie = strdup(rawcookie + 2);
	cookieend = strchr(tmpcookie, ';');

	if (cookieend) {
		*cookieend = '\0';
	}

	cookie = strdup(tmpcookie);
	FREE(tmpcookie);
	/* cookieend=NULL;  not sure why this was there since the value is not preserved in the stack -dd */

	return cookie;
}

static char *getlcookie(char *cookie)
{
	char *tmp;
	char *tmpend;
	char *login_cookie = NULL;

	tmpend = strstr(cookie, "n=");
	if (tmpend) {
		tmp = strdup(tmpend + 2);
		tmpend = strchr(tmp, '&');
		if (tmpend) {
			*tmpend = '\0';
		}
		login_cookie = strdup(tmp);
		FREE(tmp);
	}

	return login_cookie;
}

static void yahoo_process_notify(struct yahoo_input_data *yid,
                                 struct yahoo_packet *pkt)
{
	struct yahoo_data *yd = yid->yd;
	char *msg = NULL;
	char *from = NULL;
	char *to = NULL;
	int stat = 0;
	int accept = 0;
	char *ind = NULL;
	YList *l;

	for (l = pkt->hash; l; l = l->next) {
		struct yahoo_pair *pair = l->data;
		if (pair->key == 4) {
			from = pair->value;
		}
		if (pair->key == 5) {
			to = pair->value;
		}
		if (pair->key == 49) {
			msg = pair->value;
		}
		if (pair->key == 13) {
			stat = atoi(pair->value);
		}
		if (pair->key == 14) {
			ind = pair->value;
		}
		if (pair->key == 16) {  /* status == -1 */
			NOTICE((pair->value));
			return;
		}

	}

	if (!msg) {
		return;
	}

	if (!strncasecmp(msg, "TYPING", strlen("TYPING"))) {
		YAHOO_CALLBACK (ext_yahoo_typing_notify) (yd->client_id, to,
		                                          from, stat);
	} else if (!strncasecmp(msg, "GAME", strlen("GAME"))) {
		YAHOO_CALLBACK (ext_yahoo_game_notify) (yd->client_id, to, from,
		                                        stat, ind);
	} else if (!strncasecmp(msg, "WEBCAMINVITE", strlen("WEBCAMINVITE"))) {
		if (!strcmp(ind, " ")) {
			YAHOO_CALLBACK (ext_yahoo_webcam_invite) (yd->client_id,
			                                          to, from);
		} else {
			accept = atoi(ind);
			/* accept the invitation (-1 = deny 1 = accept) */
			if (accept < 0) {
				accept = 0;
			}
			YAHOO_CALLBACK (ext_yahoo_webcam_invite_reply) (yd->
			                                                client_id, to, from, accept);
		}
	} else {
		LOG(("Got unknown notification: %s", msg));
	}
}

static void yahoo_process_conference(struct yahoo_input_data *yid,
                                     struct yahoo_packet *pkt)
{
	struct yahoo_data *yd = yid->yd;
	char *msg = NULL;
	char *host = NULL;
	char *who = NULL;
	char *room = NULL;
	char *id = NULL;
	int utf8 = 0;
	YList *members = NULL;
	YList *l;

	for (l = pkt->hash; l; l = l->next) {
		struct yahoo_pair *pair = l->data;
		if (pair->key == 50) {
			host = pair->value;
		}

		if (pair->key == 52) {  /* invite */
			members = y_list_append(members, strdup(pair->value));
		}
		if (pair->key == 53) {  /* logon */
			who = pair->value;
		}
		if (pair->key == 54) {  /* decline */
			who = pair->value;
		}
		if (pair->key == 55) {  /* unavailable (status == 2) */
			who = pair->value;
		}
		if (pair->key == 56) {  /* logoff */
			who = pair->value;
		}

		if (pair->key == 57) {
			room = pair->value;
		}

		if (pair->key == 58) {  /* join message */
			msg = pair->value;
		}
		if (pair->key == 14) {  /* decline/conf message */
			msg = pair->value;
		}

		if (pair->key == 13) {
			;
		}
		if (pair->key == 16) {  /* error */
			msg = pair->value;
		}

		if (pair->key == 1) {   /* my id */
			id = pair->value;
		}
		if (pair->key == 3) {   /* message sender */
			who = pair->value;
		}

		if (pair->key == 97) {
			utf8 = atoi(pair->value);
		}
	}

	if (!room) {
		return;
	}

	if (host) {
		for (l = members; l; l = l->next) {
			char *w = l->data;
			if (!strcmp(w, host)) {
				break;
			}
		}
		if (!l) {
			members = y_list_append(members, strdup(host));
		}
	}
	/* invite, decline, join, left, message -> status == 1 */

	switch (pkt->service) {
	case YAHOO_SERVICE_CONFINVITE:
		if (pkt->status == 2) {
			;
		} else if (members) {
			YAHOO_CALLBACK (ext_yahoo_got_conf_invite) (yd->
			                                            client_id, id, host, room, msg, members);
		} else if (msg) {
			YAHOO_CALLBACK (ext_yahoo_error) (yd->client_id, msg, 0,
			                                  E_CONFNOTAVAIL);
		}
		break;
	case YAHOO_SERVICE_CONFADDINVITE:
		if (pkt->status == 1) {
			YAHOO_CALLBACK (ext_yahoo_got_conf_invite) (yd->
			                                            client_id, id, host, room, msg, members);
		}
		break;
	case YAHOO_SERVICE_CONFDECLINE:
		if (who) {
			YAHOO_CALLBACK (ext_yahoo_conf_userdecline) (yd->
			                                             client_id, id, who, room, msg);
		}
		break;
	case YAHOO_SERVICE_CONFLOGON:
		if (who) {
			YAHOO_CALLBACK (ext_yahoo_conf_userjoin) (yd->client_id,
			                                          id, who, room);
		}
		break;
	case YAHOO_SERVICE_CONFLOGOFF:
		if (who) {
			YAHOO_CALLBACK (ext_yahoo_conf_userleave) (yd->client_id,
			                                           id, who, room);
		}
		break;
	case YAHOO_SERVICE_CONFMSG:
		if (who) {
			YAHOO_CALLBACK (ext_yahoo_conf_message) (yd->client_id,
			                                         id, who, room, msg, utf8);
		}
		break;
	}
}

static void yahoo_process_chat(struct yahoo_input_data *yid,
                               struct yahoo_packet *pkt)
{
	char *msg = NULL;
	char *id = NULL;
	char *who = NULL;
	char *room = NULL;
	char *topic = NULL;
	YList *members = NULL;
	struct yahoo_chat_member *currentmember = NULL;
	int msgtype = 1;
	int utf8 = 0;
	int firstjoin = 0;
	int membercount = 0;
	int chaterr = 0;
	YList *l;

	yahoo_dump_unhandled(pkt);
	for (l = pkt->hash; l; l = l->next) {
		struct yahoo_pair *pair = l->data;

		if (pair->key == 1) {
			/* My identity */
			id = pair->value;
		}

		if (pair->key == 104) {
			/* Room name */
			room = pair->value;
		}

		if (pair->key == 105) {
			/* Room topic */
			topic = pair->value;
		}

		if (pair->key == 108) {
			/* Number of members in this packet */
			membercount = atoi(pair->value);
		}

		if (pair->key == 109) {
			/* message sender */
			who = pair->value;

			if (pkt->service == YAHOO_SERVICE_CHATJOIN) {
				currentmember =
				        y_new0(struct yahoo_chat_member, 1);
				currentmember->id = strdup(pair->value);
				members = y_list_append(members, currentmember);
			}
		}

		if (pair->key == 110) {
			/* age */
			if (pkt->service == YAHOO_SERVICE_CHATJOIN) {
				currentmember->age = atoi(pair->value);
			}
		}

		if (pair->key == 113) {
			/* attribs */
			if (pkt->service == YAHOO_SERVICE_CHATJOIN) {
				currentmember->attribs = atoi(pair->value);
			}
		}

		if (pair->key == 141) {
			/* alias */
			if (pkt->service == YAHOO_SERVICE_CHATJOIN) {
				currentmember->alias = strdup(pair->value);
			}
		}

		if (pair->key == 142) {
			/* location */
			if (pkt->service == YAHOO_SERVICE_CHATJOIN) {
				currentmember->location = strdup(pair->value);
			}
		}

		if (pair->key == 130) {
			/* first join */
			firstjoin = 1;
		}

		if (pair->key == 117) {
			/* message */
			msg = pair->value;
		}

		if (pair->key == 124) {
			/* Message type */
			msgtype = atoi(pair->value);
		}
		if (pair->key == 114) {
			/* message error not sure what all the pair values mean */
			/* but -1 means no session in room */
			chaterr = atoi(pair->value);
		}
	}

	if (!room) {
		if (pkt->service == YAHOO_SERVICE_CHATLOGOUT) { /* yahoo originated chat logout */
			YAHOO_CALLBACK (ext_yahoo_chat_yahoologout) (yid->yd->
			                                             client_id, id);
			return;
		}
		if (pkt->service == YAHOO_SERVICE_COMMENT && chaterr) {
			YAHOO_CALLBACK (ext_yahoo_chat_yahooerror) (yid->yd->
			                                            client_id, id);
			return;
		}

		WARNING(("We didn't get a room name, ignoring packet"));
		return;
	}

	switch (pkt->service) {
	case YAHOO_SERVICE_CHATJOIN:
		if (y_list_length(members) != membercount) {
			WARNING(("Count of members doesn't match No. of members we got"));
		}
		if (firstjoin && members) {
			YAHOO_CALLBACK (ext_yahoo_chat_join) (yid->yd->client_id,
			                                      id, room, topic, members, yid->fd);
		} else if (who) {
			if (y_list_length(members) != 1) {
				WARNING(("Got more than 1 member on a normal join"));
			}
			/* this should only ever have one, but just in case */
			while (members) {
				YList *n = members->next;
				currentmember = members->data;
				YAHOO_CALLBACK (ext_yahoo_chat_userjoin) (yid->
				                                          yd->client_id, id, room, currentmember);
				y_list_free_1(members);
				members = n;
			}
		}
		break;
	case YAHOO_SERVICE_CHATEXIT:
		if (who) {
			YAHOO_CALLBACK (ext_yahoo_chat_userleave) (yid->yd->
			                                           client_id, id, room, who);
		}
		break;
	case YAHOO_SERVICE_COMMENT:
		if (who) {
			YAHOO_CALLBACK (ext_yahoo_chat_message) (yid->yd->
			                                         client_id, id, who, room, msg, msgtype, utf8);
		}
		break;
	}
}

static void yahoo_process_message(struct yahoo_input_data *yid,
                                  struct yahoo_packet *pkt)
{
	struct yahoo_data *yd = yid->yd;
	YList *l;
	YList *messages = NULL;

	struct m {
		int i_31;
		int i_32;
		char *to;
		char *from;
		long tm;
		char *msg;
		int utf8;
		char *gunk;
	} *message = y_new0(struct m, 1);

	for (l = pkt->hash; l; l = l->next) {
		struct yahoo_pair *pair = l->data;
		if (pair->key == 1 || pair->key == 4) {
			if (!message->from) {
				message->from = pair->value;
			}
		} else if (pair->key == 5) {
			message->to = pair->value;
		} else if (pair->key == 15) {
			message->tm = strtol(pair->value, NULL, 10);
		} else if (pair->key == 97) {
			message->utf8 = atoi(pair->value);
		}
		/* This comes when the official client sends us a message */
		else if (pair->key == 429) {
			message->gunk = pair->value;
		}
		/* user message *//* sys message */
		else if (pair->key == 14 || pair->key == 16) {
			message->msg = pair->value;
		} else if (pair->key == 31) {
			if (message->i_31) {
				messages = y_list_append(messages, message);
				message = y_new0(struct m, 1);
			}
			message->i_31 = atoi(pair->value);
		} else if (pair->key == 32) {
			message->i_32 = atoi(pair->value);
		} else {
			LOG(("yahoo_process_message: status: %d, key: %d, value: %s", pkt->status, pair->key,
			     pair->value));
		}
	}

	messages = y_list_append(messages, message);

	for (l = messages; l; l = l->next) {
		message = l->data;
		if (pkt->service == YAHOO_SERVICE_SYSMESSAGE) {
			YAHOO_CALLBACK (ext_yahoo_system_message) (yd->client_id,
			                                           message->to, message->from, message->msg);
		} else if (pkt->status <= 2 || pkt->status == 5) {
			/* Confirm message receipt if we got the gunk */
			if (message->gunk) {
				struct yahoo_packet *outpkt;

				outpkt = yahoo_packet_new(YAHOO_SERVICE_MESSAGE_CONFIRM,
				                          YPACKET_STATUS_DEFAULT, 0);
				yahoo_packet_hash(outpkt, 1, yd->user);
				yahoo_packet_hash(outpkt, 5, message->from);
				yahoo_packet_hash(outpkt, 302, "430");
				yahoo_packet_hash(outpkt, 430, message->gunk);
				yahoo_packet_hash(outpkt, 303, "430");
				yahoo_packet_hash(outpkt, 450, "0");
				yahoo_send_packet(yid, outpkt, 0);

				yahoo_packet_free(outpkt);
			}

			if (!strcmp(message->msg, "<ding>")) {
				YAHOO_CALLBACK (ext_yahoo_got_buzz) (yd->client_id,
				                                     message->to, message->from, message->tm);
			} else {
				YAHOO_CALLBACK (ext_yahoo_got_im) (yd->client_id,
				                                   message->to, message->from, message->msg,
				                                   message->tm, pkt->status, message->utf8);
			}
		} else if (pkt->status == 0xffffffff) {
			YAHOO_CALLBACK (ext_yahoo_error) (yd->client_id,
			                                  message->msg, 0, E_SYSTEM);
		}
		FREE(message);
	}

	y_list_free(messages);
}

/*
 * Here's what multi-level packets look like. Data in brackets is the value.
 *
 * 3 level:
 * =======
 *
 * 302 (318) - Beginning level 1
 *      300 (318) - Begin level 2
 *      302 (319) - End level 2 header
 *              300 (319) - Begin level 3
 *              301 (319) - End level 3
 *      303 (319) - End level 2
 * 303 (318) - End level 1
 *
 * 2 level:
 * =======
 *
 * 302 (315) - Beginning level 1
 *      300 (315) - Begin level 2
 *      301 (315) - End level 2
 * 303 (315) - End level 1
 *
 */
static void yahoo_process_status(struct yahoo_input_data *yid,
                                 struct yahoo_packet *pkt)
{
	YList *l;
	struct yahoo_data *yd = yid->yd;

	struct yahoo_process_status_entry *u;

	YList *users = 0;

	if (pkt->service == YAHOO_SERVICE_LOGOFF && pkt->status == -1) {
		YAHOO_CALLBACK (ext_yahoo_login_response) (yd->client_id,
		                                           YAHOO_LOGIN_DUPL, NULL);
		return;
	}

	/*
	 * Status updates may be spread across multiple packets and not
	 * even on buddy boundaries, so keeping some state is important.
	 * So, continue where we left off, and only add a user entry to
	 * the list once it's complete (301-315 End buddy).
	 */
	u = yd->half_user;

	for (l = pkt->hash; l; l = l->next) {
		struct yahoo_pair *pair = l->data;

		switch (pair->key) {
		case 300:       /* Begin buddy */
			if (!strcmp(pair->value, "315") && !u) {
				u = yd->half_user = y_new0(struct yahoo_process_status_entry, 1);
			}
			break;
		case 301:       /* End buddy */
			if (!strcmp(pair->value, "315") && u) {
				/* Sometimes user info comes in an odd format with no
				   "begin buddy" but *with* an "end buddy". Don't add
				   it twice. */
				if (!y_list_find(users, u)) {
					users = y_list_prepend(users, u);
				}
				u = yd->half_user = NULL;
			}
			break;
		case 0: /* we won't actually do anything with this */
			NOTICE(("key %d:%s", pair->key, pair->value));
			break;
		case 1: /* we don't get the full buddy list here. */
			if (!yd->logged_in) {
				yd->logged_in = 1;
				if (yd->current_status < 0) {
					yd->current_status = yd->initial_status;
				}
				YAHOO_CALLBACK (ext_yahoo_login_response) (yd->
				                                           client_id, YAHOO_LOGIN_OK, NULL);
			}
			break;
		case 8: /* how many online buddies we have */
			NOTICE(("key %d:%s", pair->key, pair->value));
			break;
		case 7: /* the current buddy */
			if (!u) {
				/* This will only happen in case of a single level message */
				u = y_new0(struct yahoo_process_status_entry, 1);
				users = y_list_prepend(users, u);
			}
			u->name = pair->value;
			break;
		case 10:        /* state */
			u->state = strtol(pair->value, NULL, 10);
			break;
		case 19:        /* custom status message */
			u->msg = pair->value;
			break;
		case 47:        /* is it an away message or not. Not applicable for YMSG16 anymore */
			u->away = atoi(pair->value);
			break;
		case 137:       /* seconds idle */
			u->idle = atoi(pair->value);
			break;
		case 11:        /* this is the buddy's session id */
			u->buddy_session = atoi(pair->value);
			break;
		case 17:        /* in chat? */
			u->f17 = atoi(pair->value);
			break;
		case 13:        /* bitmask, bit 0 = pager, bit 1 = chat, bit 2 = game */
			u->flags = atoi(pair->value);
			break;
		case 60:        /* SMS -> 1 MOBILE USER */
			/* sometimes going offline makes this 2, but invisible never sends it */
			u->mobile = atoi(pair->value);
			break;
		case 138:
			u->f138 = atoi(pair->value);
			break;
		case 184:
			u->f184 = pair->value;
			break;
		case 192:
			u->f192 = atoi(pair->value);
			break;
		case 10001:
			u->f10001 = atoi(pair->value);
			break;
		case 10002:
			u->f10002 = atoi(pair->value);
			break;
		case 198:
			u->f198 = atoi(pair->value);
			break;
		case 197:
			u->f197 = pair->value;
			break;
		case 205:
			u->f205 = pair->value;
			break;
		case 213:
			u->f213 = atoi(pair->value);
			break;
		case 16:        /* Custom error message */
			YAHOO_CALLBACK (ext_yahoo_error) (yd->client_id,
			                                  pair->value, 0, E_CUSTOM);
			break;
		default:
			WARNING(("unknown status key %d:%s", pair->key,
			         pair->value));
			break;
		}
	}

	while (users) {
		YList *t = users;
		struct yahoo_process_status_entry *u = users->data;

		if (u->name != NULL) {
			if (pkt->service ==
			    YAHOO_SERVICE_LOGOFF
			    /*|| u->flags == 0 No flags for YMSG16 */) {
				YAHOO_CALLBACK (ext_yahoo_status_changed) (yd->
				                                           client_id, u->name,
				                                           YAHOO_STATUS_OFFLINE, NULL, 1, 0, 0);
			} else {
				/* Key 47 always seems to be 1 for YMSG16 */
				if (!u->state) {
					u->away = 0;
				} else {
					u->away = 1;
				}

				YAHOO_CALLBACK (ext_yahoo_status_changed) (yd->
				                                           client_id, u->name, u->state, u->msg,
				                                           u->away, u->idle, u->mobile);
			}
		}

		users = y_list_remove_link(users, users);
		y_list_free_1(t);
		FREE(u);
	}
}

static void yahoo_process_buddy_list(struct yahoo_input_data *yid,
                                     struct yahoo_packet *pkt)
{
	struct yahoo_data *yd = yid->yd;
	YList *l;
	int last_packet = 0;
	char *cur_group = NULL;
	struct yahoo_buddy *newbud = NULL;

	/* we could be getting multiple packets here */
	for (l = pkt->hash; l; l = l->next) {
		struct yahoo_pair *pair = l->data;

		switch (pair->key) {
		case 300:
		case 301:
		case 302:
			break;  /* Separators. Our logic does not need them */
		case 303:
			if (318 == atoi(pair->value)) {
				last_packet = 1;
			}
			break;
		case 65:
			cur_group = strdup(pair->value);
			break;
		case 7:
			newbud = y_new0(struct yahoo_buddy, 1);
			newbud->id = strdup(pair->value);
			if (cur_group) {
				newbud->group = strdup(cur_group);
			} else if (yd->buddies) {
				struct yahoo_buddy *lastbud =
				        (struct yahoo_buddy *) y_list_nth(yd->
				                                          buddies,
				                                          y_list_length(yd->buddies) - 1)->data;
				newbud->group = strdup(lastbud->group);
			} else {
				newbud->group = strdup("Buddies");
			}

			yd->buddies = y_list_append(yd->buddies, newbud);

			break;
		}
	}

	/* we could be getting multiple packets here */
	if (pkt->hash && !last_packet) {
		return;
	}

	YAHOO_CALLBACK (ext_yahoo_got_buddies) (yd->client_id, yd->buddies);

	/* Logged in */
	if (!yd->logged_in) {
		yd->logged_in = 1;
		if (yd->current_status < 0) {
			yd->current_status = yd->initial_status;
		}
		YAHOO_CALLBACK (ext_yahoo_login_response) (yd->client_id,
		                                           YAHOO_LOGIN_OK, NULL);

		/*
		yahoo_set_away(yd->client_id, yd->initial_status, NULL,
		        (yd->initial_status == YAHOO_STATUS_AVAILABLE) ? 0 : 1);

		yahoo_get_yab(yd->client_id);
		*/
	}

}

static void yahoo_process_list(struct yahoo_input_data *yid,
                               struct yahoo_packet *pkt)
{
	struct yahoo_data *yd = yid->yd;
	YList *l;

	/* we could be getting multiple packets here */
	for (l = pkt->hash; l; l = l->next) {
		struct yahoo_pair *pair = l->data;

		switch (pair->key) {
		case 89:        /* identities */
		{
			char **identities =
			        y_strsplit(pair->value, ",", -1);
			int i;
			for (i = 0; identities[i]; i++) {
				yd->identities =
				        y_list_append(yd->identities,
				                      strdup(identities[i]));
			}
			y_strfreev(identities);
		}
			YAHOO_CALLBACK (ext_yahoo_got_identities) (yd->client_id,
			                                           yd->identities);
			break;
		case 59:        /* cookies */
			if (pair->value[0] == 'Y') {
				FREE(yd->cookie_y);
				FREE(yd->login_cookie);

				yd->cookie_y = getcookie(pair->value);
				yd->login_cookie = getlcookie(yd->cookie_y);

			} else if (pair->value[0] == 'T') {
				FREE(yd->cookie_t);
				yd->cookie_t = getcookie(pair->value);

			} else if (pair->value[0] == 'C') {
				FREE(yd->cookie_c);
				yd->cookie_c = getcookie(pair->value);
			}

			break;
		case 3: /* my id */
		case 90:        /* 1 */
		case 100:       /* 0 */
		case 101:       /* NULL */
		case 102:       /* NULL */
		case 93:        /* 86400/1440 */
			break;
		}
	}

	if (yd->cookie_y && yd->cookie_t) {     /* We don't get cookie_c anymore */
		YAHOO_CALLBACK (ext_yahoo_got_cookies) (yd->client_id);
	}
}

static void yahoo_process_verify(struct yahoo_input_data *yid,
                                 struct yahoo_packet *pkt)
{
	struct yahoo_data *yd = yid->yd;

	if (pkt->status != 0x01) {
		DEBUG_MSG(("expected status: 0x01, got: %d", pkt->status));
		YAHOO_CALLBACK (ext_yahoo_login_response) (yd->client_id,
		                                           YAHOO_LOGIN_LOCK, "");
		return;
	}

	pkt = yahoo_packet_new(YAHOO_SERVICE_AUTH, YPACKET_STATUS_DEFAULT,
	                       yd->session_id);

	yahoo_packet_hash(pkt, 1, yd->user);
	yahoo_send_packet(yid, pkt, 0);

	yahoo_packet_free(pkt);

}

static void yahoo_process_picture_checksum(struct yahoo_input_data *yid,
                                           struct yahoo_packet *pkt)
{
	struct yahoo_data *yd = yid->yd;
	char *from = NULL;
	char *to = NULL;
	int checksum = 0;
	YList *l;

	for (l = pkt->hash; l; l = l->next) {
		struct yahoo_pair *pair = l->data;

		switch (pair->key) {
		case 1:
		case 4:
			from = pair->value;
		case 5:
			to = pair->value;
			break;
		case 212:
			break;
		case 192:
			checksum = atoi(pair->value);
			break;
		}
	}

	YAHOO_CALLBACK (ext_yahoo_got_buddyicon_checksum) (yd->client_id, to,
	                                                   from, checksum);
}

static void yahoo_process_picture(struct yahoo_input_data *yid,
                                  struct yahoo_packet *pkt)
{
	struct yahoo_data *yd = yid->yd;
	char *url = NULL;
	char *from = NULL;
	char *to = NULL;
	int status = 0;
	int checksum = 0;
	YList *l;

	for (l = pkt->hash; l; l = l->next) {
		struct yahoo_pair *pair = l->data;

		switch (pair->key) {
		case 1:
		case 4: /* sender */
			from = pair->value;
			break;
		case 5: /* we */
			to = pair->value;
			break;
		case 13:        /* request / sending */
			status = atoi(pair->value);
			break;
		case 20:        /* url */
			url = pair->value;
			break;
		case 192:       /*checksum */
			checksum = atoi(pair->value);
			break;
		}
	}

	switch (status) {
	case 1:         /* this is a request, ignore for now */
		YAHOO_CALLBACK (ext_yahoo_got_buddyicon_request) (yd->client_id,
		                                                  to, from);
		break;
	case 2:         /* this is cool - we get a picture :) */
		YAHOO_CALLBACK (ext_yahoo_got_buddyicon) (yd->client_id, to,
		                                          from, url, checksum);
		break;
	}
}

static void yahoo_process_picture_upload(struct yahoo_input_data *yid,
                                         struct yahoo_packet *pkt)
{
	struct yahoo_data *yd = yid->yd;
	YList *l;
	char *url = NULL;

	if (pkt->status != 1) {
		return;         /* something went wrong */

	}
	for (l = pkt->hash; l; l = l->next) {
		struct yahoo_pair *pair = l->data;

		switch (pair->key) {
		case 5: /* we */
			break;
		case 20:        /* url */
			url = pair->value;
			break;
		case 27:        /* local filename */
			break;
		case 38:        /* time */
			break;
		}
	}

	YAHOO_CALLBACK (ext_yahoo_buddyicon_uploaded) (yd->client_id, url);
}

void yahoo_login(int id, int initial)
{
	struct yahoo_data *yd = find_conn_by_id(id);
	struct connect_callback_data *ccd;
	struct yahoo_server_settings *yss;
	int tag;

	char *host;

	struct yahoo_input_data *yid = y_new0(struct yahoo_input_data, 1);

	yid->yd = yd;
	yid->type = YAHOO_CONNECTION_PAGER;
	inputs = y_list_prepend(inputs, yid);

	yd->initial_status = initial;
	yss = yd->server_settings;

	ccd = y_new0(struct connect_callback_data, 1);
	ccd->yd = yd;

	host = yss->pager_host;

	if (!host) {
		host = yss->pager_host_list[0];
	}

	tag = YAHOO_CALLBACK (ext_yahoo_connect_async) (yd->client_id,
	                                                host, yss->pager_port, yahoo_connected, ccd, 0);

	/*
	 * if tag <= 0, then callback has already been called
	 * so ccd will have been freed
	 */
	if (tag > 0) {
		ccd->tag = tag;
	} else if (tag < 0) {
		YAHOO_CALLBACK (ext_yahoo_login_response) (yd->client_id,
		                                           YAHOO_LOGIN_SOCK, NULL);
	}
}

struct yahoo_https_auth_data {
	struct yahoo_input_data *yid;
	char *token;
	char *chal;
};

static void yahoo_https_auth_token_init(struct yahoo_https_auth_data *had);
static void yahoo_https_auth_token_finish(struct http_request *req);
static void yahoo_https_auth_init(struct yahoo_https_auth_data *had);
static void yahoo_https_auth_finish(struct http_request *req);

/* Extract a value from a login.yahoo.com response. Assume CRLF-linebreaks
   and FAIL miserably if they're not there... */
static char *yahoo_ha_find_key(char *response, char *key)
{
	char *s, *end;
	int len = strlen(key);

	s = response;
	do {
		if (strncmp(s, key, len) == 0 && s[len] == '=') {
			s += len + 1;
			if ((end = strchr(s, '\r'))) {
				return g_strndup(s, end - s);
			} else {
				return g_strdup(s);
			}
		}

		if ((s = strchr(s, '\n'))) {
			s++;
		}
	} while (s && *s);

	return NULL;
}

static enum yahoo_status yahoo_https_status_parse(int code)
{
	switch (code) {
	case 1212: return (enum yahoo_status) YAHOO_LOGIN_PASSWD;
	case 1213: return (enum yahoo_status) YAHOO_LOGIN_LOCK;
	case 1235: return (enum yahoo_status) YAHOO_LOGIN_UNAME;
	default: return (enum yahoo_status) code;
	}
}

static void yahoo_https_auth(struct yahoo_input_data *yid, const char *seed, const char *sn)
{
	struct yahoo_https_auth_data *had = g_new0(struct yahoo_https_auth_data, 1);

	had->yid = yid;
	had->chal = g_strdup(seed);

	yahoo_https_auth_token_init(had);
}

static void yahoo_https_auth_token_init(struct yahoo_https_auth_data *had)
{
	struct yahoo_input_data *yid = had->yid;
	struct yahoo_data *yd = yid->yd;
	char *login, *passwd, *chal;
	char *url;

	login = g_strndup(yd->user, 3 * strlen(yd->user));
	http_encode(login);
	passwd = g_strndup(yd->password, 3 * strlen(yd->password));
	http_encode(passwd);
	chal = g_strndup(had->chal, 3 * strlen(had->chal));
	http_encode(chal);

	url = g_strdup_printf("https://login.yahoo.com/config/pwtoken_get?src=ymsgr&ts=%d&login=%s&passwd=%s&chal=%s",
	                      (int) time(NULL), login, passwd, chal);

	http_dorequest_url(url, yahoo_https_auth_token_finish, had);

	g_free(url);
	g_free(chal);
	g_free(passwd);
	g_free(login);
}

static void yahoo_https_auth_token_finish(struct http_request *req)
{
	struct yahoo_https_auth_data *had = req->data;
	struct yahoo_input_data *yid;
	struct yahoo_data *yd;
	int st;

	if (y_list_find(inputs, had->yid) == NULL) {
		return;
	}

	yid = had->yid;
	yd = yid->yd;

	if (req->status_code != 200) {
		YAHOO_CALLBACK (ext_yahoo_login_response) (yd->client_id, 2000 + req->status_code, NULL);
		goto fail;
	}

	if (sscanf(req->reply_body, "%d", &st) != 1 || st != 0) {
		YAHOO_CALLBACK (ext_yahoo_login_response) (yd->client_id, yahoo_https_status_parse(st), NULL);
		goto fail;
	}

	if ((had->token = yahoo_ha_find_key(req->reply_body, "ymsgr")) == NULL) {
		YAHOO_CALLBACK (ext_yahoo_login_response) (yd->client_id, 3001, NULL);
		goto fail;
	}

	yahoo_https_auth_init(had);
	return;

fail:
	g_free(had->token);
	g_free(had->chal);
	g_free(had);
}

static void yahoo_https_auth_init(struct yahoo_https_auth_data *had)
{
	char *url;

	url = g_strdup_printf("https://login.yahoo.com/config/pwtoken_login?src=ymsgr&ts=%d&token=%s",
	                      (int) time(NULL), had->token);

	http_dorequest_url(url, yahoo_https_auth_finish, had);

	g_free(url);
}

static void yahoo_https_auth_finish(struct http_request *req)
{
	struct yahoo_https_auth_data *had = req->data;
	struct yahoo_input_data *yid;
	struct yahoo_data *yd;
	struct yahoo_packet *pack;
	char *crumb = NULL;
	int st;

	if (y_list_find(inputs, had->yid) == NULL) {
		return;
	}

	yid = had->yid;
	yd = yid->yd;

	md5_byte_t result[16];
	md5_state_t ctx;

	unsigned char yhash[32];

	if (req->status_code != 200) {
		YAHOO_CALLBACK (ext_yahoo_login_response) (yd->client_id, 2000 + req->status_code, NULL);
		goto fail;
	}

	if (sscanf(req->reply_body, "%d", &st) != 1 || st != 0) {
		YAHOO_CALLBACK (ext_yahoo_login_response) (yd->client_id, yahoo_https_status_parse(st), NULL);
		goto fail;
	}

	if ((yd->cookie_y = yahoo_ha_find_key(req->reply_body, "Y")) == NULL ||
	    (yd->cookie_t = yahoo_ha_find_key(req->reply_body, "T")) == NULL ||
	    (crumb = yahoo_ha_find_key(req->reply_body, "crumb")) == NULL) {
		YAHOO_CALLBACK (ext_yahoo_login_response) (yd->client_id, 3002, NULL);
		goto fail;
	}

	md5_init(&ctx);
	md5_append(&ctx, (unsigned char *) crumb, 11);
	md5_append(&ctx, (unsigned char *) had->chal, strlen(had->chal));
	md5_finish(&ctx, result);
	to_y64(yhash, result, 16);

	pack = yahoo_packet_new(YAHOO_SERVICE_AUTHRESP, yd->initial_status, yd->session_id);
	yahoo_packet_hash(pack, 1, yd->user);
	yahoo_packet_hash(pack, 0, yd->user);
	yahoo_packet_hash(pack, 277, yd->cookie_y);
	yahoo_packet_hash(pack, 278, yd->cookie_t);
	yahoo_packet_hash(pack, 307, (char *) yhash);
	yahoo_packet_hash(pack, 244, "524223");
	yahoo_packet_hash(pack, 2, yd->user);
	yahoo_packet_hash(pack, 2, "1");
	yahoo_packet_hash(pack, 98, "us");
	yahoo_packet_hash(pack, 135, "7.5.0.647");

	yahoo_send_packet(yid, pack, 0);

	yahoo_packet_free(pack);

fail:
	g_free(crumb);
	g_free(had->token);
	g_free(had->chal);
	g_free(had);
}

static void yahoo_process_auth(struct yahoo_input_data *yid,
                               struct yahoo_packet *pkt)
{
	char *seed = NULL;
	char *sn = NULL;
	YList *l = pkt->hash;
	int m = 0;
	struct yahoo_data *yd = yid->yd;

	while (l) {
		struct yahoo_pair *pair = l->data;

		switch (pair->key) {
		case 94:
			seed = pair->value;
			break;
		case 1:
			sn = pair->value;
			break;
		case 13:
			m = atoi(pair->value);
			break;
		}
		l = l->next;
	}

	if (!seed) {
		return;
	}

	if (m == 2) {
		yahoo_https_auth(yid, seed, sn);
	} else {
		/* call error */
		WARNING(("unknown auth type %d", m));
		YAHOO_CALLBACK (ext_yahoo_login_response) (yd->client_id,
		                                           YAHOO_LOGIN_UNKNOWN, NULL);
	}
}

static void yahoo_process_auth_resp(struct yahoo_input_data *yid,
                                    struct yahoo_packet *pkt)
{
	struct yahoo_data *yd = yid->yd;
	char *url = NULL;
	int login_status = -1;

	YList *l;

	for (l = pkt->hash; l; l = l->next) {
		struct yahoo_pair *pair = l->data;
		if (pair->key == 0) {
			; /* login_id */
		} else if (pair->key == 1) {
			; /* handle */
		} else if (pair->key == 20) {
			url = pair->value;
		} else if (pair->key == 66) {
			login_status = atoi(pair->value);
		}
	}

	if (pkt->status == YPACKET_STATUS_DISCONNECTED) {
		YAHOO_CALLBACK (ext_yahoo_login_response) (yd->client_id,
		                                           login_status, url);
		/*      yahoo_logoff(yd->client_id); */
	}
}

static void yahoo_process_mail(struct yahoo_input_data *yid,
                               struct yahoo_packet *pkt)
{
	struct yahoo_data *yd = yid->yd;
	char *who = NULL;
	char *email = NULL;
	char *subj = NULL;
	int count = 0;
	YList *l;

	for (l = pkt->hash; l; l = l->next) {
		struct yahoo_pair *pair = l->data;
		if (pair->key == 9) {
			count = strtol(pair->value, NULL, 10);
		} else if (pair->key == 43) {
			who = pair->value;
		} else if (pair->key == 42) {
			email = pair->value;
		} else if (pair->key == 18) {
			subj = pair->value;
		} else {
			LOG(("key: %d => value: %s", pair->key, pair->value));
		}
	}

	if (who && email && subj) {
		char from[1024];
		snprintf(from, sizeof(from), "%s (%s)", who, email);
		YAHOO_CALLBACK (ext_yahoo_mail_notify) (yd->client_id, from,
		                                        subj, count);
	} else if (count > 0) {
		YAHOO_CALLBACK (ext_yahoo_mail_notify) (yd->client_id, NULL,
		                                        NULL, count);
	}
}

static void yahoo_process_new_contact(struct yahoo_input_data *yid,
                                      struct yahoo_packet *pkt)
{
	struct yahoo_data *yd = yid->yd;
	char *me = NULL;
	char *who = NULL;
	char *msg = NULL;
	int online = -1;

	YList *l;

	for (l = pkt->hash; l; l = l->next) {
		struct yahoo_pair *pair = l->data;
		if (pair->key == 4) {
			who = pair->value;
		} else if (pair->key == 5) {
			me = pair->value;
		} else if (pair->key == 14) {
			msg = pair->value;
		} else if (pair->key == 13) {
			online = strtol(pair->value, NULL, 10);
		}
	}

	if (who && online < 0) {
		YAHOO_CALLBACK (ext_yahoo_contact_added) (yd->client_id, me, who,
		                                          msg);
	} else if (online == 2) {
		YAHOO_CALLBACK (ext_yahoo_rejected) (yd->client_id, who, msg);
	}
}

/* UNUSED? */
static void yahoo_process_contact(struct yahoo_input_data *yid,
                                  struct yahoo_packet *pkt)
{
	struct yahoo_data *yd = yid->yd;
	char *id = NULL;
	char *who = NULL;
	char *msg = NULL;
	char *name = NULL;
	int state = YAHOO_STATUS_AVAILABLE;
	int away = 0;
	int idle = 0;
	int mobile = 0;

	YList *l;

	for (l = pkt->hash; l; l = l->next) {
		struct yahoo_pair *pair = l->data;
		if (pair->key == 1) {
			id = pair->value;
		} else if (pair->key == 3) {
			who = pair->value;
		} else if (pair->key == 14) {
			msg = pair->value;
		} else if (pair->key == 7) {
			name = pair->value;
		} else if (pair->key == 10) {
			state = strtol(pair->value, NULL, 10);
		} else if (pair->key == 15) {
			; /* tm */
		} else if (pair->key == 13) {
			; /* online */
		} else if (pair->key == 47) {
			away = strtol(pair->value, NULL, 10);
		} else if (pair->key == 137) {
			idle = strtol(pair->value, NULL, 10);
		} else if (pair->key == 60) {
			mobile = strtol(pair->value, NULL, 10);
		}

	}

	if (id) {
		YAHOO_CALLBACK (ext_yahoo_contact_added) (yd->client_id, id, who,
		                                          msg);
	} else if (name) {
		YAHOO_CALLBACK (ext_yahoo_status_changed) (yd->client_id, name,
		                                           state, msg, away, idle, mobile);
	} else if (pkt->status == 0x07) {
		YAHOO_CALLBACK (ext_yahoo_rejected) (yd->client_id, who, msg);
	}
}

static void yahoo_process_buddyadd(struct yahoo_input_data *yid,
                                   struct yahoo_packet *pkt)
{
	struct yahoo_data *yd = yid->yd;
	char *who = NULL;
	char *where = NULL;
	int status = 0;

	struct yahoo_buddy *bud = NULL;

	YList *l;

	for (l = pkt->hash; l; l = l->next) {
		struct yahoo_pair *pair = l->data;
		if (pair->key == 1) {
			; /* Me... don't care */
		}
		if (pair->key == 7) {
			who = pair->value;
		}
		if (pair->key == 65) {
			where = pair->value;
		}
		if (pair->key == 66) {
			status = strtol(pair->value, NULL, 10);
		}
	}

	if (!who) {
		return;
	}
	if (!where) {
		where = "Unknown";
	}

	bud = y_new0(struct yahoo_buddy, 1);
	bud->id = strdup(who);
	bud->group = strdup(where);
	bud->real_name = NULL;

	yd->buddies = y_list_append(yd->buddies, bud);

#if 0
	/* BitlBee: This seems to be wrong in my experience. I think:
	   status = 0: Success
	   status = 2: Already on list
	   status = 3: Doesn't exist
	   status = 42: Invalid handle (possibly banned/reserved, I get it for
	                handles like joe or jjjjjj)
	   Haven't seen others yet. But whenever the add is successful, there
	   will be a separate "went online" packet when the auth. request is
	   accepted. Couldn't find any test account that doesn't require auth.
	   unfortunately (if there is even such a thing?) */

	/* A non-zero status (i've seen 2) seems to mean the buddy is already
	 * added and is online */
	if (status) {
		LOG(("Setting online see packet for info"));
		yahoo_dump_unhandled(pkt);
		YAHOO_CALLBACK (ext_yahoo_status_changed) (yd->client_id, who,
		                                           YAHOO_STATUS_AVAILABLE, NULL, 0, 0, 0);
	}
#endif
	/* BitlBee: Need ACK of added buddy, if it was successful. */
	if (status == 0) {
		YList *tmp = y_list_append(NULL, bud);
		YAHOO_CALLBACK (ext_yahoo_got_buddies) (yd->client_id, tmp);
		y_list_free(tmp);
	}
}

static void yahoo_process_buddydel(struct yahoo_input_data *yid,
                                   struct yahoo_packet *pkt)
{
	struct yahoo_data *yd = yid->yd;
	char *who = NULL;
	char *where = NULL;
	struct yahoo_buddy *bud;

	YList *buddy;

	YList *l;

	for (l = pkt->hash; l; l = l->next) {
		struct yahoo_pair *pair = l->data;
		if (pair->key == 1) {
			; /* Me... don't care */
		} else if (pair->key == 7) {
			who = pair->value;
		} else if (pair->key == 65) {
			where = pair->value;
		} else if (pair->key == 66) {
			; /* unk_66 */
		} else {
			DEBUG_MSG(("unknown key: %d = %s", pair->key,
			           pair->value));
		}
	}

	if (!who || !where) {
		return;
	}

	bud = y_new0(struct yahoo_buddy, 1);
	bud->id = strdup(who);
	bud->group = strdup(where);

	buddy = y_list_find_custom(yd->buddies, bud, is_same_bud);

	FREE(bud->id);
	FREE(bud->group);
	FREE(bud);

	if (buddy) {
		bud = buddy->data;
		yd->buddies = y_list_remove_link(yd->buddies, buddy);
		y_list_free_1(buddy);

		FREE(bud->id);
		FREE(bud->group);
		FREE(bud->real_name);
		FREE(bud);

		bud = NULL;
	}
}

static void yahoo_process_ignore(struct yahoo_input_data *yid,
                                 struct yahoo_packet *pkt)
{
	YList *l;

	for (l = pkt->hash; l; l = l->next) {
		struct yahoo_pair *pair = l->data;
		if (pair->key == 0) {
			; /* who */
		}
		if (pair->key == 1) {
			; /* Me... don't care */
		}
		if (pair->key == 13) {  /* 1 == ignore, 2 == unignore */
			;
		}
		if (pair->key == 66) {
			; /* status */
		}
	}

	/*
	 * status
	 *      0  - ok
	 *      2  - already in ignore list, could not add
	 *      3  - not in ignore list, could not delete
	 *      12 - is a buddy, could not add
	 */

/*	if(status)
                YAHOO_CALLBACK(ext_yahoo_error)(yd->client_id, who, 0, status);
*/
}

static void yahoo_process_voicechat(struct yahoo_input_data *yid,
                                    struct yahoo_packet *pkt)
{
	char *who = NULL;
	char *me = NULL;
	char *room = NULL;

	YList *l;

	for (l = pkt->hash; l; l = l->next) {
		struct yahoo_pair *pair = l->data;
		if (pair->key == 4) {
			who = pair->value;
		}
		if (pair->key == 5) {
			me = pair->value;
		}
		if (pair->key == 13) {
			; /* voice room */
		}
		if (pair->key == 57) {
			room = pair->value;
		}
	}

	NOTICE(("got voice chat invite from %s in %s to identity %s", who, room,
	        me));
	/*
	 * send: s:0 1:me 5:who 57:room 13:1
	 * ????  s:4 5:who 10:99 19:-1615114531
	 * gotr: s:4 5:who 10:99 19:-1615114615
	 * ????  s:1 5:me 4:who 57:room 13:3room
	 * got:  s:1 5:me 4:who 57:room 13:1room
	 * rej:  s:0 1:me 5:who 57:room 13:3
	 * rejr: s:4 5:who 10:99 19:-1617114599
	 */
}

static void yahoo_process_ping(struct yahoo_input_data *yid,
                               struct yahoo_packet *pkt)
{
	char *errormsg = NULL;

	YList *l;

	for (l = pkt->hash; l; l = l->next) {
		struct yahoo_pair *pair = l->data;
		if (pair->key == 16) {
			errormsg = pair->value;
		}
	}

	NOTICE(("got ping packet"));
	YAHOO_CALLBACK (ext_yahoo_got_ping) (yid->yd->client_id, errormsg);
}

static void yahoo_process_buddy_change_group(struct yahoo_input_data *yid,
                                             struct yahoo_packet *pkt)
{
	YList *l;
	char *me = NULL;
	char *who = NULL;
	char *old_group = NULL;
	char *new_group = NULL;

	for (l = pkt->hash; l; l = l->next) {
		struct yahoo_pair *pair = l->data;
		if (pair->key == 1) {
			me = pair->value;
		}
		if (pair->key == 7) {
			who = pair->value;
		}
		if (pair->key == 224) {
			old_group = pair->value;
		}
		if (pair->key == 264) {
			new_group = pair->value;
		}
	}

	YAHOO_CALLBACK (ext_yahoo_got_buddy_change_group) (yid->yd->client_id,
	                                                   me, who, old_group, new_group);
}

static void _yahoo_webcam_get_server_connected(void *fd, int error, void *d)
{
	struct yahoo_input_data *yid = d;
	char *who = yid->wcm->user;
	char *data = NULL;
	char *packet = NULL;
	unsigned char magic_nr[] = { 0, 1, 0 };
	unsigned char header_len = 8;
	unsigned int len = 0;
	unsigned int pos = 0;

	if (error || !fd) {
		FREE(who);
		FREE(yid);
		return;
	}

	yid->fd = fd;
	inputs = y_list_prepend(inputs, yid);

	/* send initial packet */
	if (who) {
		data = strdup("<RVWCFG>");
	} else {
		data = strdup("<RUPCFG>");
	}
	yahoo_add_to_send_queue(yid, data, strlen(data));
	FREE(data);

	/* send data */
	if (who) {
		data = strdup("g=");
		data = y_string_append(data, who);
		data = y_string_append(data, "\r\n");
	} else {
		data = strdup("f=1\r\n");
	}
	len = strlen(data);
	packet = y_new0(char, header_len + len);
	packet[pos++] = header_len;
	memcpy(packet + pos, magic_nr, sizeof(magic_nr));
	pos += sizeof(magic_nr);
	pos += yahoo_put32(packet + pos, len);
	memcpy(packet + pos, data, len);
	pos += len;
	yahoo_add_to_send_queue(yid, packet, pos);
	FREE(packet);
	FREE(data);

	yid->read_tag =
	        YAHOO_CALLBACK (ext_yahoo_add_handler) (yid->yd->client_id, fd,
	                                                YAHOO_INPUT_READ, yid);
}

static void yahoo_webcam_get_server(struct yahoo_input_data *y, char *who,
                                    char *key)
{
	struct yahoo_input_data *yid = y_new0(struct yahoo_input_data, 1);
	struct yahoo_server_settings *yss = y->yd->server_settings;

	yid->type = YAHOO_CONNECTION_WEBCAM_MASTER;
	yid->yd = y->yd;
	yid->wcm = y_new0(struct yahoo_webcam, 1);
	yid->wcm->user = who ? strdup(who) : NULL;
	yid->wcm->direction = who ? YAHOO_WEBCAM_DOWNLOAD : YAHOO_WEBCAM_UPLOAD;
	yid->wcm->key = strdup(key);

	YAHOO_CALLBACK (ext_yahoo_connect_async) (yid->yd->client_id,
	                                          yss->webcam_host, yss->webcam_port,
	                                          _yahoo_webcam_get_server_connected, yid, 0);

}

static YList *webcam_queue = NULL;
static void yahoo_process_webcam_key(struct yahoo_input_data *yid,
                                     struct yahoo_packet *pkt)
{
	char *key = NULL;
	char *who = NULL;

	YList *l;

	yahoo_dump_unhandled(pkt);
	for (l = pkt->hash; l; l = l->next) {
		struct yahoo_pair *pair = l->data;
		if (pair->key == 5) {
			; /* me */
		}
		if (pair->key == 61) {
			key = pair->value;
		}
	}

	l = webcam_queue;
	if (!l) {
		return;
	}
	who = l->data;
	webcam_queue = y_list_remove_link(webcam_queue, webcam_queue);
	y_list_free_1(l);
	yahoo_webcam_get_server(yid, who, key);
	FREE(who);
}

static void yahoo_packet_process(struct yahoo_input_data *yid,
                                 struct yahoo_packet *pkt)
{
	DEBUG_MSG(("yahoo_packet_process: 0x%02x", pkt->service));
	switch (pkt->service) {
	case YAHOO_SERVICE_USERSTAT:
	case YAHOO_SERVICE_LOGON:
	case YAHOO_SERVICE_LOGOFF:
	case YAHOO_SERVICE_ISAWAY:
	case YAHOO_SERVICE_ISBACK:
	case YAHOO_SERVICE_GAMELOGON:
	case YAHOO_SERVICE_GAMELOGOFF:
	case YAHOO_SERVICE_IDACT:
	case YAHOO_SERVICE_IDDEACT:
	case YAHOO_SERVICE_Y6_STATUS_UPDATE:
	case YAHOO_SERVICE_Y8_STATUS:
		yahoo_process_status(yid, pkt);
		break;
	case YAHOO_SERVICE_NOTIFY:
		yahoo_process_notify(yid, pkt);
		break;
	case YAHOO_SERVICE_MESSAGE:
	case YAHOO_SERVICE_GAMEMSG:
	case YAHOO_SERVICE_SYSMESSAGE:
		yahoo_process_message(yid, pkt);
		break;
	case YAHOO_SERVICE_NEWMAIL:
		yahoo_process_mail(yid, pkt);
		break;
	case YAHOO_SERVICE_Y7_AUTHORIZATION:
		yahoo_process_new_contact(yid, pkt);
		break;
	case YAHOO_SERVICE_NEWCONTACT:
		yahoo_process_contact(yid, pkt);
		break;
	case YAHOO_SERVICE_LIST:
		yahoo_process_list(yid, pkt);
		break;
	case YAHOO_SERVICE_VERIFY:
		yahoo_process_verify(yid, pkt);
		break;
	case YAHOO_SERVICE_AUTH:
		yahoo_process_auth(yid, pkt);
		break;
	case YAHOO_SERVICE_AUTHRESP:
		yahoo_process_auth_resp(yid, pkt);
		break;
	case YAHOO_SERVICE_CONFINVITE:
	case YAHOO_SERVICE_CONFADDINVITE:
	case YAHOO_SERVICE_CONFDECLINE:
	case YAHOO_SERVICE_CONFLOGON:
	case YAHOO_SERVICE_CONFLOGOFF:
	case YAHOO_SERVICE_CONFMSG:
		yahoo_process_conference(yid, pkt);
		break;
	case YAHOO_SERVICE_CHATONLINE:
	case YAHOO_SERVICE_CHATGOTO:
	case YAHOO_SERVICE_CHATJOIN:
	case YAHOO_SERVICE_CHATLEAVE:
	case YAHOO_SERVICE_CHATEXIT:
	case YAHOO_SERVICE_CHATLOGOUT:
	case YAHOO_SERVICE_CHATPING:
	case YAHOO_SERVICE_COMMENT:
		yahoo_process_chat(yid, pkt);
		break;
	case YAHOO_SERVICE_P2PFILEXFER:
	case YAHOO_SERVICE_Y7_FILETRANSFER:
		yahoo_process_filetransfer(yid, pkt);
		break;
	case YAHOO_SERVICE_Y7_FILETRANSFERINFO:
		yahoo_process_filetransferinfo(yid, pkt);
		break;
	case YAHOO_SERVICE_Y7_FILETRANSFERACCEPT:
		yahoo_process_filetransferaccept(yid, pkt);
		break;
	case YAHOO_SERVICE_ADDBUDDY:
		yahoo_process_buddyadd(yid, pkt);
		break;
	case YAHOO_SERVICE_REMBUDDY:
		yahoo_process_buddydel(yid, pkt);
		break;
	case YAHOO_SERVICE_IGNORECONTACT:
		yahoo_process_ignore(yid, pkt);
		break;
	case YAHOO_SERVICE_VOICECHAT:
		yahoo_process_voicechat(yid, pkt);
		break;
	case YAHOO_SERVICE_WEBCAM:
		yahoo_process_webcam_key(yid, pkt);
		break;
	case YAHOO_SERVICE_PING:
		yahoo_process_ping(yid, pkt);
		break;
	case YAHOO_SERVICE_Y7_CHANGE_GROUP:
		yahoo_process_buddy_change_group(yid, pkt);
		break;
	case YAHOO_SERVICE_IDLE:
	case YAHOO_SERVICE_MAILSTAT:
	case YAHOO_SERVICE_CHATINVITE:
	case YAHOO_SERVICE_CALENDAR:
	case YAHOO_SERVICE_NEWPERSONALMAIL:
	case YAHOO_SERVICE_ADDIDENT:
	case YAHOO_SERVICE_ADDIGNORE:
	case YAHOO_SERVICE_GOTGROUPRENAME:
	case YAHOO_SERVICE_GROUPRENAME:
	case YAHOO_SERVICE_PASSTHROUGH2:
	case YAHOO_SERVICE_CHATLOGON:
	case YAHOO_SERVICE_CHATLOGOFF:
	case YAHOO_SERVICE_CHATMSG:
	case YAHOO_SERVICE_REJECTCONTACT:
	case YAHOO_SERVICE_PEERTOPEER:
		WARNING(("unhandled service 0x%02x", pkt->service));
		yahoo_dump_unhandled(pkt);
		break;
	case YAHOO_SERVICE_PICTURE:
		yahoo_process_picture(yid, pkt);
		break;
	case YAHOO_SERVICE_PICTURE_CHECKSUM:
		yahoo_process_picture_checksum(yid, pkt);
		break;
	case YAHOO_SERVICE_PICTURE_UPLOAD:
		yahoo_process_picture_upload(yid, pkt);
		break;
	case YAHOO_SERVICE_Y8_LIST:     /* Buddy List */
		yahoo_process_buddy_list(yid, pkt);
		break;
	default:
		WARNING(("unknown service 0x%02x", pkt->service));
		yahoo_dump_unhandled(pkt);
		break;
	}
}

static struct yahoo_packet *yahoo_getdata(struct yahoo_input_data *yid)
{
	struct yahoo_packet *pkt;
	struct yahoo_data *yd = yid->yd;
	int pos = 0;
	int pktlen;

	if (!yd) {
		return NULL;
	}

	DEBUG_MSG(("rxlen is %d", yid->rxlen));
	if (yid->rxlen < YAHOO_PACKET_HDRLEN) {
		DEBUG_MSG(("len < YAHOO_PACKET_HDRLEN"));
		return NULL;
	}

	pos += 4;               /* YMSG */
	pos += 2;
	pos += 2;

	pktlen = yahoo_get16(yid->rxqueue + pos);
	pos += 2;
	DEBUG_MSG(("%d bytes to read, rxlen is %d", pktlen, yid->rxlen));

	if (yid->rxlen < (YAHOO_PACKET_HDRLEN + pktlen)) {
		DEBUG_MSG(("len < YAHOO_PACKET_HDRLEN + pktlen"));
		return NULL;
	}

	LOG(("reading packet"));
	yahoo_packet_dump(yid->rxqueue, YAHOO_PACKET_HDRLEN + pktlen);

	pkt = yahoo_packet_new(0, 0, 0);

	pkt->service = yahoo_get16(yid->rxqueue + pos);
	pos += 2;
	pkt->status = yahoo_get32(yid->rxqueue + pos);
	pos += 4;
	DEBUG_MSG(("Yahoo Service: 0x%02x Status: %d", pkt->service,
	           pkt->status));
	pkt->id = yahoo_get32(yid->rxqueue + pos);
	pos += 4;

	yd->session_id = pkt->id;

	yahoo_packet_read(pkt, yid->rxqueue + pos, pktlen);

	yid->rxlen -= YAHOO_PACKET_HDRLEN + pktlen;
	DEBUG_MSG(("rxlen == %d, rxqueue == %p", yid->rxlen, yid->rxqueue));
	if (yid->rxlen > 0) {
		unsigned char *tmp = y_memdup(yid->rxqueue + YAHOO_PACKET_HDRLEN
		                              + pktlen, yid->rxlen);
		FREE(yid->rxqueue);
		yid->rxqueue = tmp;
		DEBUG_MSG(("new rxlen == %d, rxqueue == %p", yid->rxlen,
		           yid->rxqueue));
	} else {
		DEBUG_MSG(("freed rxqueue == %p", yid->rxqueue));
		FREE(yid->rxqueue);
	}

	return pkt;
}

#if 0
static struct yab *yahoo_yab_read(unsigned char *d, int len)
{
	char *st, *en;
	char *data = (char *) d;
	struct yab *yab = NULL;

	data[len] = '\0';

	DEBUG_MSG(("Got yab: %s", data));
	st = en = strstr(data, "e0=\"");
	if (st) {
		yab = y_new0(struct yab, 1);

		st += strlen("e0=\"");
		en = strchr(st, '"');
		*en++ = '\0';
		yab->email = yahoo_xmldecode(st);
	}

	if (!en) {
		return NULL;
	}

	st = strstr(en, "id=\"");
	if (st) {
		st += strlen("id=\"");
		en = strchr(st, '"');
		*en++ = '\0';
		yab->yid = atoi(yahoo_xmldecode(st));
	}

	st = strstr(en, "fn=\"");
	if (st) {
		st += strlen("fn=\"");
		en = strchr(st, '"');
		*en++ = '\0';
		yab->fname = yahoo_xmldecode(st);
	}

	st = strstr(en, "ln=\"");
	if (st) {
		st += strlen("ln=\"");
		en = strchr(st, '"');
		*en++ = '\0';
		yab->lname = yahoo_xmldecode(st);
	}

	st = strstr(en, "nn=\"");
	if (st) {
		st += strlen("nn=\"");
		en = strchr(st, '"');
		*en++ = '\0';
		yab->nname = yahoo_xmldecode(st);
	}

	st = strstr(en, "yi=\"");
	if (st) {
		st += strlen("yi=\"");
		en = strchr(st, '"');
		*en++ = '\0';
		yab->id = yahoo_xmldecode(st);
	}

	st = strstr(en, "hphone=\"");
	if (st) {
		st += strlen("hphone=\"");
		en = strchr(st, '"');
		*en++ = '\0';
		yab->hphone = yahoo_xmldecode(st);
	}

	st = strstr(en, "wphone=\"");
	if (st) {
		st += strlen("wphone=\"");
		en = strchr(st, '"');
		*en++ = '\0';
		yab->wphone = yahoo_xmldecode(st);
	}

	st = strstr(en, "mphone=\"");
	if (st) {
		st += strlen("mphone=\"");
		en = strchr(st, '"');
		*en++ = '\0';
		yab->mphone = yahoo_xmldecode(st);
	}

	st = strstr(en, "dbid=\"");
	if (st) {
		st += strlen("dbid=\"");
		en = strchr(st, '"');
		*en++ = '\0';
		yab->dbid = atoi(st);
	}

	return yab;
}

static struct yab *yahoo_getyab(struct yahoo_input_data *yid)
{
	struct yab *yab = NULL;
	int pos = 0, end = 0;
	struct yahoo_data *yd = yid->yd;

	if (!yd) {
		return NULL;
	}

	do {
		DEBUG_MSG(("rxlen is %d", yid->rxlen));

		if (yid->rxlen <= strlen("<ct")) {
			return NULL;
		}

		/* start with <ct */
		while (pos < yid->rxlen - strlen("<ct") + 1
		       && memcmp(yid->rxqueue + pos, "<ct", strlen("<ct"))) {
			pos++;
		}

		if (pos >= yid->rxlen - 1) {
			return NULL;
		}

		end = pos + 2;
		/* end with > */
		while (end < yid->rxlen - strlen(">")
		       && memcmp(yid->rxqueue + end, ">", strlen(">"))) {
			end++;
		}

		if (end >= yid->rxlen - 1) {
			return NULL;
		}

		yab = yahoo_yab_read(yid->rxqueue + pos, end + 2 - pos);

		yid->rxlen -= end + 1;
		DEBUG_MSG(("rxlen == %d, rxqueue == %p", yid->rxlen,
		           yid->rxqueue));
		if (yid->rxlen > 0) {
			unsigned char *tmp =
			        y_memdup(yid->rxqueue + end + 1, yid->rxlen);
			FREE(yid->rxqueue);
			yid->rxqueue = tmp;
			DEBUG_MSG(("new rxlen == %d, rxqueue == %p", yid->rxlen,
			           yid->rxqueue));
		} else {
			DEBUG_MSG(("freed rxqueue == %p", yid->rxqueue));
			FREE(yid->rxqueue);
		}

	} while (!yab && end < yid->rxlen - 1);

	return yab;
}
#endif

static char *yahoo_getwebcam_master(struct yahoo_input_data *yid)
{
	unsigned int pos = 0;
	unsigned int len = 0;
	unsigned int status = 0;
	char *server = NULL;
	struct yahoo_data *yd = yid->yd;

	if (!yid || !yd) {
		return NULL;
	}

	DEBUG_MSG(("rxlen is %d", yid->rxlen));

	len = yid->rxqueue[pos++];
	if (yid->rxlen < len) {
		return NULL;
	}

	/* extract status (0 = ok, 6 = webcam not online) */
	status = yid->rxqueue[pos++];

	if (status == 0) {
		pos += 2;       /* skip next 2 bytes */
		server = y_memdup(yid->rxqueue + pos, 16);
		pos += 16;
	} else if (status == 6) {
		YAHOO_CALLBACK (ext_yahoo_webcam_closed)
		        (yd->client_id, yid->wcm->user, 4);
	}

	/* skip rest of the data */

	yid->rxlen -= len;
	DEBUG_MSG(("rxlen == %d, rxqueue == %p", yid->rxlen, yid->rxqueue));
	if (yid->rxlen > 0) {
		unsigned char *tmp = y_memdup(yid->rxqueue + pos, yid->rxlen);
		FREE(yid->rxqueue);
		yid->rxqueue = tmp;
		DEBUG_MSG(("new rxlen == %d, rxqueue == %p", yid->rxlen,
		           yid->rxqueue));
	} else {
		DEBUG_MSG(("freed rxqueue == %p", yid->rxqueue));
		FREE(yid->rxqueue);
	}

	return server;
}

static int yahoo_get_webcam_data(struct yahoo_input_data *yid)
{
	unsigned char reason = 0;
	unsigned int pos = 0;
	unsigned int begin = 0;
	unsigned int end = 0;
	unsigned int closed = 0;
	unsigned char header_len = 0;
	char *who;
	int connect = 0;
	struct yahoo_data *yd = yid->yd;

	if (!yd) {
		return -1;
	}

	if (!yid->wcm || !yid->wcd || !yid->rxlen) {
		return -1;
	}

	DEBUG_MSG(("rxlen is %d", yid->rxlen));

	/* if we are not reading part of image then read header */
	if (!yid->wcd->to_read) {
		header_len = yid->rxqueue[pos++];
		yid->wcd->packet_type = 0;

		if (yid->rxlen < header_len) {
			return 0;
		}

		if (header_len >= 8) {
			reason = yid->rxqueue[pos++];
			/* next 2 bytes should always be 05 00 */
			pos += 2;
			yid->wcd->data_size = yahoo_get32(yid->rxqueue + pos);
			pos += 4;
			yid->wcd->to_read = yid->wcd->data_size;
		}
		if (header_len >= 13) {
			yid->wcd->packet_type = yid->rxqueue[pos++];
			yid->wcd->timestamp = yahoo_get32(yid->rxqueue + pos);
			pos += 4;
		}

		/* skip rest of header */
		pos = header_len;
	}

	begin = pos;
	pos += yid->wcd->to_read;
	if (pos > yid->rxlen) {
		pos = yid->rxlen;
	}

	/* if it is not an image then make sure we have the whole packet */
	if (yid->wcd->packet_type != 0x02) {
		if ((pos - begin) != yid->wcd->data_size) {
			yid->wcd->to_read = 0;
			return 0;
		} else {
			yahoo_packet_dump(yid->rxqueue + begin, pos - begin);
		}
	}

	DEBUG_MSG(("packet type %.2X, data length %d", yid->wcd->packet_type,
	           yid->wcd->data_size));

	/* find out what kind of packet we got */
	switch (yid->wcd->packet_type) {
	case 0x00:
		/* user requests to view webcam (uploading) */
		if (yid->wcd->data_size &&
		    yid->wcm->direction == YAHOO_WEBCAM_UPLOAD) {
			end = begin;
			while (end <= yid->rxlen && yid->rxqueue[end++] != 13) {
				;
			}
			if (end > begin) {
				who = y_memdup(yid->rxqueue + begin,
				               end - begin);
				who[end - begin - 1] = 0;
				YAHOO_CALLBACK (ext_yahoo_webcam_viewer) (yd->
				                                          client_id, who + 2, 2);
				FREE(who);
			}
		}

		if (yid->wcm->direction == YAHOO_WEBCAM_DOWNLOAD) {
			/* timestamp/status field */
			/* 0 = declined viewing permission */
			/* 1 = accepted viewing permission */
			if (yid->wcd->timestamp == 0) {
				YAHOO_CALLBACK (ext_yahoo_webcam_closed) (yd->
				                                          client_id, yid->wcm->user, 3);
			}
		}
		break;
	case 0x01:              /* status packets?? */
		/* timestamp contains status info */
		/* 00 00 00 01 = we have data?? */
		break;
	case 0x02:              /* image data */
		YAHOO_CALLBACK (ext_yahoo_got_webcam_image) (yd->client_id,
		                                             yid->wcm->user, yid->rxqueue + begin,
		                                             yid->wcd->data_size, pos - begin, yid->wcd->timestamp);
		break;
	case 0x05:              /* response packets when uploading */
		if (!yid->wcd->data_size) {
			YAHOO_CALLBACK (ext_yahoo_webcam_data_request) (yd->
			                                                client_id, yid->wcd->timestamp);
		}
		break;
	case 0x07:              /* connection is closing */
		switch (reason) {
		case 0x01:      /* user closed connection */
			closed = 1;
			break;
		case 0x0F:      /* user cancelled permission */
			closed = 2;
			break;
		}
		YAHOO_CALLBACK (ext_yahoo_webcam_closed) (yd->client_id,
		                                          yid->wcm->user, closed);
		break;
	case 0x0C:              /* user connected */
	case 0x0D:              /* user disconnected */
		if (yid->wcd->data_size) {
			who = y_memdup(yid->rxqueue + begin, pos - begin + 1);
			who[pos - begin] = 0;
			if (yid->wcd->packet_type == 0x0C) {
				connect = 1;
			} else {
				connect = 0;
			}
			YAHOO_CALLBACK (ext_yahoo_webcam_viewer) (yd->client_id,
			                                          who, connect);
			FREE(who);
		}
		break;
	case 0x13:              /* user data */
		/* i=user_ip (ip of the user we are viewing) */
		/* j=user_ext_ip (external ip of the user we */
		/*                are viewing) */
		break;
	case 0x17:              /* ?? */
		break;
	}
	yid->wcd->to_read -= pos - begin;

	yid->rxlen -= pos;
	DEBUG_MSG(("rxlen == %d, rxqueue == %p", yid->rxlen, yid->rxqueue));
	if (yid->rxlen > 0) {
		unsigned char *tmp = y_memdup(yid->rxqueue + pos, yid->rxlen);
		FREE(yid->rxqueue);
		yid->rxqueue = tmp;
		DEBUG_MSG(("new rxlen == %d, rxqueue == %p", yid->rxlen,
		           yid->rxqueue));
	} else {
		DEBUG_MSG(("freed rxqueue == %p", yid->rxqueue));
		FREE(yid->rxqueue);
	}

	/* If we read a complete packet return success */
	if (!yid->wcd->to_read) {
		return 1;
	}

	return 0;
}

int yahoo_write_ready(int id, void *fd, void *data)
{
	struct yahoo_input_data *yid = data;
	int len;
	struct data_queue *tx;

	LOG(("write callback: id=%d fd=%p data=%p", id, fd, data));
	if (!yid || !yid->txqueues) {
		return -2;
	}

	tx = yid->txqueues->data;
	LOG(("writing %d bytes", tx->len));
	len = yahoo_send_data(fd, tx->queue, MIN(1024, tx->len));

	if (len == -1 && errno == EAGAIN) {
		return 1;
	}

	if (len <= 0) {
		int e = errno;
		DEBUG_MSG(("len == %d (<= 0)", len));
		while (yid->txqueues) {
			YList *l = yid->txqueues;
			tx = l->data;
			free(tx->queue);
			free(tx);
			yid->txqueues =
			        y_list_remove_link(yid->txqueues,
			                           yid->txqueues);
			y_list_free_1(l);
		}
		LOG(("yahoo_write_ready(%d, %p) len < 0", id, fd));
		YAHOO_CALLBACK (ext_yahoo_remove_handler) (id, yid->write_tag);
		yid->write_tag = 0;
		errno = e;
		return 0;
	}


	tx->len -= len;
	if (tx->len > 0) {
		unsigned char *tmp = y_memdup(tx->queue + len, tx->len);
		FREE(tx->queue);
		tx->queue = tmp;
	} else {
		YList *l = yid->txqueues;
		free(tx->queue);
		free(tx);
		yid->txqueues =
		        y_list_remove_link(yid->txqueues, yid->txqueues);
		y_list_free_1(l);
		/*
		   if(!yid->txqueues)
		   LOG(("yahoo_write_ready(%d, %d) !yxqueues", id, fd));
		 */
		if (!yid->txqueues) {
			LOG(("yahoo_write_ready(%d, %p) !txqueues", id, fd));
			YAHOO_CALLBACK (ext_yahoo_remove_handler) (id,
			                                           yid->write_tag);
			yid->write_tag = 0;
		}
	}

	return 1;
}

static void yahoo_process_pager_connection(struct yahoo_input_data *yid,
                                           int over)
{
	struct yahoo_packet *pkt;
	struct yahoo_data *yd = yid->yd;
	int id = yd->client_id;

	if (over) {
		return;
	}

	while (find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER)
	       && (pkt = yahoo_getdata(yid)) != NULL) {

		yahoo_packet_process(yid, pkt);

		yahoo_packet_free(pkt);
	}
}

static void yahoo_process_chatcat_connection(struct yahoo_input_data *yid,
                                             int over)
{
	if (over) {
		return;
	}

	if (strstr((char *) yid->rxqueue + (yid->rxlen - 20), "</content>")) {
		YAHOO_CALLBACK (ext_yahoo_chat_cat_xml) (yid->yd->client_id,
		                                         (char *) yid->rxqueue);
	}
}

#if 0
static void yahoo_process_yab_connection(struct yahoo_input_data *yid, int over)
{
	struct yahoo_data *yd = yid->yd;
	struct yab *yab;
	YList *buds;
	int changed = 0;
	int id = yd->client_id;
	int yab_used = 0;

	LOG(("Got data for YAB"));

	if (over) {
		return;
	}

	while (find_input_by_id_and_type(id, YAHOO_CONNECTION_YAB)
	       && (yab = yahoo_getyab(yid)) != NULL) {
		if (!yab->id) {
			continue;
		}

		changed = 1;
		yab_used = 0;
		for (buds = yd->buddies; buds; buds = buds->next) {
			struct yahoo_buddy *bud = buds->data;
			if (!strcmp(bud->id, yab->id)) {
				yab_used = 1;
				bud->yab_entry = yab;
				if (yab->nname) {
					bud->real_name = strdup(yab->nname);
				} else if (yab->fname && yab->lname) {
					bud->real_name = y_new0(char,
					                        strlen(yab->fname) +
					                        strlen(yab->lname) + 2);
					sprintf(bud->real_name, "%s %s",
					        yab->fname, yab->lname);
				} else if (yab->fname) {
					bud->real_name = strdup(yab->fname);
				}
				break;  /* for */
			}
		}

		if (!yab_used) {
			FREE(yab->fname);
			FREE(yab->lname);
			FREE(yab->nname);
			FREE(yab->id);
			FREE(yab->email);
			FREE(yab->hphone);
			FREE(yab->wphone);
			FREE(yab->mphone);
			FREE(yab);
		}

	}

	if (changed) {
		YAHOO_CALLBACK (ext_yahoo_got_buddies) (yd->client_id,
		                                        yd->buddies);
	}
}
#endif

static void yahoo_process_search_connection(struct yahoo_input_data *yid,
                                            int over)
{
	struct yahoo_found_contact *yct = NULL;
	char *p = (char *) yid->rxqueue, *np, *cp;
	int k, n;
	int start = 0, found = 0, total = 0;
	YList *contacts = NULL;
	struct yahoo_input_data *pyid =
	        find_input_by_id_and_type(yid->yd->client_id,
	                                  YAHOO_CONNECTION_PAGER);

	if (!over || !pyid) {
		return;
	}

	if (p && (p = strstr(p, "\r\n\r\n"))) {
		p += 4;

		for (k = 0; (p = strchr(p, 4)) && (k < 4); k++) {
			p++;
			n = atoi(p);
			switch (k) {
			case 0:
				found = pyid->ys->lsearch_nfound = n;
				break;
			case 2:
				start = pyid->ys->lsearch_nstart = n;
				break;
			case 3:
				total = pyid->ys->lsearch_ntotal = n;
				break;
			}
		}

		if (p) {
			p++;
		}

		k = 0;
		while (p && *p) {
			cp = p;
			np = strchr(p, 4);

			if (!np) {
				break;
			}
			*np = 0;
			p = np + 1;

			switch (k++) {
			case 1:
				if (strlen(cp) > 2
				    && y_list_length(contacts) < total) {
					yct = y_new0(struct yahoo_found_contact,
					             1);
					contacts = y_list_append(contacts, yct);
					yct->id = cp + 2;
				} else {
					*p = 0;
				}
				break;
			case 2:
				yct->online = !strcmp(cp, "2") ? 1 : 0;
				break;
			case 3:
				yct->gender = cp;
				break;
			case 4:
				yct->age = atoi(cp);
				break;
			case 5:
				/* not worth the context switch for strcmp */
				if (cp[0] != '\005' || cp[1] != '\000') {
					yct->location = cp;
				}
				k = 0;
				break;
			}
		}
	}

	YAHOO_CALLBACK (ext_yahoo_got_search_result) (yid->yd->client_id, found,
	                                              start, total, contacts);

	while (contacts) {
		YList *node = contacts;
		contacts = y_list_remove_link(contacts, node);
		free(node->data);
		y_list_free_1(node);
	}
}

static void _yahoo_webcam_connected(void *fd, int error, void *d)
{
	struct yahoo_input_data *yid = d;
	struct yahoo_webcam *wcm = yid->wcm;
	struct yahoo_data *yd = yid->yd;
	char conn_type[100];
	char *data = NULL;
	char *packet = NULL;
	unsigned char magic_nr[] = { 1, 0, 0, 0, 1 };
	unsigned header_len = 0;
	unsigned int len = 0;
	unsigned int pos = 0;

	if (error || !fd) {
		FREE(yid);
		return;
	}

	yid->fd = fd;
	inputs = y_list_prepend(inputs, yid);

	LOG(("Connected"));
	/* send initial packet */
	switch (wcm->direction) {
	case YAHOO_WEBCAM_DOWNLOAD:
		data = strdup("<REQIMG>");
		break;
	case YAHOO_WEBCAM_UPLOAD:
		data = strdup("<SNDIMG>");
		break;
	default:
		return;
	}
	yahoo_add_to_send_queue(yid, data, strlen(data));
	FREE(data);

	/* send data */
	switch (wcm->direction) {
	case YAHOO_WEBCAM_DOWNLOAD:
		header_len = 8;
		data = strdup("a=2\r\nc=us\r\ne=21\r\nu=");
		data = y_string_append(data, yd->user);
		data = y_string_append(data, "\r\nt=");
		data = y_string_append(data, wcm->key);
		data = y_string_append(data, "\r\ni=");
		data = y_string_append(data, wcm->my_ip);
		data = y_string_append(data, "\r\ng=");
		data = y_string_append(data, wcm->user);
		data = y_string_append(data, "\r\no=w-2-5-1\r\np=");
		snprintf(conn_type, sizeof(conn_type), "%d", wcm->conn_type);
		data = y_string_append(data, conn_type);
		data = y_string_append(data, "\r\n");
		break;
	case YAHOO_WEBCAM_UPLOAD:
		header_len = 13;
		data = strdup("a=2\r\nc=us\r\nu=");
		data = y_string_append(data, yd->user);
		data = y_string_append(data, "\r\nt=");
		data = y_string_append(data, wcm->key);
		data = y_string_append(data, "\r\ni=");
		data = y_string_append(data, wcm->my_ip);
		data = y_string_append(data, "\r\no=w-2-5-1\r\np=");
		snprintf(conn_type, sizeof(conn_type), "%d", wcm->conn_type);
		data = y_string_append(data, conn_type);
		data = y_string_append(data, "\r\nb=");
		data = y_string_append(data, wcm->description);
		data = y_string_append(data, "\r\n");
		break;
	}

	len = strlen(data);
	packet = y_new0(char, header_len + len);
	packet[pos++] = header_len;
	packet[pos++] = 0;
	switch (wcm->direction) {
	case YAHOO_WEBCAM_DOWNLOAD:
		packet[pos++] = 1;
		packet[pos++] = 0;
		break;
	case YAHOO_WEBCAM_UPLOAD:
		packet[pos++] = 5;
		packet[pos++] = 0;
		break;
	}

	pos += yahoo_put32(packet + pos, len);
	if (wcm->direction == YAHOO_WEBCAM_UPLOAD) {
		memcpy(packet + pos, magic_nr, sizeof(magic_nr));
		pos += sizeof(magic_nr);
	}
	memcpy(packet + pos, data, len);
	yahoo_add_to_send_queue(yid, packet, header_len + len);
	FREE(packet);
	FREE(data);

	yid->read_tag =
	        YAHOO_CALLBACK (ext_yahoo_add_handler) (yid->yd->client_id,
	                                                yid->fd, YAHOO_INPUT_READ, yid);
}

static void yahoo_webcam_connect(struct yahoo_input_data *y)
{
	struct yahoo_webcam *wcm = y->wcm;
	struct yahoo_input_data *yid;

	if (!wcm || !wcm->server || !wcm->key) {
		return;
	}

	yid = y_new0(struct yahoo_input_data, 1);
	yid->type = YAHOO_CONNECTION_WEBCAM;
	yid->yd = y->yd;

	/* copy webcam data to new connection */
	yid->wcm = y->wcm;
	y->wcm = NULL;

	yid->wcd = y_new0(struct yahoo_webcam_data, 1);

	LOG(("Connecting to: %s:%d", wcm->server, wcm->port));
	YAHOO_CALLBACK (ext_yahoo_connect_async) (y->yd->client_id, wcm->server,
	                                          wcm->port, _yahoo_webcam_connected, yid, 0);

}

static void yahoo_process_webcam_master_connection(struct yahoo_input_data *yid,
                                                   int over)
{
	char *server;
	struct yahoo_server_settings *yss;

	if (over) {
		return;
	}

	server = yahoo_getwebcam_master(yid);

	if (server) {
		yss = yid->yd->server_settings;
		yid->wcm->server = strdup(server);
		yid->wcm->port = yss->webcam_port;
		yid->wcm->conn_type = yss->conn_type;
		yid->wcm->my_ip = strdup(yss->local_host);
		if (yid->wcm->direction == YAHOO_WEBCAM_UPLOAD) {
			yid->wcm->description = strdup(yss->webcam_description);
		}
		yahoo_webcam_connect(yid);
		FREE(server);
	}
}

static void yahoo_process_webcam_connection(struct yahoo_input_data *yid,
                                            int over)
{
	int id = yid->yd->client_id;
	void *fd = yid->fd;

	if (over) {
		return;
	}

	/* as long as we still have packets available keep processing them */
	while (find_input_by_id_and_fd(id, fd)
	       && yahoo_get_webcam_data(yid) == 1) {
		;
	}
}

static void(*yahoo_process_connection[]) (struct yahoo_input_data *,
                                          int over) = {
	yahoo_process_pager_connection, yahoo_process_ft_connection,
	NULL,         /*yahoo_process_yab_connection, */
	yahoo_process_webcam_master_connection,
	yahoo_process_webcam_connection,
	yahoo_process_chatcat_connection,
	yahoo_process_search_connection
};

int yahoo_read_ready(int id, void *fd, void *data)
{
	struct yahoo_input_data *yid = data;
	char buf[1024];
	int len;

	LOG(("read callback: id=%d fd=%p data=%p", id, fd, data));
	if (!yid) {
		return -2;
	}

	do {
		len = YAHOO_CALLBACK (ext_yahoo_read) (fd, buf, sizeof(buf));
	} while (len == -1 && errno == EINTR);

	if (len == -1 && (errno == EAGAIN || errno == EINTR)) { /* we'll try again later */
		return 1;
	}

	if (len <= 0) {
		int e = errno;
		DEBUG_MSG(("len == %d (<= 0)", len));

		if (yid->type == YAHOO_CONNECTION_PAGER) {
			YAHOO_CALLBACK (ext_yahoo_login_response) (yid->yd->
			                                           client_id, YAHOO_LOGIN_SOCK, NULL);
		}

		yahoo_process_connection[yid->type] (yid, 1);
		yahoo_input_close(yid);

		/* no need to return an error, because we've already fixed it */
		if (len == 0) {
			return 1;
		}

		errno = e;
		LOG(("read error: %s", strerror(errno)));
		return -1;
	}

	yid->rxqueue =
	        y_renew(unsigned char, yid->rxqueue, len + yid->rxlen + 1);
	memcpy(yid->rxqueue + yid->rxlen, buf, len);
	yid->rxlen += len;
	yid->rxqueue[yid->rxlen] = 0;

	yahoo_process_connection[yid->type] (yid, 0);

	return len;
}

int yahoo_init_with_attributes(const char *username, const char *password, ...)
{
	va_list ap;
	struct yahoo_data *yd;

	yd = y_new0(struct yahoo_data, 1);

	if (!yd) {
		return 0;
	}

	yd->user = strdup(username);
	yd->password = strdup(password);

	yd->initial_status = -1;
	yd->current_status = -1;

	yd->client_id = ++last_id;

	add_to_list(yd);

	va_start(ap, password);
	yd->server_settings = _yahoo_assign_server_settings(ap);
	va_end(ap);

	return yd->client_id;
}

int yahoo_init(const char *username, const char *password)
{
	return yahoo_init_with_attributes(username, password, NULL);
}

static void yahoo_connected(void *fd, int error, void *data)
{
	struct connect_callback_data *ccd = data;
	struct yahoo_data *yd = ccd->yd;
	struct yahoo_packet *pkt;
	struct yahoo_input_data *yid;
	struct yahoo_server_settings *yss = yd->server_settings;

	if (error) {
		int tag;
		if (fallback_ports[ccd->i]) {
			char *host = yss->pager_host;

			if (!host) {
				host = yss->pager_host_list[ccd->server_i];
			}

			yss->pager_port = fallback_ports[ccd->i++];
			tag = YAHOO_CALLBACK (ext_yahoo_connect_async) (yd->
			                                                client_id, host, yss->pager_port,
			                                                yahoo_connected, ccd, 0);

			if (tag > 0) {
				ccd->tag = tag;
			}
		} else if (yss->pager_host_list
		           && yss->pager_host_list[ccd->server_i]) {

			/* Get back to the default port */
			yss->pager_port = pager_port;
			ccd->server_i++;
			LOG(("Fallback: Connecting to %s:%d", yss->pager_host_list[ccd->server_i], yss->pager_port));

			ccd->i = 0;
			tag = YAHOO_CALLBACK (ext_yahoo_connect_async) (yd->client_id,
			                                                yss->pager_host_list[ccd->server_i],
			                                                yss->pager_port,
			                                                yahoo_connected, ccd, 0);
		} else {
			FREE(ccd);
			YAHOO_CALLBACK (ext_yahoo_login_response) (yd->client_id,
			                                           YAHOO_LOGIN_SOCK, NULL);
		}
		return;
	}

	FREE(ccd);

	/* fd == NULL && error == 0 means connect was cancelled */
	if (!fd) {
		return;
	}

	pkt = yahoo_packet_new(YAHOO_SERVICE_AUTH, YPACKET_STATUS_DEFAULT,
	                       yd->session_id);
	NOTICE(("Sending initial packet"));

	yahoo_packet_hash(pkt, 1, yd->user);

	yid = find_input_by_id_and_type(yd->client_id, YAHOO_CONNECTION_PAGER);
	yid->fd = fd;

	yahoo_send_packet(yid, pkt, 0);

	yahoo_packet_free(pkt);

	yid->read_tag =
	        YAHOO_CALLBACK (ext_yahoo_add_handler) (yid->yd->client_id,
	                                                yid->fd, YAHOO_INPUT_READ, yid);
}

void *yahoo_get_fd(int id)
{
	struct yahoo_input_data *yid =
	        find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);

	if (!yid) {
		return 0;
	} else {
		return yid->fd;
	}
}

#if 0
void yahoo_send_buzz(int id, const char *from, const char *who)
{
	yahoo_send_im(id, from, who, "<ding>", 1, 0);
}
#endif

void yahoo_send_im(int id, const char *from, const char *who, const char *what,
                   int utf8, int picture)
{
	struct yahoo_input_data *yid =
	        find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_packet *pkt = NULL;
	struct yahoo_data *yd;
	char pic_str[10];

	if (!yid) {
		return;
	}

	yd = yid->yd;

	pkt = yahoo_packet_new(YAHOO_SERVICE_MESSAGE, (enum ypacket_status) YAHOO_STATUS_OFFLINE,
	                       yd->session_id);

	snprintf(pic_str, sizeof(pic_str), "%d", picture);

	if (from && strcmp(from, yd->user)) {
		yahoo_packet_hash(pkt, 0, yd->user);
	}
	yahoo_packet_hash(pkt, 1, from ? from : yd->user);
	yahoo_packet_hash(pkt, 5, who);
	yahoo_packet_hash(pkt, 14, what);

	if (utf8) {
		yahoo_packet_hash(pkt, 97, "1");
	}

	yahoo_packet_hash(pkt, 63, ";0");       /* imvironment name; or ;0 */
	yahoo_packet_hash(pkt, 64, "0");
	yahoo_packet_hash(pkt, 206, pic_str);

	yahoo_send_packet(yid, pkt, 0);

	yahoo_packet_free(pkt);
}

void yahoo_send_typing(int id, const char *from, const char *who, int typ)
{
	struct yahoo_input_data *yid =
	        find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt = NULL;

	if (!yid) {
		return;
	}

	yd = yid->yd;
	pkt = yahoo_packet_new(YAHOO_SERVICE_NOTIFY, YPACKET_STATUS_NOTIFY,
	                       yd->session_id);

	yahoo_packet_hash(pkt, 5, who);
	yahoo_packet_hash(pkt, 1, from ? from : yd->user);
	yahoo_packet_hash(pkt, 14, " ");
	yahoo_packet_hash(pkt, 13, typ ? "1" : "0");
	yahoo_packet_hash(pkt, 49, "TYPING");

	yahoo_send_packet(yid, pkt, 0);

	yahoo_packet_free(pkt);
}

void yahoo_set_away(int id, enum yahoo_status state, const char *msg, int away)
{
	struct yahoo_input_data *yid =
	        find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt = NULL;
	int old_status;
	char s[4];

	if (!yid) {
		return;
	}

	yd = yid->yd;

	old_status = yd->current_status;
	yd->current_status = state;

	/* Thank you libpurple :) */
	if (yd->current_status == YAHOO_STATUS_INVISIBLE) {
		pkt = yahoo_packet_new(YAHOO_SERVICE_Y6_VISIBLE_TOGGLE,
		                       (enum ypacket_status) YAHOO_STATUS_AVAILABLE, 0);
		yahoo_packet_hash(pkt, 13, "2");
		yahoo_send_packet(yid, pkt, 0);
		yahoo_packet_free(pkt);

		return;
	}

	pkt = yahoo_packet_new(YAHOO_SERVICE_Y6_STATUS_UPDATE,
	                       yd->current_status, yd->session_id);
	snprintf(s, sizeof(s), "%d", yd->current_status);
	yahoo_packet_hash(pkt, 10, s);
	yahoo_packet_hash(pkt, 19, msg && state == YAHOO_STATUS_CUSTOM ? msg : "");
	yahoo_packet_hash(pkt, 47, (away == 2) ? "2" : (away) ? "1" : "0");
	yahoo_send_packet(yid, pkt, 0);
	yahoo_packet_free(pkt);

	if (old_status == YAHOO_STATUS_INVISIBLE) {
		pkt = yahoo_packet_new(YAHOO_SERVICE_Y6_VISIBLE_TOGGLE,
		                       (enum ypacket_status) YAHOO_STATUS_AVAILABLE, 0);
		yahoo_packet_hash(pkt, 13, "1");
		yahoo_send_packet(yid, pkt, 0);
		yahoo_packet_free(pkt);
	}
}

void yahoo_logoff(int id)
{
	struct yahoo_input_data *yid =
	        find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt = NULL;

	if (!yid) {
		return;
	}
	yd = yid->yd;

	LOG(("yahoo_logoff: current status: %d", yd->current_status));

	if (yd->current_status != -1 && 0) {
		/* Meh. Don't send this. The event handlers are not going to
		   get to do this so it'll just leak memory. And the TCP
		   connection reset will hopefully be clear enough. */
		pkt = yahoo_packet_new(YAHOO_SERVICE_LOGOFF,
		                       YPACKET_STATUS_DEFAULT, yd->session_id);
		yd->current_status = -1;

		if (pkt) {
			yahoo_send_packet(yid, pkt, 0);
			yahoo_packet_free(pkt);
		}
	}

/*	do {
                yahoo_input_close(yid);
        } while((yid = find_input_by_id(id)));*/

}

#if 0
void yahoo_get_list(int id)
{
	struct yahoo_input_data *yid =
	        find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt = NULL;

	if (!yid) {
		return;
	}
	yd = yid->yd;

	pkt = yahoo_packet_new(YAHOO_SERVICE_LIST, YPACKET_STATUS_DEFAULT,
	                       yd->session_id);
	yahoo_packet_hash(pkt, 1, yd->user);
	if (pkt) {
		yahoo_send_packet(yid, pkt, 0);
		yahoo_packet_free(pkt);
	}
}
#endif

static void _yahoo_http_connected(int id, void *fd, int error, void *data)
{
	struct yahoo_input_data *yid = data;

	if (fd == NULL || error) {
		inputs = y_list_remove(inputs, yid);
		FREE(yid);
		return;
	}

	yid->fd = fd;
	yid->read_tag =
	        YAHOO_CALLBACK (ext_yahoo_add_handler) (yid->yd->client_id, fd,
	                                                YAHOO_INPUT_READ, yid);
}

#if 0
/* FIXME Get address book from address.yahoo.com instead */
void yahoo_get_yab(int id)
{
	struct yahoo_data *yd = find_conn_by_id(id);
	struct yahoo_input_data *yid;
	char url[1024];
	char buff[2048];

	if (!yd) {
		return;
	}

	yid = y_new0(struct yahoo_input_data, 1);
	yid->yd = yd;
	yid->type = YAHOO_CONNECTION_YAB;

	LOG(("Sending request for Address Book"));

	snprintf(url, 1024,
	         "http://address.yahoo.com/yab/us?v=XM&prog=ymsgr&.intl=us"
	         "&diffs=1&t=0&tags=short&rt=0&prog-ver=8.1.0.249&useutf8=1&legenc=codepage-1252");

	snprintf(buff, sizeof(buff), "Y=%s; T=%s", yd->cookie_y, yd->cookie_t);

	inputs = y_list_prepend(inputs, yid);

	yahoo_http_get(yid->yd->client_id, url, buff, 0, 0,
	               _yahoo_http_connected, yid);
}

struct yahoo_post_data {
	struct yahoo_input_data *yid;
	char *data;
};

static void _yahoo_http_post_connected(int id, void *fd, int error, void *data)
{
	struct yahoo_post_data *yad = data;
	struct yahoo_input_data *yid = yad->yid;
	char *buff = yad->data;

	if (!fd) {
		inputs = y_list_remove(inputs, yid);
		FREE(yid);
		return;
	}

	YAHOO_CALLBACK (ext_yahoo_write) (fd, buff, strlen(buff));

	yid->fd = fd;
	yid->read_tag =
	        YAHOO_CALLBACK (ext_yahoo_add_handler) (yid->yd->client_id, fd,
	                                                YAHOO_INPUT_READ, yid);

	FREE(buff);
	FREE(yad);
}

/* FIXME This is also likely affected */
void yahoo_set_yab(int id, struct yab *yab)
{
	struct yahoo_post_data *yad = y_new0(struct yahoo_post_data, 1);
	struct yahoo_data *yd = find_conn_by_id(id);
	struct yahoo_input_data *yid;
	char url[1024];
	char buff[1024];
	char post[1024];
	int size = 0;

	if (!yd) {
		return;
	}

	yid = y_new0(struct yahoo_input_data, 1);
	yid->type = YAHOO_CONNECTION_YAB;
	yid->yd = yd;

	if (yab->yid) {
		size = snprintf(post, sizeof(post), "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
		                "<ab k=\"%s\" cc=\"%d\">"
		                "<ct id=\"%d\" e=\"1\" yi=\"%s\" nn=\"%s\" />"
		                "</ab>", yd->user, 9, yab->yid, /* Don't know why */
		                yab->id, yab->nname ? yab->nname : "");
	} else {
		size = snprintf(post, sizeof(post), "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
		                "<ab k=\"%s\" cc=\"%d\">"
		                "<ct a=\"1\" yi=\"%s\" nn=\"%s\" />"
		                "</ab>", yd->user, 1, /* Don't know why */
		                yab->id, yab->nname ? yab->nname : "");
	}

	yad->yid = yid;
	yad->data = strdup(post);

	strcpy(url, "http://address.yahoo.com/yab/us?v=XM&prog=ymsgr&.intl=us"
	       "&sync=1&tags=short&noclear=1&useutf8=1&legenc=codepage-1252");

	snprintf(buff, sizeof(buff), "Y=%s; T=%s", yd->cookie_y, yd->cookie_t);

	inputs = y_list_prepend(inputs, yid);

	yahoo_http_post(yid->yd->client_id, url, buff, size,
	                _yahoo_http_post_connected, yad);
}

void yahoo_set_identity_status(int id, const char *identity, int active)
{
	struct yahoo_input_data *yid =
	        find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt = NULL;

	if (!yid) {
		return;
	}
	yd = yid->yd;

	pkt = yahoo_packet_new(active ? YAHOO_SERVICE_IDACT :
	                       YAHOO_SERVICE_IDDEACT, YPACKET_STATUS_DEFAULT, yd->session_id);
	yahoo_packet_hash(pkt, 3, identity);
	if (pkt) {
		yahoo_send_packet(yid, pkt, 0);
		yahoo_packet_free(pkt);
	}
}

void yahoo_refresh(int id)
{
	struct yahoo_input_data *yid =
	        find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt = NULL;

	if (!yid) {
		return;
	}
	yd = yid->yd;

	pkt = yahoo_packet_new(YAHOO_SERVICE_USERSTAT, YPACKET_STATUS_DEFAULT,
	                       yd->session_id);
	if (pkt) {
		yahoo_send_packet(yid, pkt, 0);
		yahoo_packet_free(pkt);
	}
}
#endif

void yahoo_keepalive(int id)
{
	struct yahoo_input_data *yid =
	        find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt = NULL;

	if (!yid) {
		return;
	}
	yd = yid->yd;

	pkt = yahoo_packet_new(YAHOO_SERVICE_PING, YPACKET_STATUS_DEFAULT,
	                       yd->session_id);
	yahoo_send_packet(yid, pkt, 0);
	yahoo_packet_free(pkt);
}

#if 0
void yahoo_chat_keepalive(int id)
{
	struct yahoo_input_data *yid =
	        find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt = NULL;

	if (!yid) {
		return;
	}

	yd = yid->yd;

	pkt = yahoo_packet_new(YAHOO_SERVICE_CHATPING, YPACKET_STATUS_DEFAULT,
	                       yd->session_id);
	yahoo_send_packet(yid, pkt, 0);
	yahoo_packet_free(pkt);
}
#endif

void yahoo_add_buddy(int id, const char *who, const char *group,
                     const char *msg)
{
	struct yahoo_input_data *yid =
	        find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt;

	if (!yid) {
		return;
	}
	yd = yid->yd;

	if (!yd->logged_in) {
		return;
	}

	pkt = yahoo_packet_new(YAHOO_SERVICE_ADDBUDDY, YPACKET_STATUS_DEFAULT,
	                       yd->session_id);
	if (msg != NULL) {      /* add message/request "it's me add me" */
		yahoo_packet_hash(pkt, 14, msg);
	} else {
		yahoo_packet_hash(pkt, 14, "");
	}
	yahoo_packet_hash(pkt, 65, group);
	yahoo_packet_hash(pkt, 97, "1");
	yahoo_packet_hash(pkt, 1, yd->user);
	yahoo_packet_hash(pkt, 302, "319");
	yahoo_packet_hash(pkt, 300, "319");
	yahoo_packet_hash(pkt, 7, who);
	yahoo_packet_hash(pkt, 334, "0");
	yahoo_packet_hash(pkt, 301, "319");
	yahoo_packet_hash(pkt, 303, "319");
	yahoo_send_packet(yid, pkt, 0);
	yahoo_packet_free(pkt);
}

void yahoo_remove_buddy(int id, const char *who, const char *group)
{
	struct yahoo_input_data *yid =
	        find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt = NULL;

	if (!yid) {
		return;
	}
	yd = yid->yd;

	pkt = yahoo_packet_new(YAHOO_SERVICE_REMBUDDY, YPACKET_STATUS_DEFAULT,
	                       yd->session_id);

	yahoo_packet_hash(pkt, 1, yd->user);
	yahoo_packet_hash(pkt, 7, who);
	yahoo_packet_hash(pkt, 65, group);
	yahoo_send_packet(yid, pkt, 0);
	yahoo_packet_free(pkt);
}

void yahoo_confirm_buddy(int id, const char *who, int reject, const char *msg)
{
	struct yahoo_input_data *yid =
	        find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt;

	if (!yid) {
		return;
	}
	yd = yid->yd;

	if (!yd->logged_in) {
		return;
	}

	pkt = yahoo_packet_new(YAHOO_SERVICE_Y7_AUTHORIZATION,
	                       YPACKET_STATUS_DEFAULT, yd->session_id);
	yahoo_packet_hash(pkt, 1, yd->user);
	yahoo_packet_hash(pkt, 5, who);
	if (reject) {
		yahoo_packet_hash(pkt, 13, "2");
	} else {
		yahoo_packet_hash(pkt, 241, "0");
		yahoo_packet_hash(pkt, 13, "1");
	}

	yahoo_packet_hash(pkt, 334, "0");

	if (reject) {
		yahoo_packet_hash(pkt, 14, msg ? msg : "");
		yahoo_packet_hash(pkt, 97, "1");
	}

	yahoo_send_packet(yid, pkt, 0);
	yahoo_packet_free(pkt);
}

#if 0
void yahoo_ignore_buddy(int id, const char *who, int unignore)
{
	struct yahoo_input_data *yid =
	        find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt;

	if (!yid) {
		return;
	}
	yd = yid->yd;

	if (!yd->logged_in) {
		return;
	}

	pkt = yahoo_packet_new(YAHOO_SERVICE_IGNORECONTACT,
	                       YPACKET_STATUS_DEFAULT, yd->session_id);
	yahoo_packet_hash(pkt, 1, yd->user);
	yahoo_packet_hash(pkt, 7, who);
	yahoo_packet_hash(pkt, 13, unignore ? "2" : "1");
	yahoo_send_packet(yid, pkt, 0);
	yahoo_packet_free(pkt);
}

void yahoo_stealth_buddy(int id, const char *who, int unstealth)
{
	struct yahoo_input_data *yid =
	        find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt;

	if (!yid) {
		return;
	}
	yd = yid->yd;

	if (!yd->logged_in) {
		return;
	}

	pkt = yahoo_packet_new(YAHOO_SERVICE_STEALTH_PERM,
	                       YPACKET_STATUS_DEFAULT, yd->session_id);
	yahoo_packet_hash(pkt, 1, yd->user);
	yahoo_packet_hash(pkt, 7, who);
	yahoo_packet_hash(pkt, 31, unstealth ? "2" : "1");
	yahoo_packet_hash(pkt, 13, "2");
	yahoo_send_packet(yid, pkt, 0);
	yahoo_packet_free(pkt);
}
#endif

void yahoo_change_buddy_group(int id, const char *who, const char *old_group,
                              const char *new_group)
{
	struct yahoo_input_data *yid =
	        find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt = NULL;

	if (!yid) {
		return;
	}
	yd = yid->yd;

	pkt = yahoo_packet_new(YAHOO_SERVICE_Y7_CHANGE_GROUP,
	                       YPACKET_STATUS_DEFAULT, yd->session_id);
	yahoo_packet_hash(pkt, 1, yd->user);
	yahoo_packet_hash(pkt, 302, "240");
	yahoo_packet_hash(pkt, 300, "240");
	yahoo_packet_hash(pkt, 7, who);
	yahoo_packet_hash(pkt, 224, old_group);
	yahoo_packet_hash(pkt, 264, new_group);
	yahoo_packet_hash(pkt, 301, "240");
	yahoo_packet_hash(pkt, 303, "240");

	yahoo_send_packet(yid, pkt, 0);
	yahoo_packet_free(pkt);
}

#if 0
void yahoo_group_rename(int id, const char *old_group, const char *new_group)
{
	struct yahoo_input_data *yid =
	        find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt = NULL;

	if (!yid) {
		return;
	}
	yd = yid->yd;

	pkt = yahoo_packet_new(YAHOO_SERVICE_GROUPRENAME,
	                       YPACKET_STATUS_DEFAULT, yd->session_id);
	yahoo_packet_hash(pkt, 1, yd->user);
	yahoo_packet_hash(pkt, 65, old_group);
	yahoo_packet_hash(pkt, 67, new_group);

	yahoo_send_packet(yid, pkt, 0);
	yahoo_packet_free(pkt);
}

void yahoo_conference_addinvite(int id, const char *from, const char *who,
                                const char *room, const YList *members, const char *msg)
{
	struct yahoo_input_data *yid =
	        find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt;

	if (!yid) {
		return;
	}
	yd = yid->yd;

	pkt = yahoo_packet_new(YAHOO_SERVICE_CONFADDINVITE,
	                       YPACKET_STATUS_DEFAULT, yd->session_id);

	yahoo_packet_hash(pkt, 1, (from ? from : yd->user));
	yahoo_packet_hash(pkt, 51, who);
	yahoo_packet_hash(pkt, 57, room);
	yahoo_packet_hash(pkt, 58, msg);
	yahoo_packet_hash(pkt, 13, "0");
	for (; members; members = members->next) {
		yahoo_packet_hash(pkt, 52, (char *) members->data);
		yahoo_packet_hash(pkt, 53, (char *) members->data);
	}
	/* 52, 53 -> other members? */

	yahoo_send_packet(yid, pkt, 0);

	yahoo_packet_free(pkt);
}
#endif

void yahoo_conference_invite(int id, const char *from, YList *who,
                             const char *room, const char *msg)
{
	struct yahoo_input_data *yid =
	        find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt;

	if (!yid) {
		return;
	}
	yd = yid->yd;

	pkt = yahoo_packet_new(YAHOO_SERVICE_CONFINVITE, YPACKET_STATUS_DEFAULT,
	                       yd->session_id);

	yahoo_packet_hash(pkt, 1, (from ? from : yd->user));
	yahoo_packet_hash(pkt, 50, yd->user);
	for (; who; who = who->next) {
		yahoo_packet_hash(pkt, 52, (char *) who->data);
	}
	yahoo_packet_hash(pkt, 57, room);
	yahoo_packet_hash(pkt, 58, msg);
	yahoo_packet_hash(pkt, 13, "0");

	yahoo_send_packet(yid, pkt, 0);

	yahoo_packet_free(pkt);
}

void yahoo_conference_logon(int id, const char *from, YList *who,
                            const char *room)
{
	struct yahoo_input_data *yid =
	        find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt;

	if (!yid) {
		return;
	}
	yd = yid->yd;

	pkt = yahoo_packet_new(YAHOO_SERVICE_CONFLOGON, YPACKET_STATUS_DEFAULT,
	                       yd->session_id);

	yahoo_packet_hash(pkt, 1, (from ? from : yd->user));
	yahoo_packet_hash(pkt, 3, (from ? from : yd->user));
	yahoo_packet_hash(pkt, 57, room);
	for (; who; who = who->next) {
		yahoo_packet_hash(pkt, 3, (char *) who->data);
	}

	yahoo_send_packet(yid, pkt, 0);

	yahoo_packet_free(pkt);
}

void yahoo_conference_decline(int id, const char *from, YList *who,
                              const char *room, const char *msg)
{
	struct yahoo_input_data *yid =
	        find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt;

	if (!yid) {
		return;
	}
	yd = yid->yd;

	pkt = yahoo_packet_new(YAHOO_SERVICE_CONFDECLINE,
	                       YPACKET_STATUS_DEFAULT, yd->session_id);

	yahoo_packet_hash(pkt, 1, (from ? from : yd->user));
	yahoo_packet_hash(pkt, 3, (from ? from : yd->user));
	for (; who; who = who->next) {
		yahoo_packet_hash(pkt, 3, (char *) who->data);
	}
	yahoo_packet_hash(pkt, 57, room);
	yahoo_packet_hash(pkt, 14, msg);

	yahoo_send_packet(yid, pkt, 0);

	yahoo_packet_free(pkt);
}

void yahoo_conference_logoff(int id, const char *from, YList *who,
                             const char *room)
{
	struct yahoo_input_data *yid =
	        find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt;

	if (!yid) {
		return;
	}
	yd = yid->yd;

	pkt = yahoo_packet_new(YAHOO_SERVICE_CONFLOGOFF, YPACKET_STATUS_DEFAULT,
	                       yd->session_id);

	yahoo_packet_hash(pkt, 1, (from ? from : yd->user));
	yahoo_packet_hash(pkt, 3, (from ? from : yd->user));
	for (; who; who = who->next) {
		yahoo_packet_hash(pkt, 3, (char *) who->data);
	}

	yahoo_packet_hash(pkt, 57, room);

	yahoo_send_packet(yid, pkt, 0);

	yahoo_packet_free(pkt);
}

void yahoo_conference_message(int id, const char *from, YList *who,
                              const char *room, const char *msg, int utf8)
{
	struct yahoo_input_data *yid =
	        find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt;

	if (!yid) {
		return;
	}
	yd = yid->yd;

	pkt = yahoo_packet_new(YAHOO_SERVICE_CONFMSG, YPACKET_STATUS_DEFAULT,
	                       yd->session_id);

	yahoo_packet_hash(pkt, 1, (from ? from : yd->user));
	yahoo_packet_hash(pkt, 53, (from ? from : yd->user));
	for (; who; who = who->next) {
		yahoo_packet_hash(pkt, 53, (char *) who->data);
	}

	yahoo_packet_hash(pkt, 57, room);
	yahoo_packet_hash(pkt, 14, msg);

	if (utf8) {
		yahoo_packet_hash(pkt, 97, "1");
	}

	yahoo_send_packet(yid, pkt, 0);

	yahoo_packet_free(pkt);
}

#if 0
void yahoo_get_chatrooms(int id, int chatroomid)
{
	struct yahoo_data *yd = find_conn_by_id(id);
	struct yahoo_input_data *yid;
	char url[1024];
	char buff[1024];

	if (!yd) {
		return;
	}

	yid = y_new0(struct yahoo_input_data, 1);
	yid->yd = yd;
	yid->type = YAHOO_CONNECTION_CHATCAT;

	if (chatroomid == 0) {
		snprintf(url, 1024,
		         "http://insider.msg.yahoo.com/ycontent/?chatcat=0");
	} else {
		snprintf(url, 1024,
		         "http://insider.msg.yahoo.com/ycontent/?chatroom_%d=0",
		         chatroomid);
	}

	snprintf(buff, sizeof(buff), "Y=%s; T=%s", yd->cookie_y, yd->cookie_t);

	inputs = y_list_prepend(inputs, yid);

	yahoo_http_get(yid->yd->client_id, url, buff, 0, 0,
	               _yahoo_http_connected, yid);
}

void yahoo_chat_logon(int id, const char *from, const char *room,
                      const char *roomid)
{
	struct yahoo_input_data *yid =
	        find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt;

	if (!yid) {
		return;
	}

	yd = yid->yd;

	pkt = yahoo_packet_new(YAHOO_SERVICE_CHATONLINE, YPACKET_STATUS_DEFAULT,
	                       yd->session_id);

	yahoo_packet_hash(pkt, 1, (from ? from : yd->user));
	yahoo_packet_hash(pkt, 109, yd->user);
	yahoo_packet_hash(pkt, 6, "abcde");

	yahoo_send_packet(yid, pkt, 0);

	yahoo_packet_free(pkt);

	pkt = yahoo_packet_new(YAHOO_SERVICE_CHATJOIN, YPACKET_STATUS_DEFAULT,
	                       yd->session_id);

	yahoo_packet_hash(pkt, 1, (from ? from : yd->user));
	yahoo_packet_hash(pkt, 104, room);
	yahoo_packet_hash(pkt, 129, roomid);
	yahoo_packet_hash(pkt, 62, "2");        /* ??? */

	yahoo_send_packet(yid, pkt, 0);

	yahoo_packet_free(pkt);
}

void yahoo_chat_message(int id, const char *from, const char *room,
                        const char *msg, const int msgtype, const int utf8)
{
	struct yahoo_input_data *yid =
	        find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt;
	char buf[2];

	if (!yid) {
		return;
	}

	yd = yid->yd;

	pkt = yahoo_packet_new(YAHOO_SERVICE_COMMENT, YPACKET_STATUS_DEFAULT,
	                       yd->session_id);

	yahoo_packet_hash(pkt, 1, (from ? from : yd->user));
	yahoo_packet_hash(pkt, 104, room);
	yahoo_packet_hash(pkt, 117, msg);

	snprintf(buf, sizeof(buf), "%d", msgtype);
	yahoo_packet_hash(pkt, 124, buf);

	if (utf8) {
		yahoo_packet_hash(pkt, 97, "1");
	}

	yahoo_send_packet(yid, pkt, 0);

	yahoo_packet_free(pkt);
}

void yahoo_chat_logoff(int id, const char *from)
{
	struct yahoo_input_data *yid =
	        find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt;

	if (!yid) {
		return;
	}

	yd = yid->yd;

	pkt = yahoo_packet_new(YAHOO_SERVICE_CHATLOGOUT, YPACKET_STATUS_DEFAULT,
	                       yd->session_id);

	yahoo_packet_hash(pkt, 1, (from ? from : yd->user));

	yahoo_send_packet(yid, pkt, 0);

	yahoo_packet_free(pkt);
}

void yahoo_buddyicon_request(int id, const char *who)
{
	struct yahoo_input_data *yid =
	        find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt;

	if (!yid) {
		return;
	}

	yd = yid->yd;

	pkt = yahoo_packet_new(YAHOO_SERVICE_PICTURE, YPACKET_STATUS_DEFAULT,
	                       0);
	yahoo_packet_hash(pkt, 4, yd->user);
	yahoo_packet_hash(pkt, 5, who);
	yahoo_packet_hash(pkt, 13, "1");
	yahoo_send_packet(yid, pkt, 0);

	yahoo_packet_free(pkt);
}

void yahoo_send_picture_info(int id, const char *who, const char *url,
                             int checksum)
{
	struct yahoo_input_data *yid =
	        find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt;
	char checksum_str[10];

	if (!yid) {
		return;
	}

	yd = yid->yd;

	snprintf(checksum_str, sizeof(checksum_str), "%d", checksum);

	pkt = yahoo_packet_new(YAHOO_SERVICE_PICTURE, YPACKET_STATUS_DEFAULT,
	                       0);
	yahoo_packet_hash(pkt, 1, yd->user);
	yahoo_packet_hash(pkt, 4, yd->user);
	yahoo_packet_hash(pkt, 5, who);
	yahoo_packet_hash(pkt, 13, "2");
	yahoo_packet_hash(pkt, 20, url);
	yahoo_packet_hash(pkt, 192, checksum_str);
	yahoo_send_packet(yid, pkt, 0);

	yahoo_packet_free(pkt);
}

void yahoo_send_picture_update(int id, const char *who, int type)
{
	struct yahoo_input_data *yid =
	        find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt;
	char type_str[10];

	if (!yid) {
		return;
	}

	yd = yid->yd;

	snprintf(type_str, sizeof(type_str), "%d", type);

	pkt = yahoo_packet_new(YAHOO_SERVICE_PICTURE_UPDATE,
	                       YPACKET_STATUS_DEFAULT, 0);
	yahoo_packet_hash(pkt, 1, yd->user);
	yahoo_packet_hash(pkt, 5, who);
	yahoo_packet_hash(pkt, 206, type_str);
	yahoo_send_packet(yid, pkt, 0);

	yahoo_packet_free(pkt);
}

void yahoo_send_picture_checksum(int id, const char *who, int checksum)
{
	struct yahoo_input_data *yid =
	        find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt;
	char checksum_str[10];

	if (!yid) {
		return;
	}

	yd = yid->yd;

	snprintf(checksum_str, sizeof(checksum_str), "%d", checksum);

	pkt = yahoo_packet_new(YAHOO_SERVICE_PICTURE_CHECKSUM,
	                       YPACKET_STATUS_DEFAULT, 0);
	yahoo_packet_hash(pkt, 1, yd->user);
	if (who != 0) {
		yahoo_packet_hash(pkt, 5, who);
	}
	yahoo_packet_hash(pkt, 192, checksum_str);
	yahoo_packet_hash(pkt, 212, "1");
	yahoo_send_packet(yid, pkt, 0);

	yahoo_packet_free(pkt);
}

void yahoo_webcam_close_feed(int id, const char *who)
{
	struct yahoo_input_data *yid =
	        find_input_by_id_and_webcam_user(id, who);

	if (yid) {
		yahoo_input_close(yid);
	}
}

void yahoo_webcam_get_feed(int id, const char *who)
{
	struct yahoo_input_data *yid =
	        find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt;

	if (!yid) {
		return;
	}

	/*
	 * add the user to the queue.  this is a dirty hack, since
	 * the yahoo server doesn't tell us who's key it's returning,
	 * we have to just hope that it sends back keys in the same
	 * order that we request them.
	 * The queue is popped in yahoo_process_webcam_key
	 */
	webcam_queue = y_list_append(webcam_queue, who ? strdup(who) : NULL);

	yd = yid->yd;

	pkt = yahoo_packet_new(YAHOO_SERVICE_WEBCAM, YPACKET_STATUS_DEFAULT,
	                       yd->session_id);

	yahoo_packet_hash(pkt, 1, yd->user);
	if (who != NULL) {
		yahoo_packet_hash(pkt, 5, who);
	}
	yahoo_send_packet(yid, pkt, 0);

	yahoo_packet_free(pkt);
}

void yahoo_webcam_send_image(int id, unsigned char *image, unsigned int length,
                             unsigned int timestamp)
{
	struct yahoo_input_data *yid =
	        find_input_by_id_and_type(id, YAHOO_CONNECTION_WEBCAM);
	unsigned char *packet;
	unsigned char header_len = 13;
	unsigned int pos = 0;

	if (!yid) {
		return;
	}

	packet = y_new0(unsigned char, header_len);

	packet[pos++] = header_len;
	packet[pos++] = 0;
	packet[pos++] = 5;      /* version byte?? */
	packet[pos++] = 0;
	pos += yahoo_put32(packet + pos, length);
	packet[pos++] = 2;      /* packet type, image */
	pos += yahoo_put32(packet + pos, timestamp);
	yahoo_add_to_send_queue(yid, packet, header_len);
	FREE(packet);

	if (length) {
		yahoo_add_to_send_queue(yid, image, length);
	}
}

void yahoo_webcam_accept_viewer(int id, const char *who, int accept)
{
	struct yahoo_input_data *yid =
	        find_input_by_id_and_type(id, YAHOO_CONNECTION_WEBCAM);
	char *packet = NULL;
	char *data = NULL;
	unsigned char header_len = 13;
	unsigned int pos = 0;
	unsigned int len = 0;

	if (!yid) {
		return;
	}

	data = strdup("u=");
	data = y_string_append(data, (char *) who);
	data = y_string_append(data, "\r\n");
	len = strlen(data);

	packet = y_new0(char, header_len + len);
	packet[pos++] = header_len;
	packet[pos++] = 0;
	packet[pos++] = 5;      /* version byte?? */
	packet[pos++] = 0;
	pos += yahoo_put32(packet + pos, len);
	packet[pos++] = 0;      /* packet type */
	pos += yahoo_put32(packet + pos, accept);
	memcpy(packet + pos, data, len);
	FREE(data);
	yahoo_add_to_send_queue(yid, packet, header_len + len);
	FREE(packet);
}

void yahoo_webcam_invite(int id, const char *who)
{
	struct yahoo_input_data *yid =
	        find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_packet *pkt;

	if (!yid) {
		return;
	}

	pkt = yahoo_packet_new(YAHOO_SERVICE_NOTIFY, YPACKET_STATUS_NOTIFY,
	                       yid->yd->session_id);

	yahoo_packet_hash(pkt, 49, "WEBCAMINVITE");
	yahoo_packet_hash(pkt, 14, " ");
	yahoo_packet_hash(pkt, 13, "0");
	yahoo_packet_hash(pkt, 1, yid->yd->user);
	yahoo_packet_hash(pkt, 5, who);
	yahoo_send_packet(yid, pkt, 0);

	yahoo_packet_free(pkt);
}

static void yahoo_search_internal(int id, int t, const char *text, int g,
                                  int ar, int photo, int yahoo_only, int startpos, int total)
{
	struct yahoo_data *yd = find_conn_by_id(id);
	struct yahoo_input_data *yid;
	char url[1024];
	char buff[1024];
	char *ctext, *p;

	if (!yd) {
		return;
	}

	yid = y_new0(struct yahoo_input_data, 1);
	yid->yd = yd;
	yid->type = YAHOO_CONNECTION_SEARCH;

	/*
	   age range
	   .ar=1 - 13-18, 2 - 18-25, 3 - 25-35, 4 - 35-50, 5 - 50-70, 6 - 70+
	 */

	snprintf(buff, sizeof(buff), "&.sq=%%20&.tt=%d&.ss=%d", total,
	         startpos);

	ctext = strdup(text);
	while ((p = strchr(ctext, ' '))) {
		*p = '+';
	}

	snprintf(url, 1024,
	         "http://members.yahoo.com/interests?.oc=m&.kw=%s&.sb=%d&.g=%d&.ar=0%s%s%s",
	         ctext, t, g, photo ? "&.p=y" : "", yahoo_only ? "&.pg=y" : "",
	         startpos ? buff : "");

	FREE(ctext);

	snprintf(buff, sizeof(buff), "Y=%s; T=%s", yd->cookie_y, yd->cookie_t);

	inputs = y_list_prepend(inputs, yid);
	yahoo_http_get(yid->yd->client_id, url, buff, 0, 0,
	               _yahoo_http_connected, yid);
}

void yahoo_search(int id, enum yahoo_search_type t, const char *text,
                  enum yahoo_search_gender g, enum yahoo_search_agerange ar, int photo,
                  int yahoo_only)
{
	struct yahoo_input_data *yid =
	        find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_search_state *yss;

	if (!yid) {
		return;
	}

	if (!yid->ys) {
		yid->ys = y_new0(struct yahoo_search_state, 1);
	}

	yss = yid->ys;

	FREE(yss->lsearch_text);
	yss->lsearch_type = t;
	yss->lsearch_text = strdup(text);
	yss->lsearch_gender = g;
	yss->lsearch_agerange = ar;
	yss->lsearch_photo = photo;
	yss->lsearch_yahoo_only = yahoo_only;

	yahoo_search_internal(id, t, text, g, ar, photo, yahoo_only, 0, 0);
}

void yahoo_search_again(int id, int start)
{
	struct yahoo_input_data *yid =
	        find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_search_state *yss;

	if (!yid || !yid->ys) {
		return;
	}

	yss = yid->ys;

	if (start == -1) {
		start = yss->lsearch_nstart + yss->lsearch_nfound;
	}

	yahoo_search_internal(id, yss->lsearch_type, yss->lsearch_text,
	                      yss->lsearch_gender, yss->lsearch_agerange,
	                      yss->lsearch_photo, yss->lsearch_yahoo_only,
	                      start, yss->lsearch_ntotal);
}

void yahoo_send_picture(int id, const char *name, unsigned long size,
                        yahoo_get_fd_callback callback, void *data)
{
	/* Not Implemented */
}
#endif

/* File Transfer */
static YList *active_file_transfers = NULL;

enum {
	FT_STATE_HEAD = 1,
	FT_STATE_RECV,
	FT_STATE_RECV_START,
	FT_STATE_SEND
};

struct send_file_data {
	int client_id;
	char *id;
	char *who;
	char *filename;
	char *ip_addr;
	char *token;
	int size;

	struct yahoo_input_data *yid;
	int state;

	yahoo_get_fd_callback callback;
	void *data;
};

#if 0
static char *yahoo_get_random(void)
{
	int i = 0;
	int r = 0;
	int c = 0;
	char out[25];

	out[24] = '\0';
	out[23] = '$';
	out[22] = '$';

	for (i = 0; i < 22; i++) {
		if (r == 0) {
			r = rand();
		}

		c = r % 61;

		if (c < 26) {
			out[i] = c + 'a';
		} else if (c < 52) {
			out[i] = c - 26 + 'A';
		} else {
			out[i] = c - 52 + '0';
		}

		r /= 61;
	}

	return strdup(out);
}
#endif

static int _are_same_id(const void *sfd1, const void *id)
{
	return strcmp(((struct send_file_data *) sfd1)->id, (char *) id);
}

static int _are_same_yid(const void *sfd1, const void *yid)
{
	if (((struct send_file_data *) sfd1)->yid == yid) {
		return 0;
	} else {
		return 1;
	}
}

static struct send_file_data *yahoo_get_active_transfer(char *id)
{
	YList *l = y_list_find_custom(active_file_transfers, id,
	                              _are_same_id);

	if (l) {
		return (struct send_file_data *) l->data;
	}

	return NULL;
}

static struct send_file_data *yahoo_get_active_transfer_with_yid(void *yid)
{
	YList *l = y_list_find_custom(active_file_transfers, yid,
	                              _are_same_yid);

	if (l) {
		return (struct send_file_data *) l->data;
	}

	return NULL;
}

static void yahoo_add_active_transfer(struct send_file_data *sfd)
{
	active_file_transfers = y_list_prepend(active_file_transfers, sfd);
}

static void yahoo_remove_active_transfer(struct send_file_data *sfd)
{
	if (sfd == NULL) {
		return;
	}

	active_file_transfers = y_list_remove(active_file_transfers, sfd);
	free(sfd->id);
	free(sfd->who);
	free(sfd->filename);
	free(sfd->ip_addr);
	FREE(sfd);
}

static void _yahoo_ft_upload_connected(int id, void *fd, int error, void *data)
{
	struct send_file_data *sfd = data;
	struct yahoo_input_data *yid = sfd->yid;

	if (!fd) {
		inputs = y_list_remove(inputs, yid);
		FREE(yid);
		return;
	}

	sfd->callback(id, fd, error, sfd->data);

	yid->fd = fd;
	yid->read_tag =
	        YAHOO_CALLBACK (ext_yahoo_add_handler) (yid->yd->client_id, fd,
	                                                YAHOO_INPUT_READ, yid);
}

static void yahoo_file_transfer_upload(struct yahoo_data *yd,
                                       struct send_file_data *sfd)
{
	char url[256];
	char buff[4096];
	char *sender_enc = NULL, *recv_enc = NULL, *token_enc = NULL;

	struct yahoo_input_data *yid = y_new0(struct yahoo_input_data, 1);

	yid->yd = yd;
	yid->type = YAHOO_CONNECTION_FT;

	inputs = y_list_prepend(inputs, yid);
	sfd->yid = yid;
	sfd->state = FT_STATE_SEND;

	token_enc = yahoo_urlencode(sfd->token);
	sender_enc = yahoo_urlencode(yd->user);
	recv_enc = yahoo_urlencode(sfd->who);

	snprintf(url, sizeof(url),
	         "http://%s/relay?token=%s&sender=%s&recver=%s", sfd->ip_addr,
	         token_enc, sender_enc, recv_enc);

	snprintf(buff, sizeof(buff), "T=%s; Y=%s", yd->cookie_t, yd->cookie_y);

	yahoo_http_post(yd->client_id, url, buff, sfd->size,
	                _yahoo_ft_upload_connected, sfd);

	FREE(token_enc);
	FREE(sender_enc);
	FREE(recv_enc);
}

static void yahoo_init_ft_recv(struct yahoo_data *yd,
                               struct send_file_data *sfd)
{
	char url[256];
	char buff[1024];
	char *sender_enc = NULL, *recv_enc = NULL, *token_enc = NULL;

	struct yahoo_input_data *yid = y_new0(struct yahoo_input_data, 1);

	yid->yd = yd;
	yid->type = YAHOO_CONNECTION_FT;

	inputs = y_list_prepend(inputs, yid);
	sfd->yid = yid;
	sfd->state = FT_STATE_HEAD;

	token_enc = yahoo_urlencode(sfd->token);
	sender_enc = yahoo_urlencode(sfd->who);
	recv_enc = yahoo_urlencode(yd->user);

	snprintf(url, sizeof(url),
	         "http://%s/relay?token=%s&sender=%s&recver=%s", sfd->ip_addr,
	         token_enc, sender_enc, recv_enc);

	snprintf(buff, sizeof(buff), "Y=%s; T=%s", yd->cookie_y, yd->cookie_t);

	yahoo_http_head(yid->yd->client_id, url, buff, 0, NULL,
	                _yahoo_http_connected, yid);

	FREE(token_enc);
	FREE(sender_enc);
	FREE(recv_enc);
}

static void yahoo_file_transfer_accept(struct yahoo_input_data *yid,
                                       struct send_file_data *sfd)
{
	struct yahoo_packet *pkt;

	pkt = yahoo_packet_new(YAHOO_SERVICE_Y7_FILETRANSFERACCEPT,
	                       YPACKET_STATUS_DEFAULT, yid->yd->session_id);

	yahoo_packet_hash(pkt, 1, yid->yd->user);
	yahoo_packet_hash(pkt, 5, sfd->who);
	yahoo_packet_hash(pkt, 265, sfd->id);
	yahoo_packet_hash(pkt, 27, sfd->filename);
	yahoo_packet_hash(pkt, 249, "3");
	yahoo_packet_hash(pkt, 251, sfd->token);

	yahoo_send_packet(yid, pkt, 0);

	yahoo_packet_free(pkt);

	yahoo_init_ft_recv(yid->yd, sfd);
}

static void yahoo_process_filetransferaccept(struct yahoo_input_data *yid,
                                             struct yahoo_packet *pkt)
{
	YList *l;
	struct send_file_data *sfd;
	char *id = NULL;
	char *token = NULL;

	for (l = pkt->hash; l; l = l->next) {
		struct yahoo_pair *pair = l->data;
		switch (pair->key) {
		case 4:
			/* who */
			break;
		case 5:
			/* Me... don't care */
			break;
		case 249:
			break;
		case 265:
			id = pair->value;
			break;
		case 251:
			token = pair->value;
			break;
		case 27:
			/* filename */
			break;
		}
	}

	sfd = yahoo_get_active_transfer(id);

	if (sfd) {
		sfd->token = strdup(token);

		yahoo_file_transfer_upload(yid->yd, sfd);
	} else {
		YAHOO_CALLBACK (ext_yahoo_file_transfer_done)
		        (yid->yd->client_id, YAHOO_FILE_TRANSFER_UNKNOWN,
		        sfd ? sfd->data : NULL);

		yahoo_remove_active_transfer(sfd);
	}
}

static void yahoo_process_filetransferinfo(struct yahoo_input_data *yid,
                                           struct yahoo_packet *pkt)
{
	YList *l;
	char *id = NULL;
	char *token = NULL;
	char *ip_addr = NULL;

	struct send_file_data *sfd;

	for (l = pkt->hash; l; l = l->next) {
		struct yahoo_pair *pair = l->data;
		switch (pair->key) {
		case 1:
		case 4:
			/* who */
			break;
		case 5:
			/* Me... don't care */
			break;
		case 249:
			break;
		case 265:
			id = pair->value;
			break;
		case 250:
			ip_addr = pair->value;
			break;
		case 251:
			token = pair->value;
			break;
		case 27:
			/* filename */
			break;
		}
	}

	sfd = yahoo_get_active_transfer(id);

	if (sfd) {
		sfd->token = strdup(token);
		sfd->ip_addr = strdup(ip_addr);

		yahoo_file_transfer_accept(yid, sfd);
	} else {
		YAHOO_CALLBACK (ext_yahoo_file_transfer_done)
		        (yid->yd->client_id, YAHOO_FILE_TRANSFER_UNKNOWN,
		        sfd ? sfd->data : NULL);

		yahoo_remove_active_transfer(sfd);
	}
}

static void yahoo_send_filetransferinfo(struct yahoo_data *yd,
                                        struct send_file_data *sfd)
{
	struct yahoo_input_data *yid;
	struct yahoo_packet *pkt;

	yid = find_input_by_id_and_type(yd->client_id, YAHOO_CONNECTION_PAGER);
	sfd->ip_addr = YAHOO_CALLBACK (ext_yahoo_get_ip_addr)("relay.yahoo.com");

	if (!sfd->ip_addr) {
		YAHOO_CALLBACK (ext_yahoo_file_transfer_done)
		        (yd->client_id, YAHOO_FILE_TRANSFER_RELAY, sfd->data);

		yahoo_remove_active_transfer(sfd);

		return;
	}

	pkt = yahoo_packet_new(YAHOO_SERVICE_Y7_FILETRANSFERINFO,
	                       YPACKET_STATUS_DEFAULT, yd->session_id);

	yahoo_packet_hash(pkt, 1, yd->user);
	yahoo_packet_hash(pkt, 5, sfd->who);
	yahoo_packet_hash(pkt, 265, sfd->id);
	yahoo_packet_hash(pkt, 27, sfd->filename);
	yahoo_packet_hash(pkt, 249, "3");
	yahoo_packet_hash(pkt, 250, sfd->ip_addr);

	yahoo_send_packet(yid, pkt, 0);

	yahoo_packet_free(pkt);
}

static void yahoo_process_filetransfer(struct yahoo_input_data *yid,
                                       struct yahoo_packet *pkt)
{
	YList *l;
	char *who = NULL;
	char *filename = NULL;
	char *msg = NULL;
	char *id = NULL;
	int action = 0;
	int size = 0;
	struct yahoo_data *yd = yid->yd;

	struct send_file_data *sfd;

	for (l = pkt->hash; l; l = l->next) {
		struct yahoo_pair *pair = l->data;
		switch (pair->key) {
		case 4:
			who = pair->value;
			break;
		case 5:
			/* Me... don't care */
			break;
		case 222:
			action = atoi(pair->value);
			break;
		case 265:
			id = pair->value;
			break;
		case 266: /* Don't know */
			break;
		case 302: /* Start Data? */
			break;
		case 300:
			break;
		case 27:
			filename = pair->value;
			break;
		case 28:
			size = atoi(pair->value);
			break;
		case 14:
			msg = pair->value;
		case 301: /* End Data? */
			break;
		case 303:
			break;

		}
	}

	if (action == YAHOO_FILE_TRANSFER_INIT) {
		/* Received a FT request from buddy */
		sfd = y_new0(struct send_file_data, 1);

		sfd->client_id = yd->client_id;
		sfd->id = strdup(id);
		sfd->who = strdup(who);
		sfd->filename = strdup(filename);
		sfd->size = size;

		yahoo_add_active_transfer(sfd);

		YAHOO_CALLBACK (ext_yahoo_got_file) (yd->client_id, yd->user,
		                                     who, msg, filename, size, sfd->id);
	} else {
		/* Response to our request */
		sfd = yahoo_get_active_transfer(id);

		if (sfd && action == YAHOO_FILE_TRANSFER_ACCEPT) {
			yahoo_send_filetransferinfo(yd, sfd);
		} else if (!sfd || action == YAHOO_FILE_TRANSFER_REJECT) {
			YAHOO_CALLBACK (ext_yahoo_file_transfer_done)
			        (yd->client_id, YAHOO_FILE_TRANSFER_REJECT,
			        sfd ? sfd->data : NULL);

			yahoo_remove_active_transfer(sfd);
		}
	}
}

#if 0
void yahoo_send_file(int id, const char *who, const char *msg,
                     const char *name, unsigned long size,
                     yahoo_get_fd_callback callback, void *data)
{
	struct yahoo_packet *pkt = NULL;
	char size_str[10];
	struct yahoo_input_data *yid;
	struct yahoo_data *yd;
	struct send_file_data *sfd;

	yid = find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	yd = find_conn_by_id(id);
	sfd = y_new0(struct send_file_data, 1);

	sfd->client_id = id;
	sfd->id = yahoo_get_random();
	sfd->who = strdup(who);
	sfd->filename = strdup(name);
	sfd->size = size;
	sfd->callback = callback;
	sfd->data = data;

	yahoo_add_active_transfer(sfd);

	if (!yd) {
		return;
	}

	pkt = yahoo_packet_new(YAHOO_SERVICE_Y7_FILETRANSFER,
	                       YPACKET_STATUS_DEFAULT, yd->session_id);

	snprintf(size_str, sizeof(size_str), "%ld", size);

	yahoo_packet_hash(pkt, 1, yd->user);
	yahoo_packet_hash(pkt, 5, who);
	yahoo_packet_hash(pkt, 265, sfd->id);
	yahoo_packet_hash(pkt, 222, "1");
	yahoo_packet_hash(pkt, 266, "1");
	yahoo_packet_hash(pkt, 302, "268");
	yahoo_packet_hash(pkt, 300, "268");
	yahoo_packet_hash(pkt, 27, name);
	yahoo_packet_hash(pkt, 28, size_str);
	yahoo_packet_hash(pkt, 301, "268");
	yahoo_packet_hash(pkt, 303, "268");

	yahoo_send_packet(yid, pkt, 0);

	yahoo_packet_free(pkt);
}

void yahoo_send_file_transfer_response(int client_id, int response, char *id, void *data)
{
	struct yahoo_packet *pkt = NULL;
	char resp[2];
	struct yahoo_input_data *yid;

	struct send_file_data *sfd = yahoo_get_active_transfer(id);

	sfd->data = data;

	yid = find_input_by_id_and_type(client_id, YAHOO_CONNECTION_PAGER);

	pkt = yahoo_packet_new(YAHOO_SERVICE_Y7_FILETRANSFER,
	                       YPACKET_STATUS_DEFAULT, yid->yd->session_id);

	snprintf(resp, sizeof(resp), "%d", response);

	yahoo_packet_hash(pkt, 1, yid->yd->user);
	yahoo_packet_hash(pkt, 5, sfd->who);
	yahoo_packet_hash(pkt, 265, sfd->id);
	yahoo_packet_hash(pkt, 222, resp);

	yahoo_send_packet(yid, pkt, 0);

	yahoo_packet_free(pkt);

	if (response == YAHOO_FILE_TRANSFER_REJECT) {
		yahoo_remove_active_transfer(sfd);
	}
}
#endif

static void yahoo_process_ft_connection(struct yahoo_input_data *yid, int over)
{
	struct send_file_data *sfd;
	struct yahoo_data *yd = yid->yd;

	sfd = yahoo_get_active_transfer_with_yid(yid);

	if (!sfd) {
		LOG(("Something funny happened. yid %p has no sfd.\n", yid));
		return;
	}

	/*
	 * We want to handle only the complete data with HEAD since we don't
	 * want a situation where both the GET and HEAD are active.
	 * With SEND, we really can't do much with partial response
	 */
	if ((sfd->state == FT_STATE_HEAD || sfd->state == FT_STATE_SEND)
	    && !over) {
		return;
	}

	if (sfd->state == FT_STATE_HEAD) {
		/* Do a GET */
		char url[256];
		char buff[1024];
		char *sender_enc = NULL, *recv_enc = NULL, *token_enc = NULL;

		struct yahoo_input_data *yid_ft =
		        y_new0(struct yahoo_input_data, 1);

		yid_ft->yd = yid->yd;
		yid_ft->type = YAHOO_CONNECTION_FT;

		inputs = y_list_prepend(inputs, yid_ft);
		sfd->yid = yid_ft;
		sfd->state = FT_STATE_RECV;

		token_enc = yahoo_urlencode(sfd->token);
		sender_enc = yahoo_urlencode(sfd->who);
		recv_enc = yahoo_urlencode(yd->user);

		snprintf(url, sizeof(url),
		         "http://%s/relay?token=%s&sender=%s&recver=%s", sfd->ip_addr,
		         token_enc, sender_enc, recv_enc);

		snprintf(buff, sizeof(buff), "Y=%s; T=%s", yd->cookie_y,
		         yd->cookie_t);


		yahoo_http_get(yd->client_id, url, buff, 1, 1,
		               _yahoo_http_connected, yid_ft);

		FREE(token_enc);
		FREE(sender_enc);
		FREE(recv_enc);
	} else if (sfd->state == FT_STATE_RECV ||
	           sfd->state == FT_STATE_RECV_START) {

		unsigned char *data_begin = NULL;

		if (yid->rxlen == 0) {
			yahoo_remove_active_transfer(sfd);
		}

		if (sfd->state != FT_STATE_RECV_START &&
		    (data_begin =
		             (unsigned char *) strstr((char *) yid->rxqueue,
		                                      "\r\n\r\n"))) {

			sfd->state = FT_STATE_RECV_START;

			yid->rxlen -= 4 + (data_begin - yid->rxqueue) / sizeof(char);
			data_begin += 4;

			if (yid->rxlen > 0) {
				YAHOO_CALLBACK (ext_yahoo_got_ft_data)
				        (yd->client_id, data_begin,
				        yid->rxlen, sfd->data);
			}
		} else if (sfd->state == FT_STATE_RECV_START) {
			YAHOO_CALLBACK (ext_yahoo_got_ft_data) (yd->client_id,
			                                        yid->rxqueue, yid->rxlen, sfd->data);
		}

		FREE(yid->rxqueue);
		yid->rxqueue = NULL;
		yid->rxlen = 0;
	} else if (sfd->state == FT_STATE_SEND) {
		/* Sent file completed */
		int len = 0;
		char *off = strstr((char *) yid->rxqueue, "Content-Length: ");

		if (off) {
			off += 16;
			len = atoi(off);
		}

		if (len < sfd->size) {
			YAHOO_CALLBACK (ext_yahoo_file_transfer_done)
			        (yd->client_id,
			        YAHOO_FILE_TRANSFER_FAILED, sfd->data);
		} else {
			YAHOO_CALLBACK (ext_yahoo_file_transfer_done)
			        (yd->client_id,
			        YAHOO_FILE_TRANSFER_DONE, sfd->data);
		}

		yahoo_remove_active_transfer(sfd);
	}
}

/* End File Transfer */

#if 0
enum yahoo_status yahoo_current_status(int id)
{
	struct yahoo_data *yd = find_conn_by_id(id);

	if (!yd) {
		return YAHOO_STATUS_OFFLINE;
	}
	return yd->current_status;
}

const YList *yahoo_get_buddylist(int id)
{
	struct yahoo_data *yd = find_conn_by_id(id);

	if (!yd) {
		return NULL;
	}
	return yd->buddies;
}

const YList *yahoo_get_ignorelist(int id)
{
	struct yahoo_data *yd = find_conn_by_id(id);

	if (!yd) {
		return NULL;
	}
	return yd->ignore;
}

const YList *yahoo_get_identities(int id)
{
	struct yahoo_data *yd = find_conn_by_id(id);

	if (!yd) {
		return NULL;
	}
	return yd->identities;
}

const char *yahoo_get_cookie(int id, const char *which)
{
	struct yahoo_data *yd = find_conn_by_id(id);

	if (!yd) {
		return NULL;
	}
	if (!strncasecmp(which, "y", 1)) {
		return yd->cookie_y;
	}
	if (!strncasecmp(which, "b", 1)) {
		return yd->cookie_b;
	}
	if (!strncasecmp(which, "t", 1)) {
		return yd->cookie_t;
	}
	if (!strncasecmp(which, "c", 1)) {
		return yd->cookie_c;
	}
	if (!strncasecmp(which, "login", 5)) {
		return yd->login_cookie;
	}
	return NULL;
}
#endif

const char *yahoo_get_profile_url(void)
{
	return profile_url;
}
