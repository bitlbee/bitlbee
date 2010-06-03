/*
 * libyahoo2: libyahoo2.c
 *
 * Some code copyright (C) 2002-2004, Philip S Tellis <philip.tellis AT gmx.net>
 *
 * Yahoo Search copyright (C) 2003, Konstantin Klyagin <konst AT konst.org.ua>
 *
 * Much of this code was taken and adapted from the yahoo module for
 * gaim released under the GNU GPL.  This code is also released under the 
 * GNU GPL.
 *
 * This code is derivitive of Gaim <http://gaim.sourceforge.net>
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

#ifndef _WIN32
#include <unistd.h>
#endif
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
char *strchr (), *strrchr ();
# if !HAVE_MEMCPY
#  define memcpy(d, s, n) bcopy ((s), (d), (n))
#  define memmove(d, s, n) bcopy ((s), (d), (n))
# endif
#endif

#include <sys/types.h>

#ifdef __MINGW32__
# include <winsock2.h>
# define write(a,b,c) send(a,b,c,0)
# define read(a,b,c)  recv(a,b,c,0)
#endif

#include <stdlib.h>
#include <ctype.h>

#include "sha1.h"
#include "md5.h"
#include "yahoo2.h"
#include "yahoo_httplib.h"
#include "yahoo_util.h"
#include "yahoo_fn.h"

#include "yahoo2_callbacks.h"
#include "yahoo_debug.h"
#if defined(__MINGW32__) && !defined(HAVE_GLIB)
#define snprintf _snprintf
#define vsnprintf _vsnprintf
#endif

#include "base64.h"
#include "http_client.h"

#ifdef USE_STRUCT_CALLBACKS
struct yahoo_callbacks *yc=NULL;

void yahoo_register_callbacks(struct yahoo_callbacks * tyc)
{
	yc = tyc;
}

#define YAHOO_CALLBACK(x)	yc->x
#else
#define YAHOO_CALLBACK(x)	x
#endif

static int yahoo_send_data(int fd, void *data, int len);

int yahoo_log_message(char * fmt, ...)
{
	char out[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(out, sizeof(out), fmt, ap);
	va_end(ap);
	return YAHOO_CALLBACK(ext_yahoo_log)("%s", out);
}

int yahoo_connect(char * host, int port)
{
	return YAHOO_CALLBACK(ext_yahoo_connect)(host, port);
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
static char pager_host[] = "scs.msg.yahoo.com"; 
static int pager_port = 5050;
static int fallback_ports[]={23, 25, 80, 20, 119, 8001, 8002, 5050, 0};
static char filetransfer_host[]="filetransfer.msg.yahoo.com";
static int filetransfer_port=80;
static char webcam_host[]="webcam.yahoo.com";
static int webcam_port=5100;
static char webcam_description[]="";
static char local_host[]="";
static int conn_type=Y_WCM_DSL;

static char profile_url[] = "http://profiles.yahoo.com/";

enum yahoo_service { /* these are easier to see in hex */
	YAHOO_SERVICE_LOGON = 1,
	YAHOO_SERVICE_LOGOFF,
	YAHOO_SERVICE_ISAWAY,
	YAHOO_SERVICE_ISBACK,
	YAHOO_SERVICE_IDLE, /* 5 (placemarker) */
	YAHOO_SERVICE_MESSAGE,
	YAHOO_SERVICE_IDACT,
	YAHOO_SERVICE_IDDEACT,
	YAHOO_SERVICE_MAILSTAT,
	YAHOO_SERVICE_USERSTAT, /* 0xa */
	YAHOO_SERVICE_NEWMAIL,
	YAHOO_SERVICE_CHATINVITE,
	YAHOO_SERVICE_CALENDAR,
	YAHOO_SERVICE_NEWPERSONALMAIL,
	YAHOO_SERVICE_NEWCONTACT,
	YAHOO_SERVICE_ADDIDENT, /* 0x10 */
	YAHOO_SERVICE_ADDIGNORE,
	YAHOO_SERVICE_PING,
	YAHOO_SERVICE_GOTGROUPRENAME, /* < 1, 36(old), 37(new) */
	YAHOO_SERVICE_SYSMESSAGE = 0x14,
	YAHOO_SERVICE_SKINNAME = 0x15,
	YAHOO_SERVICE_PASSTHROUGH2 = 0x16,
	YAHOO_SERVICE_CONFINVITE = 0x18,
	YAHOO_SERVICE_CONFLOGON,
	YAHOO_SERVICE_CONFDECLINE,
	YAHOO_SERVICE_CONFLOGOFF,
	YAHOO_SERVICE_CONFADDINVITE,
	YAHOO_SERVICE_CONFMSG,
	YAHOO_SERVICE_CHATLOGON,
	YAHOO_SERVICE_CHATLOGOFF,
	YAHOO_SERVICE_CHATMSG = 0x20,
	YAHOO_SERVICE_GAMELOGON = 0x28,
	YAHOO_SERVICE_GAMELOGOFF,
	YAHOO_SERVICE_GAMEMSG = 0x2a,
	YAHOO_SERVICE_FILETRANSFER = 0x46,
	YAHOO_SERVICE_VOICECHAT = 0x4A,
	YAHOO_SERVICE_NOTIFY,
	YAHOO_SERVICE_VERIFY,
	YAHOO_SERVICE_P2PFILEXFER,
	YAHOO_SERVICE_PEERTOPEER = 0x4F,	/* Checks if P2P possible */
	YAHOO_SERVICE_WEBCAM,
	YAHOO_SERVICE_AUTHRESP = 0x54,
	YAHOO_SERVICE_LIST,
	YAHOO_SERVICE_AUTH = 0x57,
	YAHOO_SERVICE_AUTHBUDDY = 0x6d,
	YAHOO_SERVICE_ADDBUDDY = 0x83,
	YAHOO_SERVICE_REMBUDDY,
	YAHOO_SERVICE_IGNORECONTACT,	/* > 1, 7, 13 < 1, 66, 13, 0*/
	YAHOO_SERVICE_REJECTCONTACT,
	YAHOO_SERVICE_GROUPRENAME = 0x89, /* > 1, 65(new), 66(0), 67(old) */
	YAHOO_SERVICE_Y7_PING = 0x8A, /* 0 - id and that's it?? */
	YAHOO_SERVICE_CHATONLINE = 0x96, /* > 109(id), 1, 6(abcde) < 0,1*/
	YAHOO_SERVICE_CHATGOTO,
	YAHOO_SERVICE_CHATJOIN,	/* > 1 104-room 129-1600326591 62-2 */
	YAHOO_SERVICE_CHATLEAVE,
	YAHOO_SERVICE_CHATEXIT = 0x9b,
	YAHOO_SERVICE_CHATADDINVITE = 0x9d,
	YAHOO_SERVICE_CHATLOGOUT = 0xa0,
	YAHOO_SERVICE_CHATPING,
	YAHOO_SERVICE_COMMENT = 0xa8,
	YAHOO_SERVICE_STEALTH = 0xb9,
	YAHOO_SERVICE_PICTURE_CHECKSUM = 0xbd,
	YAHOO_SERVICE_PICTURE = 0xbe,
	YAHOO_SERVICE_PICTURE_UPDATE = 0xc1,
	YAHOO_SERVICE_PICTURE_UPLOAD = 0xc2,
	YAHOO_SERVICE_Y6_VISIBILITY=0xc5,
	YAHOO_SERVICE_Y6_STATUS_UPDATE=0xc6,
	YAHOO_PHOTOSHARE_INIT=0xd2,	
	YAHOO_SERVICE_CONTACT_YMSG13=0xd6,
	YAHOO_PHOTOSHARE_PREV=0xd7,
	YAHOO_PHOTOSHARE_KEY=0xd8,
	YAHOO_PHOTOSHARE_TRANS=0xda,
	YAHOO_FILE_TRANSFER_INIT_YMSG13=0xdc,
	YAHOO_FILE_TRANSFER_GET_YMSG13=0xdd,
	YAHOO_FILE_TRANSFER_PUT_YMSG13=0xde,
	YAHOO_SERVICE_YMSG15_STATUS=0xf0,
	YAHOO_SERVICE_YMSG15_BUDDY_LIST=0xf1,
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
	int   lsearch_type;
	char  *lsearch_text;
	int   lsearch_gender;
	int   lsearch_agerange;
	int   lsearch_photo;
	int   lsearch_yahoo_only;
	int   lsearch_nstart;
	int   lsearch_nfound;
	int   lsearch_ntotal;
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

	int   fd;
	enum yahoo_connection_type type;
	
	unsigned char	*rxqueue;
	int   rxlen;
	int   read_tag;

	YList *txqueues;
	int   write_tag;
};

struct yahoo_server_settings {
	char *pager_host;
	int   pager_port;
	char *filetransfer_host;
	int   filetransfer_port;
	char *webcam_host;
	int   webcam_port;
	char *webcam_description;
	char *local_host;
	int   conn_type;
};

static void * _yahoo_default_server_settings()
{
	struct yahoo_server_settings *yss = y_new0(struct yahoo_server_settings, 1);

	yss->pager_host = strdup(pager_host);
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

static void * _yahoo_assign_server_settings(va_list ap)
{
	struct yahoo_server_settings *yss = _yahoo_default_server_settings();
	char *key;
	char *svalue;
	int   nvalue;

	while(1) {
		key = va_arg(ap, char *);
		if(key == NULL)
			break;

		if(!strcmp(key, "pager_host")) {
			svalue = va_arg(ap, char *);
			free(yss->pager_host);
			yss->pager_host = strdup(svalue);
		} else if(!strcmp(key, "pager_port")) {
			nvalue = va_arg(ap, int);
			yss->pager_port = nvalue;
		} else if(!strcmp(key, "filetransfer_host")) {
			svalue = va_arg(ap, char *);
			free(yss->filetransfer_host);
			yss->filetransfer_host = strdup(svalue);
		} else if(!strcmp(key, "filetransfer_port")) {
			nvalue = va_arg(ap, int);
			yss->filetransfer_port = nvalue;
		} else if(!strcmp(key, "webcam_host")) {
			svalue = va_arg(ap, char *);
			free(yss->webcam_host);
			yss->webcam_host = strdup(svalue);
		} else if(!strcmp(key, "webcam_port")) {
			nvalue = va_arg(ap, int);
			yss->webcam_port = nvalue;
		} else if(!strcmp(key, "webcam_description")) {
			svalue = va_arg(ap, char *);
			free(yss->webcam_description);
			yss->webcam_description = strdup(svalue);
		} else if(!strcmp(key, "local_host")) {
			svalue = va_arg(ap, char *);
			free(yss->local_host);
			yss->local_host = strdup(svalue);
		} else if(!strcmp(key, "conn_type")) {
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
	if(!yss)
		return;

	free(yss->pager_host);
	free(yss->filetransfer_host);
	free(yss->webcam_host);
	free(yss->webcam_description);
	free(yss->local_host);

	free(yss);
}

static YList *conns=NULL;
static YList *inputs=NULL;
static int last_id=0;

static void add_to_list(struct yahoo_data *yd)
{
	conns = y_list_prepend(conns, yd);
}
static struct yahoo_data * find_conn_by_id(int id)
{
	YList *l;
	for(l = conns; l; l = y_list_next(l)) {
		struct yahoo_data *yd = l->data;
		if(yd->client_id == id)
			return yd;
	}
	return NULL;
}
static void del_from_list(struct yahoo_data *yd)
{
	conns = y_list_remove(conns, yd);
}

/* call repeatedly to get the next one */
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

static struct yahoo_input_data * find_input_by_id_and_webcam_user(int id, const char * who)
{
	YList *l;
	LOG(("find_input_by_id_and_webcam_user"));
	for(l = inputs; l; l = y_list_next(l)) {
		struct yahoo_input_data *yid = l->data;
		if(yid->type == YAHOO_CONNECTION_WEBCAM && yid->yd->client_id == id 
				&& yid->wcm && 
				((who && yid->wcm->user && !strcmp(who, yid->wcm->user)) ||
				 !(yid->wcm->user && !who)))
			return yid;
	}
	return NULL;
}

static struct yahoo_input_data * find_input_by_id_and_type(int id, enum yahoo_connection_type type)
{
	YList *l;
	LOG(("find_input_by_id_and_type"));
	for(l = inputs; l; l = y_list_next(l)) {
		struct yahoo_input_data *yid = l->data;
		if(yid->type == type && yid->yd->client_id == id)
			return yid;
	}
	return NULL;
}

static struct yahoo_input_data * find_input_by_id_and_fd(int id, int fd)
{
	YList *l;
	LOG(("find_input_by_id_and_fd"));
	for(l = inputs; l; l = y_list_next(l)) {
		struct yahoo_input_data *yid = l->data;
		if(yid->fd == fd && yid->yd->client_id == id)
			return yid;
	}
	return NULL;
}

static int count_inputs_with_id(int id)
{
	int c=0;
	YList *l;
	LOG(("counting %d", id));
	for(l = inputs; l; l = y_list_next(l)) {
		struct yahoo_input_data *yid = l->data;
		if(yid->yd->client_id == id)
			c++;
	}
	LOG(("%d", c));
	return c;
}


extern char *yahoo_crypt(char *, char *);

/* Free a buddy list */
static void yahoo_free_buddies(YList * list)
{
	YList *l;

	for(l = list; l; l = l->next)
	{
		struct yahoo_buddy *bud = l->data;
		if(!bud)
			continue;

		FREE(bud->group);
		FREE(bud->id);
		FREE(bud->real_name);
		if(bud->yab_entry) {
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
static void yahoo_free_identities(YList * list)
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
		enum yahoo_status status, int id)
{
	struct yahoo_packet *pkt = y_new0(struct yahoo_packet, 1);

	pkt->service = service;
	pkt->status = status;
	pkt->id = id;

	return pkt;
}

static void yahoo_packet_hash(struct yahoo_packet *pkt, int key, const char *value)
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
		(*(buf) = (unsigned char)((data)>>8)&0xff), \
		(*((buf)+1) = (unsigned char)(data)&0xff),  \
		2)
#define yahoo_get16(buf) ((((*(buf))&0xff)<<8) + ((*((buf)+1)) & 0xff))
#define yahoo_put32(buf, data) ( \
		(*((buf)) = (unsigned char)((data)>>24)&0xff), \
		(*((buf)+1) = (unsigned char)((data)>>16)&0xff), \
		(*((buf)+2) = (unsigned char)((data)>>8)&0xff), \
		(*((buf)+3) = (unsigned char)(data)&0xff), \
		4)
#define yahoo_get32(buf) ((((*(buf)   )&0xff)<<24) + \
			 (((*((buf)+1))&0xff)<<16) + \
			 (((*((buf)+2))&0xff)<< 8) + \
			 (((*((buf)+3))&0xff)))

static void yahoo_packet_read(struct yahoo_packet *pkt, unsigned char *data, int len)
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
			if (data[pos] == 0xc0 && data[pos + 1] == 0x80)
				break;
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
			if (accept)
				value = malloc(len - pos + 1);
			x = 0;
			while (pos + 1 < len) {
				if (data[pos] == 0xc0 && data[pos + 1] == 0x80)
					break;
				if (accept)
					value[x++] = data[pos++];
			}
			if (accept)
				value[x] = 0;
			pos += 2;
		} else {
			accept = 0;
		}
		
		if (accept) {
			pair->value = strdup(value);
			FREE(value);
			pkt->hash = y_list_append(pkt->hash, pair);
			DEBUG_MSG(("Key: %d  \tValue: %s", pair->key, pair->value));
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

		snprintf((char *)buf, sizeof(buf), "%d", pair->key);
		strcpy((char *)data + pos, (char *)buf);
		pos += strlen((char *)buf);
		data[pos++] = 0xc0;
		data[pos++] = 0x80;

		strcpy((char *)data + pos, pair->value);
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
	if(yahoo_get_log_level() >= YAHOO_LOG_DEBUG) {
		int i;
		for (i = 0; i < len; i++) {
			if ((i % 8 == 0) && i)
				YAHOO_CALLBACK(ext_yahoo_log)(" ");
			if ((i % 16 == 0) && i)
				YAHOO_CALLBACK(ext_yahoo_log)("\n");
			YAHOO_CALLBACK(ext_yahoo_log)("%02x ", data[i]);
		}
		YAHOO_CALLBACK(ext_yahoo_log)("\n");
		for (i = 0; i < len; i++) {
			if ((i % 8 == 0) && i)
				YAHOO_CALLBACK(ext_yahoo_log)(" ");
			if ((i % 16 == 0) && i)
				YAHOO_CALLBACK(ext_yahoo_log)("\n");
			if (isprint(data[i]))
				YAHOO_CALLBACK(ext_yahoo_log)(" %c ", data[i]);
			else
				YAHOO_CALLBACK(ext_yahoo_log)(" . ");
		}
		YAHOO_CALLBACK(ext_yahoo_log)("\n");
	}
}

/* raw bytes in quasi-big-endian order to base 64 string (NUL-terminated) */
static void to_y64(unsigned char *out, const unsigned char *in, int inlen)
{
	base64_encode_real(in, inlen, out, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-");
}

static void yahoo_add_to_send_queue(struct yahoo_input_data *yid, void *data, int length)
{
	struct data_queue *tx = y_new0(struct data_queue, 1);
	tx->queue = y_new0(unsigned char, length);
	tx->len = length;
	memcpy(tx->queue, data, length);

	yid->txqueues = y_list_append(yid->txqueues, tx);

	if(!yid->write_tag)
		yid->write_tag=YAHOO_CALLBACK(ext_yahoo_add_handler)(yid->yd->client_id, yid->fd, YAHOO_INPUT_WRITE, yid);
}

static void yahoo_send_packet(struct yahoo_input_data *yid, struct yahoo_packet *pkt, int extra_pad)
{
	int pktlen = yahoo_packet_length(pkt);
	int len = YAHOO_PACKET_HDRLEN + pktlen;

	unsigned char *data;
	int pos = 0;

	if (yid->fd < 0)
		return;

	data = y_new0(unsigned char, len + 1);

	memcpy(data + pos, "YMSG", 4); pos += 4;
	pos += yahoo_put16(data + pos, YAHOO_PROTO_VER);
	pos += yahoo_put16(data + pos, 0x0000);
	pos += yahoo_put16(data + pos, pktlen + extra_pad);
	pos += yahoo_put16(data + pos, pkt->service);
	pos += yahoo_put32(data + pos, pkt->status);
	pos += yahoo_put32(data + pos, pkt->id);

	yahoo_packet_write(pkt, data + pos);

	yahoo_packet_dump(data, len);
	
	if( yid->type == YAHOO_CONNECTION_FT )
		yahoo_send_data(yid->fd, data, len);
	else
		yahoo_add_to_send_queue(yid, data, len);
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

static int yahoo_send_data(int fd, void *data, int len)
{
	int ret;
	int e;

	if (fd < 0)
		return -1;

	yahoo_packet_dump(data, len);

	do {
		ret = write(fd, data, len);
	} while(ret == -1 && errno==EINTR);
	e=errno;

	if (ret == -1)  {
		LOG(("wrote data: ERR %s", strerror(errno)));
	} else {
		LOG(("wrote data: OK"));
	}

	errno=e;
	return ret;
}

void yahoo_close(int id) 
{
	struct yahoo_data *yd = find_conn_by_id(id);
	
	if(!yd)
		return;

	del_from_list(yd);

	yahoo_free_data(yd);
	if(id == last_id)
		last_id--;
}

static void yahoo_input_close(struct yahoo_input_data *yid) 
{
	inputs = y_list_remove(inputs, yid);

	LOG(("yahoo_input_close(read)")); 
	YAHOO_CALLBACK(ext_yahoo_remove_handler)(yid->yd->client_id, yid->read_tag);
	LOG(("yahoo_input_close(write)")); 
	YAHOO_CALLBACK(ext_yahoo_remove_handler)(yid->yd->client_id, yid->write_tag);
	yid->read_tag = yid->write_tag = 0;
	if(yid->fd)
		close(yid->fd);
	yid->fd = 0;
	FREE(yid->rxqueue);
	if(count_inputs_with_id(yid->yd->client_id) == 0) {
		LOG(("closing %d", yid->yd->client_id));
		yahoo_close(yid->yd->client_id);
	}
	yahoo_free_webcam(yid->wcm);
	if(yid->wcd)
		FREE(yid->wcd);
	if(yid->ys) {
		FREE(yid->ys->lsearch_text);
		FREE(yid->ys);
	}
	FREE(yid);
}

static int is_same_bud(const void * a, const void * b) {
	const struct yahoo_buddy *subject = a;
	const struct yahoo_buddy *object = b;

	return strcmp(subject->id, object->id);
}

static YList * bud_str2list(char *rawlist)
{
	YList * l = NULL;

	char **lines;
	char **split;
	char **buddies;
	char **tmp, **bud;

	lines = y_strsplit(rawlist, "\n", -1);
	for (tmp = lines; *tmp; tmp++) {
		struct yahoo_buddy *newbud;

		split = y_strsplit(*tmp, ":", 2);
		if (!split)
			continue;
		if (!split[0] || !split[1]) {
			y_strfreev(split);
			continue;
		}
		buddies = y_strsplit(split[1], ",", -1);

		for (bud = buddies; bud && *bud; bud++) {
			newbud = y_new0(struct yahoo_buddy, 1);
			newbud->id = strdup(*bud);
			newbud->group = strdup(split[0]);

			if(y_list_find_custom(l, newbud, is_same_bud)) {
				FREE(newbud->id);
				FREE(newbud->group);
				FREE(newbud);
				continue;
			}

			newbud->real_name = NULL;

			l = y_list_append(l, newbud);

			NOTICE(("Added buddy %s to group %s", newbud->id, newbud->group));
		}

		y_strfreev(buddies);
		y_strfreev(split);
	}
	y_strfreev(lines);

	return l;
}

static char * getcookie(char *rawcookie)
{
	char * cookie=NULL;
	char * tmpcookie; 
	char * cookieend;

	if (strlen(rawcookie) < 2) 
		return NULL;

	tmpcookie = strdup(rawcookie+2);
	cookieend = strchr(tmpcookie, ';');

	if(cookieend)
		*cookieend = '\0';

	cookie = strdup(tmpcookie);
	FREE(tmpcookie);
	/* cookieend=NULL;  not sure why this was there since the value is not preserved in the stack -dd */

	return cookie;
}

static char * getlcookie(char *cookie)
{
	char *tmp;
	char *tmpend;
	char *login_cookie = NULL;

	tmpend = strstr(cookie, "n=");
	if(tmpend) {
		tmp = strdup(tmpend+2);
		tmpend = strchr(tmp, '&');
		if(tmpend)
			*tmpend='\0';
		login_cookie = strdup(tmp);
		FREE(tmp);
	}

	return login_cookie;
}

static void yahoo_process_notify(struct yahoo_input_data *yid, struct yahoo_packet *pkt)
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
		if (pair->key == 4)
			from = pair->value;
		if (pair->key == 5)
			to = pair->value;
		if (pair->key == 49)
			msg = pair->value;
		if (pair->key == 13)
			stat = atoi(pair->value);
		if (pair->key == 14)
			ind = pair->value;
		if (pair->key == 16) {	/* status == -1 */
			NOTICE((pair->value));
			return;
		}

	}

	if (!msg)
		return;
	
	if (!strncasecmp(msg, "TYPING", strlen("TYPING"))) 
		YAHOO_CALLBACK(ext_yahoo_typing_notify)(yd->client_id, to, from, stat);
	else if (!strncasecmp(msg, "GAME", strlen("GAME"))) 
		YAHOO_CALLBACK(ext_yahoo_game_notify)(yd->client_id, to, from, stat);
	else if (!strncasecmp(msg, "WEBCAMINVITE", strlen("WEBCAMINVITE"))) 
	{
		if (!strcmp(ind, " ")) {
			YAHOO_CALLBACK(ext_yahoo_webcam_invite)(yd->client_id, to, from);
		} else {
			accept = atoi(ind);
			/* accept the invitation (-1 = deny 1 = accept) */
			if (accept < 0)
				accept = 0;
			YAHOO_CALLBACK(ext_yahoo_webcam_invite_reply)(yd->client_id, to, from, accept);
		}
	}
	else
		LOG(("Got unknown notification: %s", msg));
}

static void yahoo_process_filetransfer(struct yahoo_input_data *yid, struct yahoo_packet *pkt)
{
	struct yahoo_data *yd = yid->yd;
	char *from=NULL;
	char *to=NULL;
	char *msg=NULL;
	char *url=NULL;
	long expires=0;

	char *service=NULL;

	char *filename=NULL;
	unsigned long filesize=0L;

	YList *l;
	for (l = pkt->hash; l; l = l->next) {
		struct yahoo_pair *pair = l->data;
		if (pair->key == 4)
			from = pair->value;
		if (pair->key == 5)
			to = pair->value;
		if (pair->key == 14)
			msg = pair->value;
		if (pair->key == 20)
			url = pair->value;
		if (pair->key == 38)
			expires = atol(pair->value);

		if (pair->key == 27)
			filename = pair->value;
		if (pair->key == 28)
			filesize = atol(pair->value);

		if (pair->key == 49)
			service = pair->value;
	}

	if(pkt->service == YAHOO_SERVICE_P2PFILEXFER) {
		if(strcmp("FILEXFER", service) != 0) {
			WARNING(("unhandled service 0x%02x", pkt->service));
			yahoo_dump_unhandled(pkt);
			return;
		}
	}

	if(msg) {
		char *tmp;
		tmp = strchr(msg, '\006');
		if(tmp)
			*tmp = '\0';
	}
	if(url && from)
		YAHOO_CALLBACK(ext_yahoo_got_file)(yd->client_id, to, from, url, expires, msg, filename, filesize);

}

static void yahoo_process_conference(struct yahoo_input_data *yid, struct yahoo_packet *pkt)
{
	struct yahoo_data *yd = yid->yd;
	char *msg = NULL;
	char *host = NULL;
	char *who = NULL;
	char *room = NULL;
	char *id = NULL;
	int  utf8 = 0;
	YList *members = NULL;
	YList *l;
	
	for (l = pkt->hash; l; l = l->next) {
		struct yahoo_pair *pair = l->data;
		if (pair->key == 50)
			host = pair->value;
		
		if (pair->key == 52) {		/* invite */
			members = y_list_append(members, strdup(pair->value));
		}
		if (pair->key == 53)		/* logon */
			who = pair->value;
		if (pair->key == 54)		/* decline */
			who = pair->value;
		if (pair->key == 55)		/* unavailable (status == 2) */
			who = pair->value;
		if (pair->key == 56)		/* logoff */
			who = pair->value;

		if (pair->key == 57)
			room = pair->value;

		if (pair->key == 58)		/* join message */
			msg = pair->value;
		if (pair->key == 14)		/* decline/conf message */
			msg = pair->value;

		if (pair->key == 13)
			;
		if (pair->key == 16)		/* error */
			msg = pair->value;

		if (pair->key == 1)		/* my id */
			id = pair->value;
		if (pair->key == 3)		/* message sender */
			who = pair->value;

		if (pair->key == 97)
			utf8 = atoi(pair->value);
	}

	if(!room)
		return;

	if(host) {
		for(l = members; l; l = l->next) {
			char * w = l->data;
			if(!strcmp(w, host))
				break;
		}
		if(!l)
			members = y_list_append(members, strdup(host));
	}
	/* invite, decline, join, left, message -> status == 1 */

	switch(pkt->service) {
	case YAHOO_SERVICE_CONFINVITE:
		if(pkt->status == 2)
			;
		else if(members)
			YAHOO_CALLBACK(ext_yahoo_got_conf_invite)(yd->client_id, id, host, room, msg, members);
		else if(msg)
			YAHOO_CALLBACK(ext_yahoo_error)(yd->client_id, msg, 0, E_CONFNOTAVAIL);
		break;
	case YAHOO_SERVICE_CONFADDINVITE:
		if(pkt->status == 2)
			;
		else
			YAHOO_CALLBACK(ext_yahoo_got_conf_invite)(yd->client_id, id, host, room, msg, members);
		break;
	case YAHOO_SERVICE_CONFDECLINE:
		if(who)
			YAHOO_CALLBACK(ext_yahoo_conf_userdecline)(yd->client_id, id, who, room, msg);
		break;
	case YAHOO_SERVICE_CONFLOGON:
		if(who)
			YAHOO_CALLBACK(ext_yahoo_conf_userjoin)(yd->client_id, id, who, room);
		break;
	case YAHOO_SERVICE_CONFLOGOFF:
		if(who)
			YAHOO_CALLBACK(ext_yahoo_conf_userleave)(yd->client_id, id, who, room);
		break;
	case YAHOO_SERVICE_CONFMSG:
		if(who)
			YAHOO_CALLBACK(ext_yahoo_conf_message)(yd->client_id, id, who, room, msg, utf8);
		break;
	}
}

static void yahoo_process_chat(struct yahoo_input_data *yid, struct yahoo_packet *pkt)
{
	char *msg = NULL;
	char *id = NULL;
	char *who = NULL;
	char *room = NULL;
	char *topic = NULL;
	YList *members = NULL;
	struct yahoo_chat_member *currentmember = NULL;
	int  msgtype = 1;
	int  utf8 = 0;
	int  firstjoin = 0;
	int  membercount = 0;
	int  chaterr=0;
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
				currentmember = y_new0(struct yahoo_chat_member, 1);
				currentmember->id = strdup(pair->value);
				members = y_list_append(members, currentmember);
			}
		}

		if (pair->key == 110) {
			/* age */
			if (pkt->service == YAHOO_SERVICE_CHATJOIN)
				currentmember->age = atoi(pair->value);
		}

		if (pair->key == 113) {
			/* attribs */
			if (pkt->service == YAHOO_SERVICE_CHATJOIN)
				currentmember->attribs = atoi(pair->value);
		}

		if (pair->key == 141) {
			/* alias */
			if (pkt->service == YAHOO_SERVICE_CHATJOIN)
				currentmember->alias = strdup(pair->value);
		}

		if (pair->key == 142) {
			/* location */
			if (pkt->service == YAHOO_SERVICE_CHATJOIN)
				currentmember->location = strdup(pair->value);
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
			chaterr= atoi(pair->value);
		}
	}

	if(!room) {
		if (pkt->service == YAHOO_SERVICE_CHATLOGOUT) { /* yahoo originated chat logout */
			YAHOO_CALLBACK(ext_yahoo_chat_yahoologout)(yid->yd->client_id, id);
			return ;
		}
		if (pkt->service == YAHOO_SERVICE_COMMENT && chaterr)  {
			YAHOO_CALLBACK(ext_yahoo_chat_yahooerror)(yid->yd->client_id, id);
			return;
		}

		WARNING(("We didn't get a room name, ignoring packet"));
		return;
	}

	switch(pkt->service) {
	case YAHOO_SERVICE_CHATJOIN:
		if(y_list_length(members) != membercount) {
			WARNING(("Count of members doesn't match No. of members we got"));
		}
		if(firstjoin && members) {
			YAHOO_CALLBACK(ext_yahoo_chat_join)(yid->yd->client_id, id, room, topic, members, yid->fd);
		} else if(who) {
			if(y_list_length(members) != 1) {
				WARNING(("Got more than 1 member on a normal join"));
			}
			/* this should only ever have one, but just in case */
			while(members) {
				YList *n = members->next;
				currentmember = members->data;
				YAHOO_CALLBACK(ext_yahoo_chat_userjoin)(yid->yd->client_id, id, room, currentmember);
				y_list_free_1(members);
				members=n;
			}
		}
		break;
	case YAHOO_SERVICE_CHATEXIT:
		if(who) {
			YAHOO_CALLBACK(ext_yahoo_chat_userleave)(yid->yd->client_id, id, room, who);
		}
		break;
	case YAHOO_SERVICE_COMMENT:
		if(who) {
			YAHOO_CALLBACK(ext_yahoo_chat_message)(yid->yd->client_id, id, who, room, msg, msgtype, utf8);
		}
		break;
	}
}

static void yahoo_process_message(struct yahoo_input_data *yid, struct yahoo_packet *pkt)
{
	struct yahoo_data *yd = yid->yd;
	YList *l;
	YList * messages = NULL;

	struct m {
		int  i_31;
		int  i_32;
		char *to;
		char *from;
		long tm;
		char *msg;
		int  utf8;
	} *message = y_new0(struct m, 1);

	for (l = pkt->hash; l; l = l->next) {
		struct yahoo_pair *pair = l->data;
		if (pair->key == 1 || pair->key == 4)
		{
			if(!message->from)
				message->from = pair->value;
		}
		else if (pair->key == 5)
			message->to = pair->value;
		else if (pair->key == 15)
			message->tm = strtol(pair->value, NULL, 10);
		else if (pair->key == 97)
			message->utf8 = atoi(pair->value);
			/* user message */  /* sys message */
		else if (pair->key == 14 || pair->key == 16)
			message->msg = pair->value;
		else if (pair->key == 31) {
			if(message->i_31) {
				messages = y_list_append(messages, message);
				message = y_new0(struct m, 1);
			}
			message->i_31 = atoi(pair->value);
		}
		else if (pair->key == 32)
			message->i_32 = atoi(pair->value);
		else
			LOG(("yahoo_process_message: status: %d, key: %d, value: %s",
					pkt->status, pair->key, pair->value));
	}

	messages = y_list_append(messages, message);

	for (l = messages; l; l=l->next) {
		message = l->data;
		if (pkt->service == YAHOO_SERVICE_SYSMESSAGE) {
			YAHOO_CALLBACK(ext_yahoo_system_message)(yd->client_id, message->msg);
		} else if (pkt->status <= 2 || pkt->status == 5) {
			YAHOO_CALLBACK(ext_yahoo_got_im)(yd->client_id, message->to, message->from, message->msg, message->tm, pkt->status, message->utf8);
		} else if (pkt->status == 0xffffffff) {
			YAHOO_CALLBACK(ext_yahoo_error)(yd->client_id, message->msg, 0, E_SYSTEM);
		}
		free(message);
	}

	y_list_free(messages);
}


static void yahoo_process_status(struct yahoo_input_data *yid, struct yahoo_packet *pkt)
{
	YList *l;
	struct yahoo_data *yd = yid->yd;

	struct user
	{
		char *name;	/* 7	name */
		int   state;	/* 10	state */
		int   flags;	/* 13	flags, bit 0 = pager, bit 1 = chat, bit 2 = game */
		int   mobile;	/* 60	mobile */
		char *msg;	/* 19	custom status message */
		int   away;	/* 47	away (or invisible)*/
		int   buddy_session;	/* 11	state */
		int   f17;	/* 17	in chat? then what about flags? */
		int   idle;	/* 137	seconds idle */
		int   f138;	/* 138	state */
		char *f184;	/* 184	state */
		int   f192;	/* 192	state */
		int   f10001;	/* 10001	state */
		int   f10002;	/* 10002	state */
		int   f198;	/* 198	state */
		char *f197;	/* 197	state */
		char *f205;	/* 205	state */
		int   f213;	/* 213	state */
	} *u;

	YList *users = 0;
	
	if (pkt->service == YAHOO_SERVICE_LOGOFF && pkt->status == -1) {
		YAHOO_CALLBACK(ext_yahoo_login_response)(yd->client_id, YAHOO_LOGIN_DUPL, NULL);
		return;
	}

	for (l = pkt->hash; l; l = l->next) {
		struct yahoo_pair *pair = l->data;

		switch (pair->key) {
		case 0: /* we won't actually do anything with this */
			NOTICE(("key %d:%s", pair->key, pair->value));
			break;
		case 1: /* we don't get the full buddy list here. */
			if (!yd->logged_in) {
				yd->logged_in = TRUE;
				if(yd->current_status < 0)
					yd->current_status = yd->initial_status;
				YAHOO_CALLBACK(ext_yahoo_login_response)(yd->client_id, YAHOO_LOGIN_OK, NULL);
			}
			break;
		case 8: /* how many online buddies we have */
			NOTICE(("key %d:%s", pair->key, pair->value));
			break;
		case 7: /* the current buddy */
			u = y_new0(struct user, 1);
			u->name = pair->value;
			users = y_list_prepend(users, u);
			break;
		case 10: /* state */
			((struct user*)users->data)->state = strtol(pair->value, NULL, 10);
			break;
		case 19: /* custom status message */
			((struct user*)users->data)->msg = pair->value;
			break;
		case 47: /* is it an away message or not */
			((struct user*)users->data)->away = atoi(pair->value);
			break;
		case 137: /* seconds idle */
			((struct user*)users->data)->idle = atoi(pair->value);
			break;
		case 11: /* this is the buddy's session id */
			((struct user*)users->data)->buddy_session = atoi(pair->value);
			break;
		case 17: /* in chat? */
			((struct user*)users->data)->f17 = atoi(pair->value);
			break;
		case 13: /* bitmask, bit 0 = pager, bit 1 = chat, bit 2 = game */
			((struct user*)users->data)->flags = atoi(pair->value);
			break;
		case 60: /* SMS -> 1 MOBILE USER */
			/* sometimes going offline makes this 2, but invisible never sends it */
			((struct user*)users->data)->mobile = atoi(pair->value);
			break;
		case 138:
			((struct user*)users->data)->f138 = atoi(pair->value);
			break;
		case 184:
			((struct user*)users->data)->f184 = pair->value;
			break;
		case 192:
			((struct user*)users->data)->f192 = atoi(pair->value);
			break;
		case 10001:
			((struct user*)users->data)->f10001 = atoi(pair->value);
			break;
		case 10002:
			((struct user*)users->data)->f10002 = atoi(pair->value);
			break;
		case 198:
			((struct user*)users->data)->f198 = atoi(pair->value);
			break;
		case 197:
			((struct user*)users->data)->f197 = pair->value;
			break;
		case 205:
			((struct user*)users->data)->f205 = pair->value;
			break;
		case 213:
			((struct user*)users->data)->f213 = atoi(pair->value);
			break;
		case 16: /* Custom error message */
			YAHOO_CALLBACK(ext_yahoo_error)(yd->client_id, pair->value, 0, E_CUSTOM);
			break;
		default:
			WARNING(("unknown status key %d:%s", pair->key, pair->value));
			break;
		}
	}
	
	while (users) {
		YList *t = users;
		struct user *u = users->data;

		if (u->name != NULL) {
			if (pkt->service == YAHOO_SERVICE_LOGOFF) { /* || u->flags == 0) { Not in YMSG16 */
				YAHOO_CALLBACK(ext_yahoo_status_changed)(yd->client_id, u->name, YAHOO_STATUS_OFFLINE, NULL, 1, 0, 0);
			} else {
				/* Key 47 always seems to be 1 for YMSG16 */
				if(!u->state)
					u->away = 0;
				else
					u->away = 1;

				YAHOO_CALLBACK(ext_yahoo_status_changed)(yd->client_id, u->name, u->state, u->msg, u->away, u->idle, u->mobile);
			}
		}

		users = y_list_remove_link(users, users);
		y_list_free_1(t);
		FREE(u);
	}
}

static void yahoo_process_buddy_list(struct yahoo_input_data *yid, struct yahoo_packet *pkt)
{
	struct yahoo_data *yd = yid->yd;
	YList *l;
	int last_packet = 0;
	char *cur_group = NULL;
	struct yahoo_buddy *newbud = NULL;

	/* we could be getting multiple packets here */
	for (l = pkt->hash; l; l = l->next) {
		struct yahoo_pair *pair = l->data;

		switch(pair->key) {
		case 300:
		case 301:
		case 302:
		case 303:
			if ( 315 == atoi(pair->value) )
				last_packet = 1;
			break;
		case 65:
			g_free(cur_group);
			cur_group = strdup(pair->value);
			break;
		case 7:
			newbud = y_new0(struct yahoo_buddy, 1);
			newbud->id = strdup(pair->value);
			if(cur_group)
				newbud->group = strdup(cur_group);
			else {
				struct yahoo_buddy *lastbud = (struct yahoo_buddy *)y_list_nth(
								yd->buddies, y_list_length(yd->buddies)-1)->data;
				newbud->group = strdup(lastbud->group);
			}

			yd->buddies = y_list_append(yd->buddies, newbud);

			break;
		}
	}
	
	g_free(cur_group);

	/* we could be getting multiple packets here */
	if (last_packet)
		return;

	YAHOO_CALLBACK(ext_yahoo_got_buddies)(yd->client_id, yd->buddies);

	/*** We login at the very end of the packet communication */
	if (!yd->logged_in) {
		yd->logged_in = TRUE;
		if(yd->current_status < 0)
			yd->current_status = yd->initial_status;
		YAHOO_CALLBACK(ext_yahoo_login_response)(yd->client_id, YAHOO_LOGIN_OK, NULL);
	}
}

static void yahoo_process_list(struct yahoo_input_data *yid, struct yahoo_packet *pkt)
{
	struct yahoo_data *yd = yid->yd;
	YList *l;

	if (!yd->logged_in) {
		yd->logged_in = TRUE;
		if(yd->current_status < 0)
			yd->current_status = yd->initial_status;
		YAHOO_CALLBACK(ext_yahoo_login_response)(yd->client_id, YAHOO_LOGIN_OK, NULL);
	}

	for (l = pkt->hash; l; l = l->next) {
		struct yahoo_pair *pair = l->data;

		switch(pair->key) {
		case 87: /* buddies */
			if(!yd->rawbuddylist)
				yd->rawbuddylist = strdup(pair->value);
			else {
				yd->rawbuddylist = y_string_append(yd->rawbuddylist, pair->value);
			}
			break;

		case 88: /* ignore list */
			if(!yd->ignorelist)
				yd->ignorelist = strdup("Ignore:");
			yd->ignorelist = y_string_append(yd->ignorelist, pair->value);
			break;

		case 89: /* identities */
			{
			char **identities = y_strsplit(pair->value, ",", -1);
			int i;
			for(i=0; identities[i]; i++)
				yd->identities = y_list_append(yd->identities, 
						strdup(identities[i]));
			y_strfreev(identities);
			}
			YAHOO_CALLBACK(ext_yahoo_got_identities)(yd->client_id, yd->identities);
			break;
		case 59: /* cookies */
			if(yd->ignorelist) {
				yd->ignore = bud_str2list(yd->ignorelist);
				FREE(yd->ignorelist);
				YAHOO_CALLBACK(ext_yahoo_got_ignore)(yd->client_id, yd->ignore);
			}
			if(yd->rawbuddylist) {
				yd->buddies = bud_str2list(yd->rawbuddylist);
				FREE(yd->rawbuddylist);
				YAHOO_CALLBACK(ext_yahoo_got_buddies)(yd->client_id, yd->buddies);
			}

			if(pair->value[0]=='Y') {
				FREE(yd->cookie_y);
				FREE(yd->login_cookie);

				yd->cookie_y = getcookie(pair->value);
				yd->login_cookie = getlcookie(yd->cookie_y);

			} else if(pair->value[0]=='T') {
				FREE(yd->cookie_t);
				yd->cookie_t = getcookie(pair->value);

			} else if(pair->value[0]=='C') {
				FREE(yd->cookie_c);
				yd->cookie_c = getcookie(pair->value);
			} 

			if(yd->cookie_y && yd->cookie_t)
				YAHOO_CALLBACK(ext_yahoo_got_cookies)(yd->client_id);

			break;
		case 3: /* my id */
		case 90: /* 1 */
		case 100: /* 0 */
		case 101: /* NULL */
		case 102: /* NULL */
		case 93: /* 86400/1440 */
			break;
		}
	}
}

static void yahoo_process_verify(struct yahoo_input_data *yid, struct yahoo_packet *pkt)
{
	struct yahoo_data *yd = yid->yd;

	if(pkt->status != 0x01) {
		DEBUG_MSG(("expected status: 0x01, got: %d", pkt->status));
		YAHOO_CALLBACK(ext_yahoo_login_response)(yd->client_id, YAHOO_LOGIN_LOCK, "");
		return;
	}

	pkt = yahoo_packet_new(YAHOO_SERVICE_AUTH, YAHOO_STATUS_AVAILABLE, yd->session_id);

	yahoo_packet_hash(pkt, 1, yd->user);
	yahoo_send_packet(yid, pkt, 0);

	yahoo_packet_free(pkt);

}

static void yahoo_process_picture_checksum( struct yahoo_input_data *yid, struct yahoo_packet *pkt)
{
	struct yahoo_data *yd = yid->yd;
	char *from = NULL;
	char *to = NULL;
	int checksum = 0;
	YList *l;

	for(l = pkt->hash; l; l = l->next)
	{
		struct yahoo_pair *pair = l->data;

		switch(pair->key)
		{
			case 1:
			case 4:
				from = pair->value;
			case 5:
				to = pair->value;
				break;
			case 212:
				break;
			case 192:
				checksum = atoi( pair->value );
				break;
		}
	}

	YAHOO_CALLBACK(ext_yahoo_got_buddyicon_checksum)(yd->client_id,to,from,checksum);
}

static void yahoo_process_picture(struct yahoo_input_data *yid, struct yahoo_packet *pkt)
{
	struct yahoo_data *yd = yid->yd;
	char *url = NULL;
	char *from = NULL;
	char *to = NULL;
	int status = 0;
	int checksum = 0;
	YList *l;
	
	for(l = pkt->hash; l; l = l->next)
	{
		struct yahoo_pair *pair = l->data;

		switch(pair->key)
		{
		case 1:
		case 4:		/* sender */
			from = pair->value;
			break;
		case 5:		/* we */
			to = pair->value;
			break;
		case 13:		/* request / sending */
			status = atoi( pair->value );
			break;
		case 20:		/* url */
			url = pair->value;
			break;
		case 192:	/*checksum */
			checksum = atoi( pair->value );
			break;
		}
	}

	switch( status )
	{
		case 1:	/* this is a request, ignore for now */
			YAHOO_CALLBACK(ext_yahoo_got_buddyicon_request)(yd->client_id, to, from);
			break;
		case 2:	/* this is cool - we get a picture :) */
			YAHOO_CALLBACK(ext_yahoo_got_buddyicon)(yd->client_id,to, from, url, checksum);
			break;
	}
}

static void yahoo_process_picture_upload(struct yahoo_input_data *yid, struct yahoo_packet *pkt)
{
	struct yahoo_data *yd = yid->yd;
	YList *l;
	char *url = NULL;

	if ( pkt->status != 1 )
		return;		/* something went wrong */
	
	for(l = pkt->hash; l; l = l->next)
	{
		struct yahoo_pair *pair = l->data;

		switch(pair->key)
		{
			case 5:		/* we */
				break;
			case 20:		/* url */
				url = pair->value;
				break;
			case 27:		/* local filename */
				break;
			case 38:		/* time */
				break;
		}
	}

	YAHOO_CALLBACK(ext_yahoo_buddyicon_uploaded)(yd->client_id, url);
}

static void yahoo_process_auth_pre_0x0b(struct yahoo_input_data *yid, 
		const char *seed, const char *sn)
{
	struct yahoo_data *yd = yid->yd;
	
	/* So, Yahoo has stopped supporting its older clients in India, and 
	 * undoubtedly will soon do so in the rest of the world.
	 * 
	 * The new clients use this authentication method.  I warn you in 
	 * advance, it's bizzare, convoluted, inordinately complicated.  
	 * It's also no more secure than crypt() was.  The only purpose this 
	 * scheme could serve is to prevent third part clients from connecting 
	 * to their servers.
	 *
	 * Sorry, Yahoo.
	 */

	struct yahoo_packet *pack;
	
	md5_byte_t result[16];
	md5_state_t ctx;
	char *crypt_result;
	unsigned char *password_hash = malloc(25);
	unsigned char *crypt_hash = malloc(25);
	unsigned char *hash_string_p = malloc(50 + strlen(sn));
	unsigned char *hash_string_c = malloc(50 + strlen(sn));
	
	char checksum;
	
	int sv;
	
	unsigned char *result6 = malloc(25);
	unsigned char *result96 = malloc(25);

	sv = seed[15];
	sv = (sv % 8) % 5;

	md5_init(&ctx);
	md5_append(&ctx, (md5_byte_t *)yd->password, strlen(yd->password));
	md5_finish(&ctx, result);
	to_y64(password_hash, result, 16);
	
	md5_init(&ctx);
	crypt_result = yahoo_crypt(yd->password, "$1$_2S43d5f$");  
	md5_append(&ctx, (md5_byte_t *)crypt_result, strlen(crypt_result));
	md5_finish(&ctx, result);
	to_y64(crypt_hash, result, 16);
	free(crypt_result);

	switch (sv) {
	case 0:
		checksum = seed[seed[7] % 16];
		snprintf((char *)hash_string_p, strlen(sn) + 50,
			"%c%s%s%s", checksum, password_hash, yd->user, seed);
		snprintf((char *)hash_string_c, strlen(sn) + 50,
			"%c%s%s%s", checksum, crypt_hash, yd->user, seed);
		break;
	case 1:
		checksum = seed[seed[9] % 16];
		snprintf((char *)hash_string_p, strlen(sn) + 50,
			"%c%s%s%s", checksum, yd->user, seed, password_hash);
		snprintf((char *)hash_string_c, strlen(sn) + 50,
			"%c%s%s%s", checksum, yd->user, seed, crypt_hash);
		break;
	case 2:
		checksum = seed[seed[15] % 16];
		snprintf((char *)hash_string_p, strlen(sn) + 50,
			"%c%s%s%s", checksum, seed, password_hash, yd->user);
		snprintf((char *)hash_string_c, strlen(sn) + 50,
			"%c%s%s%s", checksum, seed, crypt_hash, yd->user);
		break;
	case 3:
		checksum = seed[seed[1] % 16];
		snprintf((char *)hash_string_p, strlen(sn) + 50,
			"%c%s%s%s", checksum, yd->user, password_hash, seed);
		snprintf((char *)hash_string_c, strlen(sn) + 50,
			"%c%s%s%s", checksum, yd->user, crypt_hash, seed);
		break;
	case 4:
		checksum = seed[seed[3] % 16];
		snprintf((char *)hash_string_p, strlen(sn) + 50,
			"%c%s%s%s", checksum, password_hash, seed, yd->user);
		snprintf((char *)hash_string_c, strlen(sn) + 50,
			"%c%s%s%s", checksum, crypt_hash, seed, yd->user);
		break;
	}
		
	md5_init(&ctx);  
	md5_append(&ctx, (md5_byte_t *)hash_string_p, strlen((char *)hash_string_p));
	md5_finish(&ctx, result);
	to_y64(result6, result, 16);

	md5_init(&ctx);  
	md5_append(&ctx, (md5_byte_t *)hash_string_c, strlen((char *)hash_string_c));
	md5_finish(&ctx, result);
	to_y64(result96, result, 16);

	pack = yahoo_packet_new(YAHOO_SERVICE_AUTHRESP, yd->initial_status, yd->session_id);
	yahoo_packet_hash(pack, 0, yd->user);
	yahoo_packet_hash(pack, 6, (char *)result6);
	yahoo_packet_hash(pack, 96, (char *)result96);
	yahoo_packet_hash(pack, 1, yd->user);
		
	yahoo_send_packet(yid, pack, 0);
		
	FREE(result6);
	FREE(result96);
	FREE(password_hash);
	FREE(crypt_hash);
	FREE(hash_string_p);
	FREE(hash_string_c);

	yahoo_packet_free(pack);

}

/*
 * New auth protocol cracked by Cerulean Studios and sent in to Gaim
 */
static void yahoo_process_auth_0x0b(struct yahoo_input_data *yid, const char *seed, const char *sn)
{
	struct yahoo_packet *pack = NULL;
	struct yahoo_data *yd = yid->yd;

	md5_byte_t         result[16];
	md5_state_t        ctx;

	sha1_state_t       ctx1;
	sha1_state_t       ctx2;

	char *alphabet1 = "FBZDWAGHrJTLMNOPpRSKUVEXYChImkwQ";
	char *alphabet2 = "F0E1D2C3B4A59687abcdefghijklmnop";

	char *challenge_lookup = "qzec2tb3um1olpar8whx4dfgijknsvy5";
	char *operand_lookup = "+|&%/*^-";
	char *delimit_lookup = ",;";

	unsigned char *password_hash = malloc(25);
	unsigned char *crypt_hash = malloc(25);
	char *crypt_result = NULL;
	unsigned char pass_hash_xor1[64];
	unsigned char pass_hash_xor2[64];
	unsigned char crypt_hash_xor1[64];
	unsigned char crypt_hash_xor2[64];
	unsigned char chal[7];
	char resp_6[100];
	char resp_96[100];

	unsigned char digest1[20];
	unsigned char digest2[20];
	unsigned char magic_key_char[4];
	const unsigned char *magic_ptr;

	unsigned int  magic[64];
	unsigned int  magic_work=0;

	char comparison_src[20];

	int x, j, i;
	int cnt = 0;
	int magic_cnt = 0;
	int magic_len;
	int depth =0, table =0;

	memset(&pass_hash_xor1, 0, 64);
	memset(&pass_hash_xor2, 0, 64);
	memset(&crypt_hash_xor1, 0, 64);
	memset(&crypt_hash_xor2, 0, 64);
	memset(&digest1, 0, 20);
	memset(&digest2, 0, 20);
	memset(&magic, 0, 64);
	memset(&resp_6, 0, 100);
	memset(&resp_96, 0, 100);
	memset(&magic_key_char, 0, 4);

	/* 
	 * Magic: Phase 1.  Generate what seems to be a 30 
	 * byte value (could change if base64
	 * ends up differently?  I don't remember and I'm 
	 * tired, so use a 64 byte buffer.
	 */

	magic_ptr = (unsigned char *)seed;

	while (*magic_ptr != 0) {
		char *loc;

		/* Ignore parentheses.  */

		if (*magic_ptr == '(' || *magic_ptr == ')') {
			magic_ptr++;
			continue;
		}

		/* Characters and digits verify against 
		   the challenge lookup.
		*/

		if (isalpha(*magic_ptr) || isdigit(*magic_ptr)) {
			loc = strchr(challenge_lookup, *magic_ptr);
			if (!loc) {
				/* This isn't good */
				continue;
			}

			/* Get offset into lookup table and lsh 3. */

			magic_work = loc - challenge_lookup;
			magic_work <<= 3;

			magic_ptr++;
			continue;
		} else {
			unsigned int local_store;

			loc = strchr(operand_lookup, *magic_ptr);
			if (!loc) {
				/* Also not good. */
				continue;
			}

			local_store = loc - operand_lookup;

			/* Oops; how did this happen? */
			if (magic_cnt >= 64) 
				break;

			magic[magic_cnt++] = magic_work | local_store;
			magic_ptr++;
			continue;
		}
	}

	magic_len = magic_cnt;
	magic_cnt = 0;

	/* Magic: Phase 2.  Take generated magic value and 
	 * sprinkle fairy dust on the values. */

	for (magic_cnt = magic_len-2; magic_cnt >= 0; magic_cnt--) {
		unsigned char byte1;
		unsigned char byte2;

		/* Bad.  Abort.
		 */
		if (magic_cnt >= magic_len) {
			WARNING(("magic_cnt(%d)  magic_len(%d)", magic_cnt, magic_len))
			break;
		}

		byte1 = magic[magic_cnt];
		byte2 = magic[magic_cnt+1];

		byte1 *= 0xcd;
		byte1 ^= byte2;

		magic[magic_cnt+1] = byte1;
	}

	/* Magic: Phase 3.  This computes 20 bytes.  The first 4 bytes are used as our magic 
	 * key (and may be changed later); the next 16 bytes are an MD5 sum of the magic key 
	 * plus 3 bytes.  The 3 bytes are found by looping, and they represent the offsets 
	 * into particular functions we'll later call to potentially alter the magic key. 
	 * 
	 * %-) 
	 */ 
	
	magic_cnt = 1; 
	x = 0; 
	
	do { 
		unsigned int     bl = 0;  
		unsigned int     cl = magic[magic_cnt++]; 
		
		if (magic_cnt >= magic_len) 
			break; 
		
		if (cl > 0x7F) { 
			if (cl < 0xe0)  
				bl = cl = (cl & 0x1f) << 6;  
			else { 
				bl = magic[magic_cnt++];  
                              cl = (cl & 0x0f) << 6;  
                              bl = ((bl & 0x3f) + cl) << 6;  
			}  
			
			cl = magic[magic_cnt++];  
			bl = (cl & 0x3f) + bl;  
		} else 
			bl = cl;  
		
		comparison_src[x++] = (bl & 0xff00) >> 8;  
		comparison_src[x++] = bl & 0xff;  
	} while (x < 20); 

	/* Dump magic key into a char for SHA1 action. */
	
		
	for(x = 0; x < 4; x++) 
		magic_key_char[x] = comparison_src[x];

	/* Compute values for recursive function table! */
	memcpy( chal, magic_key_char, 4 );
        x = 1;
	for( i = 0; i < 0xFFFF && x; i++ )
	{
		for( j = 0; j < 5 && x; j++ )
		{
			chal[4] = i;
			chal[5] = i >> 8;
			chal[6] = j;
			md5_init( &ctx );
			md5_append( &ctx, chal, 7 );
			md5_finish( &ctx, result );
			if( memcmp( comparison_src + 4, result, 16 ) == 0 )
			{
				depth = i;
				table = j;
				x = 0;
			}
		}
	}

	/* Transform magic_key_char using transform table */
	x = magic_key_char[3] << 24  | magic_key_char[2] << 16 
		| magic_key_char[1] << 8 | magic_key_char[0];
	x = yahoo_xfrm( table, depth, x );
	x = yahoo_xfrm( table, depth, x );
	magic_key_char[0] = x & 0xFF;
	magic_key_char[1] = x >> 8 & 0xFF;
	magic_key_char[2] = x >> 16 & 0xFF;
	magic_key_char[3] = x >> 24 & 0xFF;

	/* Get password and crypt hashes as per usual. */
	md5_init(&ctx);
	md5_append(&ctx, (md5_byte_t *)yd->password,  strlen(yd->password));
	md5_finish(&ctx, result);
	to_y64(password_hash, result, 16);

	md5_init(&ctx);
	crypt_result = yahoo_crypt(yd->password, "$1$_2S43d5f$");  
	md5_append(&ctx, (md5_byte_t *)crypt_result, strlen(crypt_result));
	md5_finish(&ctx, result);
	to_y64(crypt_hash, result, 16);
	free(crypt_result);

	/* Our first authentication response is based off 
	 * of the password hash. */

	for (x = 0; x < (int)strlen((char *)password_hash); x++) 
		pass_hash_xor1[cnt++] = password_hash[x] ^ 0x36;

	if (cnt < 64) 
		memset(&(pass_hash_xor1[cnt]), 0x36, 64-cnt);

	cnt = 0;

	for (x = 0; x < (int)strlen((char *)password_hash); x++) 
		pass_hash_xor2[cnt++] = password_hash[x] ^ 0x5c;

	if (cnt < 64) 
		memset(&(pass_hash_xor2[cnt]), 0x5c, 64-cnt);

	sha1_init(&ctx1);
	sha1_init(&ctx2);

	/* The first context gets the password hash XORed 
	 * with 0x36 plus a magic value
	 * which we previously extrapolated from our 
	 * challenge. */

	sha1_append(&ctx1, pass_hash_xor1, 64);
	if (j >= 3 )
		ctx1.Length_Low = 0x1ff;
	sha1_append(&ctx1, magic_key_char, 4);
	sha1_finish(&ctx1, digest1);

	 /* The second context gets the password hash XORed 
	  * with 0x5c plus the SHA-1 digest
	  * of the first context. */

	sha1_append(&ctx2, pass_hash_xor2, 64);
	sha1_append(&ctx2, digest1, 20);
	sha1_finish(&ctx2, digest2);

	/* Now that we have digest2, use it to fetch 
	 * characters from an alphabet to construct
	 * our first authentication response. */

	for (x = 0; x < 20; x += 2) {
		unsigned int    val = 0;
		unsigned int    lookup = 0;
		char            byte[6];

		memset(&byte, 0, 6);

		/* First two bytes of digest stuffed 
		 *  together.
		 */

		val = digest2[x];
		val <<= 8;
		val += digest2[x+1];

		lookup = (val >> 0x0b);
		lookup &= 0x1f;
		if (lookup >= strlen(alphabet1))
			break;
		sprintf(byte, "%c", alphabet1[lookup]);
		strcat(resp_6, byte);
		strcat(resp_6, "=");

		lookup = (val >> 0x06);
		lookup &= 0x1f;
		if (lookup >= strlen(alphabet2))
			break;
		sprintf(byte, "%c", alphabet2[lookup]);
		strcat(resp_6, byte);

		lookup = (val >> 0x01);
		lookup &= 0x1f;
		if (lookup >= strlen(alphabet2))
			break;
		sprintf(byte, "%c", alphabet2[lookup]);
		strcat(resp_6, byte);

		lookup = (val & 0x01);
		if (lookup >= strlen(delimit_lookup))
			break;
		sprintf(byte, "%c", delimit_lookup[lookup]);
		strcat(resp_6, byte);
	}

	/* Our second authentication response is based off 
	 * of the crypto hash. */

	cnt = 0;
	memset(&digest1, 0, 20);
	memset(&digest2, 0, 20);

	for (x = 0; x < (int)strlen((char *)crypt_hash); x++) 
		crypt_hash_xor1[cnt++] = crypt_hash[x] ^ 0x36;

	if (cnt < 64) 
		memset(&(crypt_hash_xor1[cnt]), 0x36, 64-cnt);

	cnt = 0;

	for (x = 0; x < (int)strlen((char *)crypt_hash); x++) 
		crypt_hash_xor2[cnt++] = crypt_hash[x] ^ 0x5c;

	if (cnt < 64) 
		memset(&(crypt_hash_xor2[cnt]), 0x5c, 64-cnt);

	sha1_init(&ctx1);
	sha1_init(&ctx2);

	/* The first context gets the password hash XORed 
	 * with 0x36 plus a magic value
	 * which we previously extrapolated from our 
	 * challenge. */

	sha1_append(&ctx1, crypt_hash_xor1, 64);
	if (j >= 3 )
		ctx1.Length_Low = 0x1ff;
	sha1_append(&ctx1, magic_key_char, 4);
	sha1_finish(&ctx1, digest1);

	/* The second context gets the password hash XORed 
	 * with 0x5c plus the SHA-1 digest
	 * of the first context. */

	sha1_append(&ctx2, crypt_hash_xor2, 64);
	sha1_append(&ctx2, digest1, 20);
	sha1_finish(&ctx2, digest2);

	/* Now that we have digest2, use it to fetch 
	 * characters from an alphabet to construct
	 * our first authentication response.  */

	for (x = 0; x < 20; x += 2) {
		unsigned int val = 0;
		unsigned int lookup = 0;

		char byte[6];

		memset(&byte, 0, 6);

		/* First two bytes of digest stuffed 
		 *  together. */

		val = digest2[x];
		val <<= 8;
		val += digest2[x+1];

		lookup = (val >> 0x0b);
		lookup &= 0x1f;
		if (lookup >= strlen(alphabet1))
			break;
		sprintf(byte, "%c", alphabet1[lookup]);
		strcat(resp_96, byte);
		strcat(resp_96, "=");

		lookup = (val >> 0x06);
		lookup &= 0x1f;
		if (lookup >= strlen(alphabet2))
			break;
		sprintf(byte, "%c", alphabet2[lookup]);
		strcat(resp_96, byte);

		lookup = (val >> 0x01);
		lookup &= 0x1f;
		if (lookup >= strlen(alphabet2))
			break;
		sprintf(byte, "%c", alphabet2[lookup]);
		strcat(resp_96, byte);

		lookup = (val & 0x01);
		if (lookup >= strlen(delimit_lookup))
			break;
		sprintf(byte, "%c", delimit_lookup[lookup]);
		strcat(resp_96, byte);
	}

	pack = yahoo_packet_new(YAHOO_SERVICE_AUTHRESP, yd->initial_status, yd->session_id);
	yahoo_packet_hash(pack, 0, sn);
	yahoo_packet_hash(pack, 6, resp_6);
	yahoo_packet_hash(pack, 96, resp_96);
	yahoo_packet_hash(pack, 1, sn);
	yahoo_send_packet(yid, pack, 0);
	yahoo_packet_free(pack);

	free(password_hash);
	free(crypt_hash);
}

struct yahoo_https_auth_data
{
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
			if ((end = strchr(s, '\r')))
				return g_strndup(s, end - s);
			else
				return g_strdup(s);
		}
		
		if ((s = strchr(s, '\n')))
			s ++;
	} while (s && *s);
	
	return NULL;
}

static enum yahoo_status yahoo_https_status_parse(int code)
{
	switch (code)
	{
		case 1212: return YAHOO_LOGIN_PASSWD;
		case 1213: return YAHOO_LOGIN_LOCK;
		case 1235: return YAHOO_LOGIN_UNAME;
		default: return (enum yahoo_status) code;
	}
}

static void yahoo_process_auth_0x10(struct yahoo_input_data *yid, const char *seed, const char *sn)
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
	struct http_request *req;
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
	
	req = http_dorequest_url(url, yahoo_https_auth_token_finish, had);
	
	g_free(url);
	g_free(chal);
	g_free(passwd);
	g_free(login);
}

static void yahoo_https_auth_token_finish(struct http_request *req)
{
	struct yahoo_https_auth_data *had = req->data;
	struct yahoo_input_data *yid = had->yid;
	struct yahoo_data *yd = yid->yd;
	int st;
	
	if (req->status_code != 200) {
		YAHOO_CALLBACK(ext_yahoo_login_response)(yd->client_id, 2000 + req->status_code, NULL);
		goto fail;
	}
	
	if (sscanf(req->reply_body, "%d", &st) != 1 || st != 0) {
		YAHOO_CALLBACK(ext_yahoo_login_response)(yd->client_id, yahoo_https_status_parse(st), NULL);
		goto fail;
	}
	
	if ((had->token = yahoo_ha_find_key(req->reply_body, "ymsgr")) == NULL) {
		YAHOO_CALLBACK(ext_yahoo_login_response)(yd->client_id, 3001, NULL);
		goto fail;
	}
	
	return yahoo_https_auth_init(had);
	
fail:
	g_free(had->token);
	g_free(had->chal);
	g_free(had);
}

static void yahoo_https_auth_init(struct yahoo_https_auth_data *had)
{
	struct http_request *req;
	char *url;
	
	url = g_strdup_printf("https://login.yahoo.com/config/pwtoken_login?src=ymsgr&ts=%d&token=%s",
	                      (int) time(NULL), had->token);
	
	req = http_dorequest_url(url, yahoo_https_auth_finish, had);
	
	g_free(url);
}

static void yahoo_https_auth_finish(struct http_request *req)
{
	struct yahoo_https_auth_data *had = req->data;
	struct yahoo_input_data *yid = had->yid;
	struct yahoo_data *yd = yid->yd;
	struct yahoo_packet *pack;
	char *crumb;
	int st;
	
	md5_byte_t result[16];
	md5_state_t ctx;
	
	unsigned char yhash[32];

	if (req->status_code != 200) {
		YAHOO_CALLBACK(ext_yahoo_login_response)(yd->client_id, 2000 + req->status_code, NULL);
		goto fail;
	}
	
	if (sscanf(req->reply_body, "%d", &st) != 1 || st != 0) {
		YAHOO_CALLBACK(ext_yahoo_login_response)(yd->client_id, yahoo_https_status_parse(st), NULL);
		goto fail;
	}
	
	if ((yd->cookie_y = yahoo_ha_find_key(req->reply_body, "Y")) == NULL ||
	    (yd->cookie_t = yahoo_ha_find_key(req->reply_body, "T")) == NULL ||
	    (crumb = yahoo_ha_find_key(req->reply_body, "crumb")) == NULL) {
		YAHOO_CALLBACK(ext_yahoo_login_response)(yd->client_id, 3002, NULL);
		goto fail;
	}
	
	md5_init(&ctx);  
	md5_append(&ctx, (unsigned char*) crumb, 11);
	md5_append(&ctx, (unsigned char*) had->chal, strlen(had->chal));
	md5_finish(&ctx, result);
	to_y64(yhash, result, 16);

	pack = yahoo_packet_new(YAHOO_SERVICE_AUTHRESP, yd->initial_status, yd->session_id);
	yahoo_packet_hash(pack, 1, yd->user);
	yahoo_packet_hash(pack, 0, yd->user);
	yahoo_packet_hash(pack, 277, yd->cookie_y);
	yahoo_packet_hash(pack, 278, yd->cookie_t);
	yahoo_packet_hash(pack, 307, (char*) yhash);
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

static void yahoo_process_auth(struct yahoo_input_data *yid, struct yahoo_packet *pkt)
{
	char *seed = NULL;
	char *sn   = NULL;
	YList *l = pkt->hash;
	int m = 0;

	while (l) {
		struct yahoo_pair *pair = l->data;
		if (pair->key == 94)
			seed = pair->value;
		if (pair->key == 1)
			sn = pair->value;
		if (pair->key == 13)
			m = atoi(pair->value);
		l = l->next;
	}

	if (!seed) 
		return;

	switch (m) {
		case 0:
			yahoo_process_auth_pre_0x0b(yid, seed, sn);
			break;
		case 1:
			yahoo_process_auth_0x0b(yid, seed, sn);
			break;
		case 2:
			yahoo_process_auth_0x10(yid, seed, sn);
			break;
		default:
			/* call error */
			WARNING(("unknown auth type %d", m));
			yahoo_process_auth_0x0b(yid, seed, sn);
			break;
	}
}

static void yahoo_process_auth_resp(struct yahoo_input_data *yid, struct yahoo_packet *pkt)
{
	struct yahoo_data *yd = yid->yd;
	char *login_id;
	char *handle;
	char *url=NULL;
	int  login_status=0;

	YList *l;

	for (l = pkt->hash; l; l = l->next) {
		struct yahoo_pair *pair = l->data;
		if (pair->key == 0)
			login_id = pair->value;
		else if (pair->key == 1)
			handle = pair->value;
		else if (pair->key == 20)
			url = pair->value;
		else if (pair->key == 66)
			login_status = atoi(pair->value);
	}

	if(pkt->status == 0xffffffff) {
		YAHOO_CALLBACK(ext_yahoo_login_response)(yd->client_id, login_status, url);
	/*	yahoo_logoff(yd->client_id);*/
	}
}

static void yahoo_process_mail(struct yahoo_input_data *yid, struct yahoo_packet *pkt)
{
	struct yahoo_data *yd = yid->yd;
	char *who = NULL;
	char *email = NULL;
	char *subj = NULL;
	int count = 0;
	YList *l;

	for (l = pkt->hash; l; l = l->next) {
		struct yahoo_pair *pair = l->data;
		if (pair->key == 9)
			count = strtol(pair->value, NULL, 10);
		else if (pair->key == 43)
			who = pair->value;
		else if (pair->key == 42)
			email = pair->value;
		else if (pair->key == 18)
			subj = pair->value;
		else
			LOG(("key: %d => value: %s", pair->key, pair->value));
	}

	if (who && email && subj) {
		char from[1024];
		snprintf(from, sizeof(from), "%s (%s)", who, email);
		YAHOO_CALLBACK(ext_yahoo_mail_notify)(yd->client_id, from, subj, count);
	} else if(count > 0)
		YAHOO_CALLBACK(ext_yahoo_mail_notify)(yd->client_id, NULL, NULL, count);
}

static void yahoo_process_contact(struct yahoo_input_data *yid, struct yahoo_packet *pkt)
{
	struct yahoo_data *yd = yid->yd;
	char *id = NULL;
	char *who = NULL;
	char *msg = NULL;
	char *name = NULL;
	long tm = 0L;
	int state = YAHOO_STATUS_AVAILABLE;
	int online = FALSE;
	int away = 0;
	int idle = 0;
	int mobile = 0;

	YList *l;

	for (l = pkt->hash; l; l = l->next) {
		struct yahoo_pair *pair = l->data;
		if (pair->key == 1)
			id = pair->value;
		else if (pair->key == 3)
			who = pair->value;
		else if (pair->key == 14)
			msg = pair->value;
		else if (pair->key == 7)
			name = pair->value;
		else if (pair->key == 10)
			state = strtol(pair->value, NULL, 10);
		else if (pair->key == 15)
			tm = strtol(pair->value, NULL, 10);
		else if (pair->key == 13)
			online = strtol(pair->value, NULL, 10);
		else if (pair->key == 47)
			away = strtol(pair->value, NULL, 10);
		else if (pair->key == 137)
			idle = strtol(pair->value, NULL, 10);
		else if (pair->key == 60)
			mobile = strtol(pair->value, NULL, 10);
		
	}

	if (id)
		YAHOO_CALLBACK(ext_yahoo_contact_added)(yd->client_id, id, who, msg);
	else if (name)
		YAHOO_CALLBACK(ext_yahoo_status_changed)(yd->client_id, name, state, msg, away, idle, mobile);
	else if(pkt->status == 0x07)
		YAHOO_CALLBACK(ext_yahoo_rejected)(yd->client_id, who, msg);
}

static void yahoo_process_buddyadd(struct yahoo_input_data *yid, struct yahoo_packet *pkt)
{
	struct yahoo_data *yd = yid->yd;
	char *who = NULL;
	char *where = NULL;
	int status = 0;
	char *me = NULL;

	struct yahoo_buddy *bud=NULL;

	YList *l;
	for (l = pkt->hash; l; l = l->next) {
		struct yahoo_pair *pair = l->data;
		if (pair->key == 1)
			me = pair->value;
		if (pair->key == 7)
			who = pair->value;
		if (pair->key == 65)
			where = pair->value;
		if (pair->key == 66)
			status = strtol(pair->value, NULL, 10);
	}

	yahoo_dump_unhandled(pkt);

	if(!who)
		return;
	if(!where)
		where = "Unknown";

	/* status: 0 == Successful, 1 == Error (does not exist), 2 == Already in list */
	if( status == 0 ) {
		bud = y_new0(struct yahoo_buddy, 1);
		bud->id = strdup(who);
		bud->group = strdup(where);
		bud->real_name = NULL;
		
		yd->buddies = y_list_append(yd->buddies, bud);
	
		/* Possibly called already, but at least the call above doesn't
		   seem to happen every time (not anytime I tried). */
		YAHOO_CALLBACK(ext_yahoo_contact_added)(yd->client_id, me, who, NULL);
	}

/*	YAHOO_CALLBACK(ext_yahoo_status_changed)(yd->client_id, who, status, NULL, (status==YAHOO_STATUS_AVAILABLE?0:1)); */
}

static void yahoo_process_contact_ymsg13(struct yahoo_input_data *yid, struct yahoo_packet *pkt)
{
	char* who=NULL;
	char* me=NULL;	
	char* msg=NULL;
	YList *l;
	for (l = pkt->hash; l; l = l->next) {
		struct yahoo_pair *pair = l->data;
		if (pair->key == 4)
			who = pair->value;
		else if (pair->key == 5)
			me = pair->value;
		else
			DEBUG_MSG(("unknown key: %d = %s", pair->key, pair->value));
	}

	if(pkt->status==3)
		YAHOO_CALLBACK(ext_yahoo_contact_auth_request)(yid->yd->client_id, me, who, msg);
}

static void yahoo_process_buddydel(struct yahoo_input_data *yid, struct yahoo_packet *pkt)
{
	struct yahoo_data *yd = yid->yd;
	char *who = NULL;
	char *where = NULL;
	int unk_66 = 0;
	char *me = NULL;
	struct yahoo_buddy *bud;

	YList *buddy;

	YList *l;
	for (l = pkt->hash; l; l = l->next) {
		struct yahoo_pair *pair = l->data;
		if (pair->key == 1)
			me = pair->value;
		else if (pair->key == 7)
			who = pair->value;
		else if (pair->key == 65)
			where = pair->value;
		else if (pair->key == 66)
			unk_66 = strtol(pair->value, NULL, 10);
		else
			DEBUG_MSG(("unknown key: %d = %s", pair->key, pair->value));
	}

	if(!who || !where)
		return;
	
	bud = y_new0(struct yahoo_buddy, 1);
	bud->id = strdup(who);
	bud->group = strdup(where);

	buddy = y_list_find_custom(yd->buddies, bud, is_same_bud);

	FREE(bud->id);
	FREE(bud->group);
	FREE(bud);

	if(buddy) {
		bud = buddy->data;
		yd->buddies = y_list_remove_link(yd->buddies, buddy);
		y_list_free_1(buddy);

		FREE(bud->id);
		FREE(bud->group);
		FREE(bud->real_name);
		FREE(bud);

		bud=NULL;
	}
}

static void yahoo_process_ignore(struct yahoo_input_data *yid, struct yahoo_packet *pkt)
{
	char *who = NULL;
	int  status = 0;
	char *me = NULL;
	int  un_ignore = 0;

	YList *l;
	for (l = pkt->hash; l; l = l->next) {
		struct yahoo_pair *pair = l->data;
		if (pair->key == 0)
			who = pair->value;
		if (pair->key == 1)
			me = pair->value;
		if (pair->key == 13) /* 1 == ignore, 2 == unignore */ 
			un_ignore = strtol(pair->value, NULL, 10);
		if (pair->key == 66) 
			status = strtol(pair->value, NULL, 10);
	}


	/*
	 * status
	 * 	0  - ok
	 * 	2  - already in ignore list, could not add
	 * 	3  - not in ignore list, could not delete
	 * 	12 - is a buddy, could not add
	 */

/*	if(status)
		YAHOO_CALLBACK(ext_yahoo_error)(yd->client_id, who, 0, status);
*/	
}

static void yahoo_process_voicechat(struct yahoo_input_data *yid, struct yahoo_packet *pkt)
{
	char *who = NULL;
	char *me = NULL;
	char *room = NULL;
	char *voice_room = NULL;

	YList *l;
	for (l = pkt->hash; l; l = l->next) {
		struct yahoo_pair *pair = l->data;
		if (pair->key == 4)
			who = pair->value;
		if (pair->key == 5)
			me = pair->value;
		if (pair->key == 13)
			voice_room=pair->value;
		if (pair->key == 57) 
			room=pair->value;
	}

	NOTICE(("got voice chat invite from %s in %s to identity %s", who, room, me));
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

static void yahoo_process_ping(struct yahoo_input_data *yid, struct yahoo_packet *pkt)
{
	char *errormsg = NULL;
	
	YList *l;
	for (l = pkt->hash; l; l = l->next) {
		struct yahoo_pair *pair = l->data;
		if (pair->key == 16)
			errormsg = pair->value;
	}
	
	NOTICE(("got ping packet"));
	YAHOO_CALLBACK(ext_yahoo_got_ping)(yid->yd->client_id, errormsg);
}

static void _yahoo_webcam_get_server_connected(int fd, int error, void *d)
{
	struct yahoo_input_data *yid = d;
	char *who = yid->wcm->user;
	char *data = NULL;
	char *packet = NULL;
	unsigned char magic_nr[] = {0, 1, 0};
	unsigned char header_len = 8;
	unsigned int len = 0;
	unsigned int pos = 0;

	if(error || fd <= 0) {
		FREE(who);
		FREE(yid);
		return;
	}

	yid->fd = fd;
	inputs = y_list_prepend(inputs, yid);
	
	/* send initial packet */
	if (who)
		data = strdup("<RVWCFG>");
	else
		data = strdup("<RUPCFG>");
	yahoo_add_to_send_queue(yid, data, strlen(data));
	FREE(data);

	/* send data */
	if (who)
	{
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

	yid->read_tag=YAHOO_CALLBACK(ext_yahoo_add_handler)(yid->yd->client_id, fd, YAHOO_INPUT_READ, yid);
}

static void yahoo_webcam_get_server(struct yahoo_input_data *y, char *who, char *key)
{
	struct yahoo_input_data *yid = y_new0(struct yahoo_input_data, 1);
	struct yahoo_server_settings *yss = y->yd->server_settings;

	yid->type = YAHOO_CONNECTION_WEBCAM_MASTER;
	yid->yd = y->yd;
	yid->wcm = y_new0(struct yahoo_webcam, 1);
	yid->wcm->user = who?strdup(who):NULL;
	yid->wcm->direction = who?YAHOO_WEBCAM_DOWNLOAD:YAHOO_WEBCAM_UPLOAD;
	yid->wcm->key = strdup(key);

	YAHOO_CALLBACK(ext_yahoo_connect_async)(yid->yd->client_id, yss->webcam_host, yss->webcam_port, 
			_yahoo_webcam_get_server_connected, yid);

}

static YList *webcam_queue=NULL;
static void yahoo_process_webcam_key(struct yahoo_input_data *yid, struct yahoo_packet *pkt)
{
	char *me = NULL;
	char *key = NULL;
	char *who = NULL;

	YList *l;
	// yahoo_dump_unhandled(pkt);
	for (l = pkt->hash; l; l = l->next) {
		struct yahoo_pair *pair = l->data;
		if (pair->key == 5)
			me = pair->value;
		if (pair->key == 61) 
			key=pair->value;
	}

	l = webcam_queue;
	if(!l)
		return;
	who = l->data;
	webcam_queue = y_list_remove_link(webcam_queue, webcam_queue);
	y_list_free_1(l);
	yahoo_webcam_get_server(yid, who, key);
	FREE(who);
}

static void yahoo_packet_process(struct yahoo_input_data *yid, struct yahoo_packet *pkt)
{
	DEBUG_MSG(("yahoo_packet_process: 0x%02x", pkt->service));
	yahoo_dump_unhandled(pkt);
	switch (pkt->service)
	{
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
	case YAHOO_SERVICE_YMSG15_STATUS:
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
	case YAHOO_SERVICE_REJECTCONTACT:
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
	case YAHOO_SERVICE_FILETRANSFER:
		yahoo_process_filetransfer(yid, pkt);
		break;
	case YAHOO_SERVICE_ADDBUDDY:
		yahoo_process_buddyadd(yid, pkt);
		break;
	case YAHOO_SERVICE_CONTACT_YMSG13:
		yahoo_process_contact_ymsg13(yid,pkt);
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
	case YAHOO_SERVICE_YMSG15_BUDDY_LIST:	/* Buddy List */
		yahoo_process_buddy_list(yid, pkt);
	default:
		WARNING(("unknown service 0x%02x", pkt->service));
		yahoo_dump_unhandled(pkt);
		break;
	}
}

static struct yahoo_packet * yahoo_getdata(struct yahoo_input_data * yid)
{
	struct yahoo_packet *pkt;
	struct yahoo_data *yd = yid->yd;
	int pos = 0;
	int pktlen;

	if(!yd)
		return NULL;

	DEBUG_MSG(("rxlen is %d", yid->rxlen));
	if (yid->rxlen < YAHOO_PACKET_HDRLEN) {
		DEBUG_MSG(("len < YAHOO_PACKET_HDRLEN"));
		return NULL;
	}

	pos += 4; /* YMSG */
	pos += 2;
	pos += 2;

	pktlen = yahoo_get16(yid->rxqueue + pos); pos += 2;
	DEBUG_MSG(("%d bytes to read, rxlen is %d", 
			pktlen, yid->rxlen));

	if (yid->rxlen < (YAHOO_PACKET_HDRLEN + pktlen)) {
		DEBUG_MSG(("len < YAHOO_PACKET_HDRLEN + pktlen"));
		return NULL;
	}

	LOG(("reading packet"));
	yahoo_packet_dump(yid->rxqueue, YAHOO_PACKET_HDRLEN + pktlen);

	pkt = yahoo_packet_new(0, 0, 0);

	pkt->service = yahoo_get16(yid->rxqueue + pos); pos += 2;
	pkt->status = yahoo_get32(yid->rxqueue + pos); pos += 4;
	DEBUG_MSG(("Yahoo Service: 0x%02x Status: %d", pkt->service,
				pkt->status));
	pkt->id = yahoo_get32(yid->rxqueue + pos); pos += 4;

	yd->session_id = pkt->id;

	yahoo_packet_read(pkt, yid->rxqueue + pos, pktlen);

	yid->rxlen -= YAHOO_PACKET_HDRLEN + pktlen;
	DEBUG_MSG(("rxlen == %d, rxqueue == %p", yid->rxlen, yid->rxqueue));
	if (yid->rxlen>0) {
		unsigned char *tmp = y_memdup(yid->rxqueue + YAHOO_PACKET_HDRLEN 
				+ pktlen, yid->rxlen);
		FREE(yid->rxqueue);
		yid->rxqueue = tmp;
		DEBUG_MSG(("new rxlen == %d, rxqueue == %p", yid->rxlen, yid->rxqueue));
	} else {
		DEBUG_MSG(("freed rxqueue == %p", yid->rxqueue));
		FREE(yid->rxqueue);
	}

	return pkt;
}

static void yahoo_yab_read(struct yab *yab, unsigned char *d, int len)
{
	char *st, *en;
	char *data = (char *)d;
	data[len]='\0';

	DEBUG_MSG(("Got yab: %s", data));
	st = en = strstr(data, "userid=\"");
	if(st) {
		st += strlen("userid=\"");
		en = strchr(st, '"'); *en++ = '\0';
		yab->id = yahoo_xmldecode(st);
	}

	st = strstr(en, "fname=\"");
	if(st) {
		st += strlen("fname=\"");
		en = strchr(st, '"'); *en++ = '\0';
		yab->fname = yahoo_xmldecode(st);
	}

	st = strstr(en, "lname=\"");
	if(st) {
		st += strlen("lname=\"");
		en = strchr(st, '"'); *en++ = '\0';
		yab->lname = yahoo_xmldecode(st);
	}

	st = strstr(en, "nname=\"");
	if(st) {
		st += strlen("nname=\"");
		en = strchr(st, '"'); *en++ = '\0';
		yab->nname = yahoo_xmldecode(st);
	}

	st = strstr(en, "email=\"");
	if(st) {
		st += strlen("email=\"");
		en = strchr(st, '"'); *en++ = '\0';
		yab->email = yahoo_xmldecode(st);
	}

	st = strstr(en, "hphone=\"");
	if(st) {
		st += strlen("hphone=\"");
		en = strchr(st, '"'); *en++ = '\0';
		yab->hphone = yahoo_xmldecode(st);
	}

	st = strstr(en, "wphone=\"");
	if(st) {
		st += strlen("wphone=\"");
		en = strchr(st, '"'); *en++ = '\0';
		yab->wphone = yahoo_xmldecode(st);
	}

	st = strstr(en, "mphone=\"");
	if(st) {
		st += strlen("mphone=\"");
		en = strchr(st, '"'); *en++ = '\0';
		yab->mphone = yahoo_xmldecode(st);
	}

	st = strstr(en, "dbid=\"");
	if(st) {
		st += strlen("dbid=\"");
		en = strchr(st, '"'); *en++ = '\0';
		yab->dbid = atoi(st);
	}
}

static struct yab * yahoo_getyab(struct yahoo_input_data *yid)
{
	struct yab *yab = NULL;
	int pos = 0, end=0;
	struct yahoo_data *yd = yid->yd;

	if(!yd)
		return NULL;

	DEBUG_MSG(("rxlen is %d", yid->rxlen));

	if(yid->rxlen <= strlen("<record"))
		return NULL;

	/* start with <record */
	while(pos < yid->rxlen-strlen("<record")+1 
			&& memcmp(yid->rxqueue + pos, "<record", strlen("<record")))
		pos++;

	if(pos >= yid->rxlen-1)
		return NULL;

	end = pos+2;
	/* end with /> */
	while(end < yid->rxlen-strlen("/>")+1 && memcmp(yid->rxqueue + end, "/>", strlen("/>")))
		end++;

	if(end >= yid->rxlen-1)
		return NULL;

	yab = y_new0(struct yab, 1);
	yahoo_yab_read(yab, yid->rxqueue + pos, end+2-pos);
	

	yid->rxlen -= end+1;
	DEBUG_MSG(("rxlen == %d, rxqueue == %p", yid->rxlen, yid->rxqueue));
	if (yid->rxlen>0) {
		unsigned char *tmp = y_memdup(yid->rxqueue + end + 1, yid->rxlen);
		FREE(yid->rxqueue);
		yid->rxqueue = tmp;
		DEBUG_MSG(("new rxlen == %d, rxqueue == %p", yid->rxlen, yid->rxqueue));
	} else {
		DEBUG_MSG(("freed rxqueue == %p", yid->rxqueue));
		FREE(yid->rxqueue);
	}


	return yab;
}

static char * yahoo_getwebcam_master(struct yahoo_input_data *yid)
{
	unsigned int pos=0;
	unsigned int len=0;
	unsigned int status=0;
	char *server=NULL;
	struct yahoo_data *yd = yid->yd;

	if(!yid || !yd)
		return NULL;

	DEBUG_MSG(("rxlen is %d", yid->rxlen));

	len = yid->rxqueue[pos++];
	if (yid->rxlen < len)
		return NULL;

	/* extract status (0 = ok, 6 = webcam not online) */
	status = yid->rxqueue[pos++];

	if (status == 0)
	{
		pos += 2; /* skip next 2 bytes */
		server =  y_memdup(yid->rxqueue+pos, 16);
		pos += 16;
	}
	else if (status == 6)
	{
		YAHOO_CALLBACK(ext_yahoo_webcam_closed)
			(yd->client_id, yid->wcm->user, 4);
	}

	/* skip rest of the data */

	yid->rxlen -= len;
	DEBUG_MSG(("rxlen == %d, rxqueue == %p", yid->rxlen, yid->rxqueue));
	if (yid->rxlen>0) {
		unsigned char *tmp = y_memdup(yid->rxqueue + pos, yid->rxlen);
		FREE(yid->rxqueue);
		yid->rxqueue = tmp;
		DEBUG_MSG(("new rxlen == %d, rxqueue == %p", yid->rxlen, yid->rxqueue));
	} else {
		DEBUG_MSG(("freed rxqueue == %p", yid->rxqueue));
		FREE(yid->rxqueue);
	}

	return server;
}

static int yahoo_get_webcam_data(struct yahoo_input_data *yid)
{
	unsigned char reason=0;
	unsigned int pos=0;
	unsigned int begin=0;
	unsigned int end=0;
	unsigned int closed=0;
	unsigned char header_len=0;
	char *who;
	int connect=0;
	struct yahoo_data *yd = yid->yd;

	if(!yd)
		return -1;

	if(!yid->wcm || !yid->wcd || !yid->rxlen)
		return -1;

	DEBUG_MSG(("rxlen is %d", yid->rxlen));

	/* if we are not reading part of image then read header */
	if (!yid->wcd->to_read)
	{
		header_len=yid->rxqueue[pos++];
		yid->wcd->packet_type=0;

		if (yid->rxlen < header_len)
			return 0;

		if (header_len >= 8)
		{
			reason = yid->rxqueue[pos++];
			/* next 2 bytes should always be 05 00 */
			pos += 2;
			yid->wcd->data_size = yahoo_get32(yid->rxqueue + pos);
			pos += 4;
			yid->wcd->to_read = yid->wcd->data_size;
		}
		if (header_len >= 13)
		{
			yid->wcd->packet_type = yid->rxqueue[pos++];
			yid->wcd->timestamp = yahoo_get32(yid->rxqueue + pos);
			pos += 4;
		}

		/* skip rest of header */
		pos = header_len;
	}

	begin = pos;
	pos += yid->wcd->to_read;
	if (pos > yid->rxlen) pos = yid->rxlen;

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
	switch (yid->wcd->packet_type)
	{
		case 0x00:
			/* user requests to view webcam (uploading) */
			if (yid->wcd->data_size &&
				yid->wcm->direction == YAHOO_WEBCAM_UPLOAD) {
				end = begin;
				while (end <= yid->rxlen &&
					yid->rxqueue[end++] != 13);
				if (end > begin)
				{
					who = y_memdup(yid->rxqueue + begin, end - begin);
					who[end - begin - 1] = 0;
					YAHOO_CALLBACK(ext_yahoo_webcam_viewer)(yd->client_id, who + 2, 2);
					FREE(who);
				}
			}

			if (yid->wcm->direction == YAHOO_WEBCAM_DOWNLOAD) {
				/* timestamp/status field */
				/* 0 = declined viewing permission */
				/* 1 = accepted viewing permission */
				if (yid->wcd->timestamp == 0) {
					YAHOO_CALLBACK(ext_yahoo_webcam_closed)(yd->client_id, yid->wcm->user, 3);
				}
			}
			break;
		case 0x01: /* status packets?? */
			/* timestamp contains status info */
			/* 00 00 00 01 = we have data?? */
			break;
		case 0x02: /* image data */
			YAHOO_CALLBACK(ext_yahoo_got_webcam_image)(yd->client_id, 
					yid->wcm->user, yid->rxqueue + begin,
					yid->wcd->data_size, pos - begin,
					yid->wcd->timestamp);
			break;
		case 0x05: /* response packets when uploading */
			if (!yid->wcd->data_size) {
				YAHOO_CALLBACK(ext_yahoo_webcam_data_request)(yd->client_id, yid->wcd->timestamp);
			}
			break;
		case 0x07: /* connection is closing */
			switch(reason)
			{
				case 0x01: /* user closed connection */
					closed = 1;
					break;
				case 0x0F: /* user cancelled permission */
					closed = 2;
					break;
			}
			YAHOO_CALLBACK(ext_yahoo_webcam_closed)(yd->client_id, yid->wcm->user, closed);
			break;
		case 0x0C: /* user connected */
		case 0x0D: /* user disconnected */
			if (yid->wcd->data_size) {
				who = y_memdup(yid->rxqueue + begin, pos - begin + 1);
				who[pos - begin] = 0;
				if (yid->wcd->packet_type == 0x0C)
					connect=1;
				else
					connect=0;
				YAHOO_CALLBACK(ext_yahoo_webcam_viewer)(yd->client_id, who, connect);
				FREE(who);
			}
			break;
		case 0x13: /* user data */
			/* i=user_ip (ip of the user we are viewing) */
			/* j=user_ext_ip (external ip of the user we */
 			/*                are viewing) */
			break;
		case 0x17: /* ?? */
			break;
	}
	yid->wcd->to_read -= pos - begin;

	yid->rxlen -= pos;
	DEBUG_MSG(("rxlen == %d, rxqueue == %p", yid->rxlen, yid->rxqueue));
	if (yid->rxlen>0) {
		unsigned char *tmp = y_memdup(yid->rxqueue + pos, yid->rxlen);
		FREE(yid->rxqueue);
		yid->rxqueue = tmp;
		DEBUG_MSG(("new rxlen == %d, rxqueue == %p", yid->rxlen, yid->rxqueue));
	} else {
		DEBUG_MSG(("freed rxqueue == %p", yid->rxqueue));
		FREE(yid->rxqueue);
	}

	/* If we read a complete packet return success */
	if (!yid->wcd->to_read)
		return 1;

	return 0;
}

int yahoo_write_ready(int id, int fd, void *data)
{
	struct yahoo_input_data *yid = data;
	int len;
	struct data_queue *tx;

	LOG(("write callback: id=%d fd=%d data=%p", id, fd, data));
	if(!yid || !yid->txqueues || !find_conn_by_id(id))
		return -2;
	
	tx = yid->txqueues->data;
	LOG(("writing %d bytes", tx->len));
	len = yahoo_send_data(fd, tx->queue, MIN(1024, tx->len));

	if(len == -1 && errno == EAGAIN)
		return 1;

	if(len <= 0) {
		int e = errno;
		DEBUG_MSG(("len == %d (<= 0)", len));
		while(yid->txqueues) {
			YList *l=yid->txqueues;
			tx = l->data;
			free(tx->queue);
			free(tx);
			yid->txqueues = y_list_remove_link(yid->txqueues, yid->txqueues);
			y_list_free_1(l);
		}
		LOG(("yahoo_write_ready(%d, %d) len < 0", id, fd));
		YAHOO_CALLBACK(ext_yahoo_remove_handler)(id, yid->write_tag);
		yid->write_tag = 0;
		errno=e;
		return 0;
	}


	tx->len -= len;
	if(tx->len > 0) {
		unsigned char *tmp = y_memdup(tx->queue + len, tx->len);
		FREE(tx->queue);
		tx->queue = tmp;
	} else {
		YList *l=yid->txqueues;
		free(tx->queue);
		free(tx);
		yid->txqueues = y_list_remove_link(yid->txqueues, yid->txqueues);
		y_list_free_1(l);
		/*
		if(!yid->txqueues) 
			LOG(("yahoo_write_ready(%d, %d) !yxqueues", id, fd));
		*/
		if(!yid->txqueues) {
			LOG(("yahoo_write_ready(%d, %d) !yxqueues", id, fd));
			YAHOO_CALLBACK(ext_yahoo_remove_handler)(id, yid->write_tag);
			yid->write_tag = 0;
		}
	}

	return 1;
}

static void yahoo_process_pager_connection(struct yahoo_input_data *yid, int over)
{
	struct yahoo_packet *pkt;
	struct yahoo_data *yd = yid->yd;
	int id = yd->client_id;

	if(over)
		return;

	while (find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER) 
			&& (pkt = yahoo_getdata(yid)) != NULL) {

		yahoo_packet_process(yid, pkt);

		yahoo_packet_free(pkt);
	}
}

static void yahoo_process_ft_connection(struct yahoo_input_data *yid, int over)
{
}

static void yahoo_process_chatcat_connection(struct yahoo_input_data *yid, int over)
{
	if(over)
		return;

	if (strstr((char*)yid->rxqueue+(yid->rxlen-20), "</content>")) {
		YAHOO_CALLBACK(ext_yahoo_chat_cat_xml)(yid->yd->client_id, (char*)yid->rxqueue);
	}
}

static void yahoo_process_yab_connection(struct yahoo_input_data *yid, int over)
{
	struct yahoo_data *yd = yid->yd;
	struct yab *yab;
	YList *buds;
	int changed=0;
	int id = yd->client_id;

	if(over)
		return;

	while(find_input_by_id_and_type(id, YAHOO_CONNECTION_YAB) 
			&& (yab = yahoo_getyab(yid)) != NULL) {
		if(!yab->id)
			continue;
		changed=1;
		for(buds = yd->buddies; buds; buds=buds->next) {
			struct yahoo_buddy * bud = buds->data;
			if(!strcmp(bud->id, yab->id)) {
				bud->yab_entry = yab;
				if(yab->nname) {
					bud->real_name = strdup(yab->nname);
				} else if(yab->fname && yab->lname) {
					bud->real_name = y_new0(char, 
							strlen(yab->fname)+
							strlen(yab->lname)+2
							);
					sprintf(bud->real_name, "%s %s",
							yab->fname, yab->lname);
				} else if(yab->fname) {
					bud->real_name = strdup(yab->fname);
				}
				break; /* for */
			}
		}
	}

	if(changed)
		YAHOO_CALLBACK(ext_yahoo_got_buddies)(yd->client_id, yd->buddies);
}

static void yahoo_process_search_connection(struct yahoo_input_data *yid, int over)
{
	struct yahoo_found_contact *yct=NULL;
	char *p = (char *)yid->rxqueue, *np, *cp;
	int k, n;
	int start=0, found=0, total=0;
	YList *contacts=NULL;
	struct yahoo_input_data *pyid = find_input_by_id_and_type(yid->yd->client_id, YAHOO_CONNECTION_PAGER);

	if(!over || !pyid)
		return;

	if(p && (p=strstr(p, "\r\n\r\n"))) {
		p += 4;

		for(k = 0; (p = strchr(p, 4)) && (k < 4); k++) {
			p++;
			n = atoi(p);
			switch(k) {
				case 0: found = pyid->ys->lsearch_nfound = n; break;
				case 2: start = pyid->ys->lsearch_nstart = n; break;
				case 3: total = pyid->ys->lsearch_ntotal = n; break;
			}
		}

		if(p)
			p++;

		k=0;
		while(p && *p) {
			cp = p;
			np = strchr(p, 4);

			if(!np)
				break;
			*np = 0;
			p = np+1;

			switch(k++) {
				case 1:
					if(strlen(cp) > 2 && y_list_length(contacts) < total) {
						yct = y_new0(struct yahoo_found_contact, 1);
						contacts = y_list_append(contacts, yct);
						yct->id = cp+2;
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
					if(strcmp(cp, "5") != 0)
						yct->location = cp;
					k = 0;
					break;
			}
		}
	}

	YAHOO_CALLBACK(ext_yahoo_got_search_result)(yid->yd->client_id, found, start, total, contacts);

	while(contacts) {
		YList *node = contacts;
		contacts = y_list_remove_link(contacts, node);
		free(node->data);
		y_list_free_1(node);
	}
}

static void _yahoo_webcam_connected(int fd, int error, void *d)
{
	struct yahoo_input_data *yid = d;
	struct yahoo_webcam *wcm = yid->wcm;
	struct yahoo_data *yd = yid->yd;
	char conn_type[100];
	char *data=NULL;
	char *packet=NULL;
	unsigned char magic_nr[] = {1, 0, 0, 0, 1};
	unsigned header_len=0;
	unsigned int len=0;
	unsigned int pos=0;

	if(error || fd <= 0) {
		FREE(yid);
		return;
	}

	yid->fd = fd;
	inputs = y_list_prepend(inputs, yid);

	LOG(("Connected"));
	/* send initial packet */
	switch (wcm->direction)
	{
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
	switch (wcm->direction)
	{
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
	switch (wcm->direction)
	{
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
	if (wcm->direction == YAHOO_WEBCAM_UPLOAD)
	{
		memcpy(packet + pos, magic_nr, sizeof(magic_nr));
		pos += sizeof(magic_nr);
	}
	memcpy(packet + pos, data, len);
	yahoo_add_to_send_queue(yid, packet, header_len + len);
	FREE(packet);
	FREE(data);

	yid->read_tag=YAHOO_CALLBACK(ext_yahoo_add_handler)(yid->yd->client_id, yid->fd, YAHOO_INPUT_READ, yid);
}

static void yahoo_webcam_connect(struct yahoo_input_data *y)
{
	struct yahoo_webcam *wcm = y->wcm;
	struct yahoo_input_data *yid;
	struct yahoo_server_settings *yss;

	if (!wcm || !wcm->server || !wcm->key)
		return;

	yid = y_new0(struct yahoo_input_data, 1);
	yid->type = YAHOO_CONNECTION_WEBCAM;
	yid->yd = y->yd;

	/* copy webcam data to new connection */
	yid->wcm = y->wcm;
	y->wcm = NULL;

	yss = y->yd->server_settings;

	yid->wcd = y_new0(struct yahoo_webcam_data, 1);

	LOG(("Connecting to: %s:%d", wcm->server, wcm->port));
	YAHOO_CALLBACK(ext_yahoo_connect_async)(y->yd->client_id, wcm->server, wcm->port,
			_yahoo_webcam_connected, yid);

}

static void yahoo_process_webcam_master_connection(struct yahoo_input_data *yid, int over)
{
	char* server;
	struct yahoo_server_settings *yss;

	if(over)
		return;

	server = yahoo_getwebcam_master(yid);

	if (server)
	{
		yss = yid->yd->server_settings;
		yid->wcm->server = strdup(server);
		yid->wcm->port = yss->webcam_port;
		yid->wcm->conn_type = yss->conn_type;
		yid->wcm->my_ip = strdup(yss->local_host);
		if (yid->wcm->direction == YAHOO_WEBCAM_UPLOAD)
			yid->wcm->description = strdup(yss->webcam_description);
		yahoo_webcam_connect(yid);
		FREE(server);
	}
}

static void yahoo_process_webcam_connection(struct yahoo_input_data *yid, int over)
{
	int id = yid->yd->client_id;
	int fd = yid->fd;

	if(over)
		return;

	/* as long as we still have packets available keep processing them */
	while (find_input_by_id_and_fd(id, fd) 
			&& yahoo_get_webcam_data(yid) == 1);
}

static void (*yahoo_process_connection[])(struct yahoo_input_data *, int over) = {
	yahoo_process_pager_connection,
	yahoo_process_ft_connection,
	yahoo_process_yab_connection,
	yahoo_process_webcam_master_connection,
	yahoo_process_webcam_connection,
	yahoo_process_chatcat_connection,
	yahoo_process_search_connection,
};

int yahoo_read_ready(int id, int fd, void *data)
{
	struct yahoo_input_data *yid = data;
	char buf[1024];
	int len;

	LOG(("read callback: id=%d fd=%d data=%p", id, fd, data));
	if(!yid)
		return -2;

	
	do {
		len = read(fd, buf, sizeof(buf));
	} while(len == -1 && errno == EINTR);

	if(len == -1 && (errno == EAGAIN||errno == EINTR))	/* we'll try again later */
		return 1;

	if (len <= 0) {
		int e = errno;
		DEBUG_MSG(("len == %d (<= 0)", len));

		if(yid->type == YAHOO_CONNECTION_PAGER) {
			YAHOO_CALLBACK(ext_yahoo_error)(yid->yd->client_id, "Connection closed by server", 1, E_CONNECTION);
		}

		yahoo_process_connection[yid->type](yid, 1);
		yahoo_input_close(yid);

		/* no need to return an error, because we've already fixed it */
		if(len == 0)
			return 1;

		errno=e;
		LOG(("read error: %s", strerror(errno)));
		return -1;
	}

	yid->rxqueue = y_renew(unsigned char, yid->rxqueue, len + yid->rxlen);
	memcpy(yid->rxqueue + yid->rxlen, buf, len);
	yid->rxlen += len;

	yahoo_process_connection[yid->type](yid, 0);

	return len;
}

int yahoo_init_with_attributes(const char *username, const char *password, ...)
{
	va_list ap;
	struct yahoo_data *yd;

	yd = y_new0(struct yahoo_data, 1);

	if(!yd)
		return 0;

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

struct connect_callback_data {
	struct yahoo_data *yd;
	int tag;
	int i;
};

static void yahoo_connected(int fd, int error, void *data)
{
	struct connect_callback_data *ccd = data;
	struct yahoo_data *yd = ccd->yd;
	struct yahoo_packet *pkt;
	struct yahoo_input_data *yid;
	struct yahoo_server_settings *yss = yd->server_settings;

	if(error) {
		if(fallback_ports[ccd->i]) {
			int tag;
			yss->pager_port = fallback_ports[ccd->i++];
			tag = YAHOO_CALLBACK(ext_yahoo_connect_async)(yd->client_id, yss->pager_host,
					yss->pager_port, yahoo_connected, ccd);

			if(tag > 0)
				ccd->tag=tag;
		} else {
			FREE(ccd);
			YAHOO_CALLBACK(ext_yahoo_login_response)(yd->client_id, YAHOO_LOGIN_SOCK, NULL);
		}
		return;
	}

	FREE(ccd);

	/* fd < 0 && error == 0 means connect was cancelled */
	if(fd < 0)
		return;

	pkt = yahoo_packet_new(YAHOO_SERVICE_AUTH, YAHOO_STATUS_AVAILABLE, yd->session_id);
	NOTICE(("Sending initial packet"));

	yahoo_packet_hash(pkt, 1, yd->user);

	yid = y_new0(struct yahoo_input_data, 1);
	yid->yd = yd;
	yid->fd = fd;
	inputs = y_list_prepend(inputs, yid);

	yahoo_send_packet(yid, pkt, 0);

	yahoo_packet_free(pkt);

	yid->read_tag=YAHOO_CALLBACK(ext_yahoo_add_handler)(yid->yd->client_id, yid->fd, YAHOO_INPUT_READ, yid);
}

void yahoo_login(int id, int initial)
{
	struct yahoo_data *yd = find_conn_by_id(id);
	struct connect_callback_data *ccd;
	struct yahoo_server_settings *yss;
	int tag;

	if(!yd)
		return;

	yss = yd->server_settings;

	yd->initial_status = initial;

	ccd = y_new0(struct connect_callback_data, 1);
	ccd->yd = yd;
	tag = YAHOO_CALLBACK(ext_yahoo_connect_async)(yd->client_id, yss->pager_host, yss->pager_port, 
			yahoo_connected, ccd);

	/*
	 * if tag <= 0, then callback has already been called
	 * so ccd will have been freed
	 */
	if(tag > 0)
		ccd->tag = tag;
	else if(tag < 0)
		YAHOO_CALLBACK(ext_yahoo_login_response)(yd->client_id, YAHOO_LOGIN_SOCK, NULL);
}


int yahoo_get_fd(int id)
{
	struct yahoo_input_data *yid = find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	if(!yid)
		return 0;
	else
		return yid->fd;
}

void yahoo_send_im(int id, const char *from, const char *who, const char *what, int utf8, int picture)
{
	struct yahoo_input_data *yid = find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_packet *pkt = NULL;
	struct yahoo_data *yd;
	char pic_str[10];

	if(!yid)
		return;

	yd = yid->yd;

	pkt = yahoo_packet_new(YAHOO_SERVICE_MESSAGE, YAHOO_STATUS_OFFLINE, yd->session_id);

	snprintf(pic_str, sizeof(pic_str), "%d", picture);
	
	if(from && strcmp(from, yd->user))
		yahoo_packet_hash(pkt, 0, yd->user);
	yahoo_packet_hash(pkt, 1, from?from:yd->user);
	yahoo_packet_hash(pkt, 5, who);
	yahoo_packet_hash(pkt, 14, what);

	if(utf8)
		yahoo_packet_hash(pkt, 97, "1");

	yahoo_packet_hash(pkt, 63, ";0");	/* imvironment name; or ;0 */
	yahoo_packet_hash(pkt, 64, "0");
	yahoo_packet_hash(pkt, 206, pic_str);


	yahoo_send_packet(yid, pkt, 0);

	yahoo_packet_free(pkt);
}

void yahoo_send_typing(int id, const char *from, const char *who, int typ)
{
	struct yahoo_input_data *yid = find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt = NULL;
	if(!yid)
		return;

	yd = yid->yd;
	pkt = yahoo_packet_new(YAHOO_SERVICE_NOTIFY, YAHOO_STATUS_NOTIFY, yd->session_id);

	yahoo_packet_hash(pkt, 5, who);
	yahoo_packet_hash(pkt, 1, from?from:yd->user);
	yahoo_packet_hash(pkt, 14, " ");
	yahoo_packet_hash(pkt, 13, typ ? "1" : "0");
	yahoo_packet_hash(pkt, 49, "TYPING");

	yahoo_send_packet(yid, pkt, 0);

	yahoo_packet_free(pkt);
}

void yahoo_set_away(int id, enum yahoo_status state, const char *msg, int away)
{
	struct yahoo_input_data *yid = find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt = NULL;
	int old_status;
	char s[4];

	if(!yid)
		return;

	yd = yid->yd;

	old_status = yd->current_status;

	if (msg && strncmp(msg,"Invisible",9)) {
		yd->current_status = YAHOO_STATUS_CUSTOM;
	} else {
		yd->current_status = state;
	}

	/* Thank you libpurple :) */
	if (yd->current_status == YAHOO_STATUS_INVISIBLE) {
		pkt = yahoo_packet_new(YAHOO_SERVICE_Y6_VISIBILITY, YAHOO_STATUS_AVAILABLE, 0);
		yahoo_packet_hash(pkt, 13, "2");
		yahoo_send_packet(yid, pkt, 0);
		yahoo_packet_free(pkt);

		return;
	}

	pkt = yahoo_packet_new(YAHOO_SERVICE_Y6_STATUS_UPDATE, yd->current_status, yd->session_id);
	snprintf(s, sizeof(s), "%d", yd->current_status);
	yahoo_packet_hash(pkt, 10, s);
	 
	if (yd->current_status == YAHOO_STATUS_CUSTOM) {
		yahoo_packet_hash(pkt, 19, msg);
	} else {
		yahoo_packet_hash(pkt, 19, "");
	}
	
	yahoo_packet_hash(pkt, 47, (away == 2)? "2": (away) ?"1":"0");

	yahoo_send_packet(yid, pkt, 0);
	yahoo_packet_free(pkt);

	if(old_status == YAHOO_STATUS_INVISIBLE) {
		pkt = yahoo_packet_new(YAHOO_SERVICE_Y6_VISIBILITY, YAHOO_STATUS_AVAILABLE, 0);
		yahoo_packet_hash(pkt, 13, "1");
		yahoo_send_packet(yid, pkt, 0);
		yahoo_packet_free(pkt);
	}
}

void yahoo_logoff(int id)
{
	struct yahoo_input_data *yid = find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt = NULL;

	if(!yid)
		return;
	yd = yid->yd;

	LOG(("yahoo_logoff: current status: %d", yd->current_status));

	if(yd->current_status != -1 && 0) {
		/* Meh. Don't send this. The event handlers are not going to
		   get to do this so it'll just leak memory. And the TCP
		   connection reset will hopefully be clear enough. */
		pkt = yahoo_packet_new(YAHOO_SERVICE_LOGOFF, YAHOO_STATUS_AVAILABLE, yd->session_id);
		yd->current_status = -1;

		if (pkt) {
			yahoo_send_packet(yid, pkt, 0);
			yahoo_packet_free(pkt);
		}
	}

	do {
		yahoo_input_close(yid);
	} while((yid = find_input_by_id(id)));
}

void yahoo_get_list(int id)
{
	struct yahoo_input_data *yid = find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt = NULL;

	if(!yid)
		return;
	yd = yid->yd;

	pkt = yahoo_packet_new(YAHOO_SERVICE_LIST, YAHOO_STATUS_AVAILABLE, yd->session_id);
	yahoo_packet_hash(pkt, 1, yd->user);
	if (pkt) {
		yahoo_send_packet(yid, pkt, 0);
		yahoo_packet_free(pkt);
	}
}

static void _yahoo_http_connected(int id, int fd, int error, void *data)
{
	struct yahoo_input_data *yid = data;
	if(fd <= 0) {
		inputs = y_list_remove(inputs, yid);
		FREE(yid);
		return;
	}

	yid->fd = fd;
	yid->read_tag=YAHOO_CALLBACK(ext_yahoo_add_handler)(yid->yd->client_id, fd, YAHOO_INPUT_READ, yid);
}

void yahoo_get_yab(int id)
{
	struct yahoo_data *yd = find_conn_by_id(id);
	struct yahoo_input_data *yid;
	char url[1024];
	char buff[1024];

	if(!yd)
		return;

	yid = y_new0(struct yahoo_input_data, 1);
	yid->yd = yd;
	yid->type = YAHOO_CONNECTION_YAB;

	snprintf(url, 1024, "http://insider.msg.yahoo.com/ycontent/?ab2=0");

	snprintf(buff, sizeof(buff), "Y=%s; T=%s",
			yd->cookie_y, yd->cookie_t);

	inputs = y_list_prepend(inputs, yid);

	yahoo_http_get(yid->yd->client_id, url, buff, 
			_yahoo_http_connected, yid);
}

void yahoo_set_yab(int id, struct yab * yab)
{
	struct yahoo_data *yd = find_conn_by_id(id);
	struct yahoo_input_data *yid;
	char url[1024];
	char buff[1024];
	char *temp;
	int size = sizeof(url)-1;

	if(!yd)
		return;

	yid = y_new0(struct yahoo_input_data, 1);
	yid->type = YAHOO_CONNECTION_YAB;
	yid->yd = yd;

	strncpy(url, "http://insider.msg.yahoo.com/ycontent/?addab2=0", size);

	if(yab->dbid) {
		/* change existing yab */
		char tmp[32];
		strncat(url, "&ee=1&ow=1&id=", size - strlen(url));
		snprintf(tmp, sizeof(tmp), "%d", yab->dbid);
		strncat(url, tmp, size - strlen(url));
	}

	if(yab->fname) {
		strncat(url, "&fn=", size - strlen(url));
		temp = yahoo_urlencode(yab->fname);
		strncat(url, temp, size - strlen(url));
		free(temp);
	}
	if(yab->lname) {
		strncat(url, "&ln=", size - strlen(url));
		temp = yahoo_urlencode(yab->lname);
		strncat(url, temp, size - strlen(url));
		free(temp);
	}
	strncat(url, "&yid=", size - strlen(url));
	temp = yahoo_urlencode(yab->id);
	strncat(url, temp, size - strlen(url));
	free(temp);
	if(yab->nname) {
		strncat(url, "&nn=", size - strlen(url));
		temp = yahoo_urlencode(yab->nname);
		strncat(url, temp, size - strlen(url));
		free(temp);
	}
	if(yab->email) {
		strncat(url, "&e=", size - strlen(url));
		temp = yahoo_urlencode(yab->email);
		strncat(url, temp, size - strlen(url));
		free(temp);
	}
	if(yab->hphone) {
		strncat(url, "&hp=", size - strlen(url));
		temp = yahoo_urlencode(yab->hphone);
		strncat(url, temp, size - strlen(url));
		free(temp);
	}
	if(yab->wphone) {
		strncat(url, "&wp=", size - strlen(url));
		temp = yahoo_urlencode(yab->wphone);
		strncat(url, temp, size - strlen(url));
		free(temp);
	}
	if(yab->mphone) {
		strncat(url, "&mp=", size - strlen(url));
		temp = yahoo_urlencode(yab->mphone);
		strncat(url, temp, size - strlen(url));
		free(temp);
	}
	strncat(url, "&pp=0", size - strlen(url));

	snprintf(buff, sizeof(buff), "Y=%s; T=%s",
			yd->cookie_y, yd->cookie_t);

	inputs = y_list_prepend(inputs, yid);

	yahoo_http_get(yid->yd->client_id, url, buff, 
			_yahoo_http_connected, yid);
}

void yahoo_set_identity_status(int id, const char * identity, int active)
{
	struct yahoo_input_data *yid = find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt = NULL;

	if(!yid)
		return;
	yd = yid->yd;

	pkt = yahoo_packet_new(active?YAHOO_SERVICE_IDACT:YAHOO_SERVICE_IDDEACT,
			YAHOO_STATUS_AVAILABLE, yd->session_id);
	yahoo_packet_hash(pkt, 3, identity);
	if (pkt) {
		yahoo_send_packet(yid, pkt, 0);
		yahoo_packet_free(pkt);
	}
}

void yahoo_refresh(int id)
{
	struct yahoo_input_data *yid = find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt = NULL;

	if(!yid)
		return;
	yd = yid->yd;

	pkt = yahoo_packet_new(YAHOO_SERVICE_USERSTAT, YAHOO_STATUS_AVAILABLE, yd->session_id);
	if (pkt) {
		yahoo_send_packet(yid, pkt, 0);
		yahoo_packet_free(pkt);
	}
}

void yahoo_keepalive(int id)
{
	struct yahoo_input_data *yid = find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt=NULL;
	if(!yid)
		return;
	yd = yid->yd;

	pkt = yahoo_packet_new(YAHOO_SERVICE_PING, YAHOO_STATUS_AVAILABLE, yd->session_id);
	yahoo_send_packet(yid, pkt, 0);
	yahoo_packet_free(pkt);
}

void yahoo_chat_keepalive (int id)
{
	struct yahoo_input_data *yid = find_input_by_id_and_type (id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt = NULL;

	if (!yid)
	    return;

	yd = yid->yd;

	pkt = yahoo_packet_new (YAHOO_SERVICE_CHATPING, YAHOO_STATUS_AVAILABLE, yd->session_id);
	yahoo_send_packet (yid, pkt, 0);
	yahoo_packet_free (pkt);
}

void yahoo_add_buddy(int id, const char *who, const char *group, const char *msg)
{
	struct yahoo_input_data *yid = find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt;

	if(!yid)
		return;
	yd = yid->yd;

	if (!yd->logged_in)
		return;

	pkt = yahoo_packet_new(YAHOO_SERVICE_ADDBUDDY, YPACKET_STATUS_DEFAULT, yd->session_id);

	if (msg != NULL) /* add message/request "it's me add me" */
		yahoo_packet_hash(pkt, 14, msg);
	else
		yahoo_packet_hash(pkt,14,"");

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
	struct yahoo_input_data *yid = find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt = NULL;

	if(!yid)
		return;
	yd = yid->yd;

	pkt = yahoo_packet_new(YAHOO_SERVICE_REMBUDDY, YAHOO_STATUS_AVAILABLE, yd->session_id);

	yahoo_packet_hash(pkt, 1, yd->user);
	yahoo_packet_hash(pkt, 7, who);
	yahoo_packet_hash(pkt, 65, group);
	yahoo_send_packet(yid, pkt, 0);
	yahoo_packet_free(pkt);
}

void yahoo_accept_buddy_ymsg13(int id,const char* me,const char* who){
	struct yahoo_input_data *yid = find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;

	if(!yid)
		return;
	yd = yid->yd;

	struct yahoo_packet* pkt=NULL;
	pkt= yahoo_packet_new(YAHOO_SERVICE_CONTACT_YMSG13,YAHOO_STATUS_AVAILABLE,0);

	yahoo_packet_hash(pkt,1,me ?: yd->user);	
	yahoo_packet_hash(pkt,5,who);
	yahoo_packet_hash(pkt,13,"1");
	yahoo_packet_hash(pkt,334,"0");
	yahoo_send_packet(yid, pkt, 0);
	yahoo_packet_free(pkt);
}

void yahoo_reject_buddy_ymsg13(int id,const char* me,const char* who,const char* msg){
	struct yahoo_input_data *yid = find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;

	if(!yid)
		return;
	yd = yid->yd;

	struct yahoo_packet* pkt=NULL;
	pkt= yahoo_packet_new(YAHOO_SERVICE_CONTACT_YMSG13,YAHOO_STATUS_AVAILABLE,0);

	yahoo_packet_hash(pkt,1,me ?: yd->user);	
	yahoo_packet_hash(pkt,5,who);
//	yahoo_packet_hash(pkt,241,YAHOO_PROTO_VER);
	yahoo_packet_hash(pkt,13,"2");
	yahoo_packet_hash(pkt,334,"0");
	yahoo_packet_hash(pkt,97,"1");
	yahoo_packet_hash(pkt,14,msg?:"");

	yahoo_send_packet(yid, pkt, 0);
	yahoo_packet_free(pkt);

}

void yahoo_reject_buddy(int id, const char *who, const char *msg)
{
	struct yahoo_input_data *yid = find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt;

	if(!yid)
		return;
	yd = yid->yd;

	if (!yd->logged_in)
		return;

	pkt = yahoo_packet_new(YAHOO_SERVICE_REJECTCONTACT, YAHOO_STATUS_AVAILABLE, yd->session_id);
	yahoo_packet_hash(pkt, 1, yd->user);
	yahoo_packet_hash(pkt, 7, who);
	yahoo_packet_hash(pkt, 14, msg);
	yahoo_send_packet(yid, pkt, 0);
	yahoo_packet_free(pkt);
}

void yahoo_ignore_buddy(int id, const char *who, int unignore)
{
	struct yahoo_input_data *yid = find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt;

	if(!yid)
		return;
	yd = yid->yd;

	if (!yd->logged_in)
		return;

	pkt = yahoo_packet_new(YAHOO_SERVICE_IGNORECONTACT, YAHOO_STATUS_AVAILABLE, yd->session_id);
	yahoo_packet_hash(pkt, 1, yd->user);
	yahoo_packet_hash(pkt, 7, who);
	yahoo_packet_hash(pkt, 13, unignore?"2":"1");
	yahoo_send_packet(yid, pkt, 0);
	yahoo_packet_free(pkt);
}

void yahoo_stealth_buddy(int id, const char *who, int unstealth)
{
	struct yahoo_input_data *yid = find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt;

	if(!yid)
		return;
	yd = yid->yd;

	if (!yd->logged_in)
		return;

	pkt = yahoo_packet_new(YAHOO_SERVICE_STEALTH, YAHOO_STATUS_AVAILABLE, yd->session_id);
	yahoo_packet_hash(pkt, 1, yd->user);
	yahoo_packet_hash(pkt, 7, who);
	yahoo_packet_hash(pkt, 31, unstealth?"2":"1");
	yahoo_packet_hash(pkt, 13, "2");
	yahoo_send_packet(yid, pkt, 0);
	yahoo_packet_free(pkt);
}

void yahoo_change_buddy_group(int id, const char *who, const char *old_group, const char *new_group)
{
	struct yahoo_input_data *yid = find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt = NULL;

	if(!yid)
		return;
	yd = yid->yd;

	pkt = yahoo_packet_new(YAHOO_SERVICE_ADDBUDDY, YAHOO_STATUS_AVAILABLE, yd->session_id);
	yahoo_packet_hash(pkt, 1, yd->user);
	yahoo_packet_hash(pkt, 7, who);
	yahoo_packet_hash(pkt, 65, new_group);
	yahoo_packet_hash(pkt, 14, " ");

	yahoo_send_packet(yid, pkt, 0);
	yahoo_packet_free(pkt);

	pkt = yahoo_packet_new(YAHOO_SERVICE_REMBUDDY, YAHOO_STATUS_AVAILABLE, yd->session_id);
	yahoo_packet_hash(pkt, 1, yd->user);
	yahoo_packet_hash(pkt, 7, who);
	yahoo_packet_hash(pkt, 65, old_group);
	yahoo_send_packet(yid, pkt, 0);
	yahoo_packet_free(pkt);
}

void yahoo_group_rename(int id, const char *old_group, const char *new_group)
{
	struct yahoo_input_data *yid = find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt = NULL;

	if(!yid)
		return;
	yd = yid->yd;

	pkt = yahoo_packet_new(YAHOO_SERVICE_GROUPRENAME, YAHOO_STATUS_AVAILABLE, yd->session_id);
	yahoo_packet_hash(pkt, 1, yd->user);
	yahoo_packet_hash(pkt, 65, old_group);
	yahoo_packet_hash(pkt, 67, new_group);

	yahoo_send_packet(yid, pkt, 0);
	yahoo_packet_free(pkt);
}

void yahoo_conference_addinvite(int id, const char * from, const char *who, const char *room, const YList * members, const char *msg)
{
	struct yahoo_input_data *yid = find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt;
		
	if(!yid)
		return;
	yd = yid->yd;

	pkt = yahoo_packet_new(YAHOO_SERVICE_CONFADDINVITE, YAHOO_STATUS_AVAILABLE, yd->session_id);

	yahoo_packet_hash(pkt, 1, (from?from:yd->user));
	yahoo_packet_hash(pkt, 51, who);
	yahoo_packet_hash(pkt, 57, room);
	yahoo_packet_hash(pkt, 58, msg);
	yahoo_packet_hash(pkt, 13, "0");
	for(; members; members = members->next) {
		yahoo_packet_hash(pkt, 52, (char *)members->data);
		yahoo_packet_hash(pkt, 53, (char *)members->data);
	}
	/* 52, 53 -> other members? */

	yahoo_send_packet(yid, pkt, 0);

	yahoo_packet_free(pkt);
}

void yahoo_conference_invite(int id, const char * from, YList *who, const char *room, const char *msg)
{
	struct yahoo_input_data *yid = find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt;
		
	if(!yid)
		return;
	yd = yid->yd;

	pkt = yahoo_packet_new(YAHOO_SERVICE_CONFINVITE, YAHOO_STATUS_AVAILABLE, yd->session_id);

	yahoo_packet_hash(pkt, 1, (from?from:yd->user));
	yahoo_packet_hash(pkt, 50, yd->user);
	for(; who; who = who->next) {
		yahoo_packet_hash(pkt, 52, (char *)who->data);
	}
	yahoo_packet_hash(pkt, 57, room);
	yahoo_packet_hash(pkt, 58, msg);
	yahoo_packet_hash(pkt, 13, "0");

	yahoo_send_packet(yid, pkt, 0);

	yahoo_packet_free(pkt);
}

void yahoo_conference_logon(int id, const char *from, YList *who, const char *room)
{
	struct yahoo_input_data *yid = find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt;
		
	if(!yid)
		return;
	yd = yid->yd;

	pkt = yahoo_packet_new(YAHOO_SERVICE_CONFLOGON, YAHOO_STATUS_AVAILABLE, yd->session_id);

	yahoo_packet_hash(pkt, 1, (from?from:yd->user));
	for(; who; who = who->next) {
		yahoo_packet_hash(pkt, 3, (char *)who->data);
	}
	yahoo_packet_hash(pkt, 57, room);

	yahoo_send_packet(yid, pkt, 0);

	yahoo_packet_free(pkt);
}

void yahoo_conference_decline(int id, const char * from, YList *who, const char *room, const char *msg)
{
	struct yahoo_input_data *yid = find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt;
		
	if(!yid)
		return;
	yd = yid->yd;

	pkt = yahoo_packet_new(YAHOO_SERVICE_CONFDECLINE, YAHOO_STATUS_AVAILABLE, yd->session_id);

	yahoo_packet_hash(pkt, 1, (from?from:yd->user));
	for(; who; who = who->next) {
		yahoo_packet_hash(pkt, 3, (char *)who->data);
	}
	yahoo_packet_hash(pkt, 57, room);
	yahoo_packet_hash(pkt, 14, msg);

	yahoo_send_packet(yid, pkt, 0);

	yahoo_packet_free(pkt);
}

void yahoo_conference_logoff(int id, const char * from, YList *who, const char *room)
{
	struct yahoo_input_data *yid = find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt;
		
	if(!yid)
		return;
	yd = yid->yd;

	pkt = yahoo_packet_new(YAHOO_SERVICE_CONFLOGOFF, YAHOO_STATUS_AVAILABLE, yd->session_id);

	yahoo_packet_hash(pkt, 1, (from?from:yd->user));
	for(; who; who = who->next) {
		yahoo_packet_hash(pkt, 3, (char *)who->data);
	}
	yahoo_packet_hash(pkt, 57, room);

	yahoo_send_packet(yid, pkt, 0);

	yahoo_packet_free(pkt);
}

void yahoo_conference_message(int id, const char * from, YList *who, const char *room, const char *msg, int utf8)
{
	struct yahoo_input_data *yid = find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt;
		
	if(!yid)
		return;
	yd = yid->yd;

	pkt = yahoo_packet_new(YAHOO_SERVICE_CONFMSG, YAHOO_STATUS_AVAILABLE, yd->session_id);

	yahoo_packet_hash(pkt, 1, (from?from:yd->user));
	for(; who; who = who->next) {
		yahoo_packet_hash(pkt, 53, (char *)who->data);
	}
	yahoo_packet_hash(pkt, 57, room);
	yahoo_packet_hash(pkt, 14, msg);

	if(utf8)
		yahoo_packet_hash(pkt, 97, "1");

	yahoo_send_packet(yid, pkt, 0);

	yahoo_packet_free(pkt);
}

void yahoo_get_chatrooms(int id, int chatroomid)
{
	struct yahoo_data *yd = find_conn_by_id(id);
	struct yahoo_input_data *yid;
	char url[1024];
	char buff[1024];

	if(!yd)
		return;

	yid = y_new0(struct yahoo_input_data, 1);
	yid->yd = yd;
	yid->type = YAHOO_CONNECTION_CHATCAT;

	if (chatroomid == 0) {
		snprintf(url, 1024, "http://insider.msg.yahoo.com/ycontent/?chatcat=0");
	} else {
		snprintf(url, 1024, "http://insider.msg.yahoo.com/ycontent/?chatroom_%d=0",chatroomid);
	}

	snprintf(buff, sizeof(buff), "Y=%s; T=%s", yd->cookie_y, yd->cookie_t);

	inputs = y_list_prepend(inputs, yid);

	yahoo_http_get(yid->yd->client_id, url, buff, _yahoo_http_connected, yid);
}

void yahoo_chat_logon(int id, const char *from, const char *room, const char *roomid)
{
	struct yahoo_input_data *yid = find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt;
		
	if(!yid)
		return;

	yd = yid->yd;

	pkt = yahoo_packet_new(YAHOO_SERVICE_CHATONLINE, YAHOO_STATUS_AVAILABLE, yd->session_id);

	yahoo_packet_hash(pkt, 1, (from?from:yd->user));
	yahoo_packet_hash(pkt, 109, yd->user);
	yahoo_packet_hash(pkt, 6, "abcde");

	yahoo_send_packet(yid, pkt, 0);

	yahoo_packet_free(pkt);

	pkt = yahoo_packet_new(YAHOO_SERVICE_CHATJOIN, YAHOO_STATUS_AVAILABLE, yd->session_id);

	yahoo_packet_hash(pkt, 1, (from?from:yd->user));
	yahoo_packet_hash(pkt, 104, room);
	yahoo_packet_hash(pkt, 129, roomid);
	yahoo_packet_hash(pkt, 62, "2"); /* ??? */

	yahoo_send_packet(yid, pkt, 0);

	yahoo_packet_free(pkt);
}


void  yahoo_chat_message(int id, const char *from, const char *room, const char *msg, const int msgtype, const int utf8)
{
	struct yahoo_input_data *yid = find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt;
	char buf[2];
		
	if(!yid)
		return;

	yd = yid->yd;

	pkt = yahoo_packet_new(YAHOO_SERVICE_COMMENT, YAHOO_STATUS_AVAILABLE, yd->session_id);

	yahoo_packet_hash(pkt, 1, (from?from:yd->user));
	yahoo_packet_hash(pkt, 104, room);
	yahoo_packet_hash(pkt, 117, msg);
	
	snprintf(buf, sizeof(buf), "%d", msgtype);
	yahoo_packet_hash(pkt, 124, buf);

	if(utf8)
		yahoo_packet_hash(pkt, 97, "1");

	yahoo_send_packet(yid, pkt, 0);

	yahoo_packet_free(pkt);
}


void yahoo_chat_logoff(int id, const char *from)
{
	struct yahoo_input_data *yid = find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt;
		
	if(!yid)
		return;

	yd = yid->yd;

	pkt = yahoo_packet_new(YAHOO_SERVICE_CHATLOGOUT, YAHOO_STATUS_AVAILABLE, yd->session_id);

	yahoo_packet_hash(pkt, 1, (from?from:yd->user));

	yahoo_send_packet(yid, pkt, 0);

	yahoo_packet_free(pkt);
}

void yahoo_buddyicon_request(int id, const char *who)
{
	struct yahoo_input_data *yid = find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt;

	if( !yid )
		return;

	yd = yid->yd;
	
	pkt = yahoo_packet_new(YAHOO_SERVICE_PICTURE, YAHOO_STATUS_AVAILABLE, 0);
	yahoo_packet_hash(pkt, 4, yd->user);
	yahoo_packet_hash(pkt, 5, who);
	yahoo_packet_hash(pkt, 13, "1");
	yahoo_send_packet(yid, pkt, 0);

	yahoo_packet_free(pkt);
}

void yahoo_send_picture_info(int id, const char *who, const char *url, int checksum)
{
	struct yahoo_input_data *yid = find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt;
	char checksum_str[10];

	if( !yid )
		return;

	yd = yid->yd;

	snprintf(checksum_str, sizeof(checksum_str), "%d", checksum);

	pkt = yahoo_packet_new(YAHOO_SERVICE_PICTURE, YAHOO_STATUS_AVAILABLE, 0);
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
	struct yahoo_input_data *yid = find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt;
	char type_str[10];

	if( !yid )
		return;

	yd = yid->yd;

	snprintf(type_str, sizeof(type_str), "%d", type);

	pkt = yahoo_packet_new(YAHOO_SERVICE_PICTURE_UPDATE, YAHOO_STATUS_AVAILABLE, 0);
	yahoo_packet_hash(pkt, 1, yd->user);
	yahoo_packet_hash(pkt, 5, who);
	yahoo_packet_hash(pkt, 206, type_str);
	yahoo_send_packet(yid, pkt, 0);

	yahoo_packet_free(pkt);
}

void yahoo_send_picture_checksum(int id, const char *who, int checksum)
{
	struct yahoo_input_data *yid = find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt;
	char checksum_str[10];

	if( !yid )
		return;

	yd = yid->yd;
	
	snprintf(checksum_str, sizeof(checksum_str), "%d", checksum);

	pkt = yahoo_packet_new(YAHOO_SERVICE_PICTURE_CHECKSUM, YAHOO_STATUS_AVAILABLE, 0);
	yahoo_packet_hash(pkt, 1, yd->user);
	if( who != 0 )
		yahoo_packet_hash(pkt, 5, who);
	yahoo_packet_hash(pkt, 192, checksum_str);
	yahoo_packet_hash(pkt, 212, "1");
	yahoo_send_packet(yid, pkt, 0);

	yahoo_packet_free(pkt);
}

void yahoo_webcam_close_feed(int id, const char *who)
{
	struct yahoo_input_data *yid = find_input_by_id_and_webcam_user(id, who);

	if(yid)
		yahoo_input_close(yid);
}

void yahoo_webcam_get_feed(int id, const char *who)
{
	struct yahoo_input_data *yid = find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_data *yd;
	struct yahoo_packet *pkt;
		
	if(!yid)
		return;

	/* 
	 * add the user to the queue.  this is a dirty hack, since
	 * the yahoo server doesn't tell us who's key it's returning,
	 * we have to just hope that it sends back keys in the same 
	 * order that we request them.
	 * The queue is popped in yahoo_process_webcam_key
	 */
	webcam_queue = y_list_append(webcam_queue, who?strdup(who):NULL);

	yd = yid->yd;

	pkt = yahoo_packet_new(YAHOO_SERVICE_WEBCAM, YAHOO_STATUS_AVAILABLE, yd->session_id);

	yahoo_packet_hash(pkt, 1, yd->user);
	if (who != NULL)
		yahoo_packet_hash(pkt, 5, who);
	yahoo_send_packet(yid, pkt, 0);

	yahoo_packet_free(pkt);
}

void yahoo_webcam_send_image(int id, unsigned char *image, unsigned int length, unsigned int timestamp)
{
	struct yahoo_input_data *yid = find_input_by_id_and_type(id, YAHOO_CONNECTION_WEBCAM);
	unsigned char *packet;
	unsigned char header_len = 13;
	unsigned int pos = 0;

	if (!yid)
		return;

	packet = y_new0(unsigned char, header_len);

	packet[pos++] = header_len;
	packet[pos++] = 0;
	packet[pos++] = 5; /* version byte?? */
	packet[pos++] = 0;
	pos += yahoo_put32(packet + pos, length);
	packet[pos++] = 2; /* packet type, image */
	pos += yahoo_put32(packet + pos, timestamp);
	yahoo_add_to_send_queue(yid, packet, header_len);
	FREE(packet);

	if (length)
		yahoo_add_to_send_queue(yid, image, length);
}

void yahoo_webcam_accept_viewer(int id, const char* who, int accept)
{
	struct yahoo_input_data *yid = find_input_by_id_and_type(id, YAHOO_CONNECTION_WEBCAM);
	char *packet = NULL;
	char *data = NULL;
	unsigned char header_len = 13;
	unsigned int pos = 0;
	unsigned int len = 0;

	if (!yid)
		return;

	data = strdup("u=");
	data = y_string_append(data, (char*)who);
	data = y_string_append(data, "\r\n");
	len = strlen(data);

	packet = y_new0(char, header_len + len);
	packet[pos++] = header_len;
	packet[pos++] = 0;
	packet[pos++] = 5; /* version byte?? */
	packet[pos++] = 0;
	pos += yahoo_put32(packet + pos, len);
	packet[pos++] = 0; /* packet type */
	pos += yahoo_put32(packet + pos, accept);
	memcpy(packet + pos, data, len);
	FREE(data);
	yahoo_add_to_send_queue(yid, packet, header_len + len);
	FREE(packet);
}

void yahoo_webcam_invite(int id, const char *who)
{
	struct yahoo_input_data *yid = find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_packet *pkt;
		
	if(!yid)
		return;

	pkt = yahoo_packet_new(YAHOO_SERVICE_NOTIFY, YAHOO_STATUS_NOTIFY, yid->yd->session_id);

	yahoo_packet_hash(pkt, 49, "WEBCAMINVITE");
	yahoo_packet_hash(pkt, 14, " ");
	yahoo_packet_hash(pkt, 13, "0");
	yahoo_packet_hash(pkt, 1, yid->yd->user);
	yahoo_packet_hash(pkt, 5, who);
	yahoo_send_packet(yid, pkt, 0);

	yahoo_packet_free(pkt);
}

static void yahoo_search_internal(int id, int t, const char *text, int g, int ar, int photo, int yahoo_only, int startpos, int total)
{
	struct yahoo_data *yd = find_conn_by_id(id);
	struct yahoo_input_data *yid;
	char url[1024];
	char buff[1024];
	char *ctext, *p;

	if(!yd)
		return;

	yid = y_new0(struct yahoo_input_data, 1);
	yid->yd = yd;
	yid->type = YAHOO_CONNECTION_SEARCH;

	/*
	age range
	.ar=1 - 13-18, 2 - 18-25, 3 - 25-35, 4 - 35-50, 5 - 50-70, 6 - 70+
	*/

	snprintf(buff, sizeof(buff), "&.sq=%%20&.tt=%d&.ss=%d", total, startpos);

	ctext = strdup(text);
	while((p = strchr(ctext, ' ')))
		*p = '+';

	snprintf(url, 1024, "http://members.yahoo.com/interests?.oc=m&.kw=%s&.sb=%d&.g=%d&.ar=0%s%s%s",
			ctext, t, g, photo ? "&.p=y" : "", yahoo_only ? "&.pg=y" : "",
			startpos ? buff : "");

	FREE(ctext);

	snprintf(buff, sizeof(buff), "Y=%s; T=%s", yd->cookie_y, yd->cookie_t);

	inputs = y_list_prepend(inputs, yid);
	yahoo_http_get(yid->yd->client_id, url, buff, _yahoo_http_connected, yid);
}

void yahoo_search(int id, enum yahoo_search_type t, const char *text, enum yahoo_search_gender g, enum yahoo_search_agerange ar, 
		int photo, int yahoo_only)
{
	struct yahoo_input_data *yid = find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_search_state *yss;

	if(!yid)
		return;

	if(!yid->ys)
		yid->ys = y_new0(struct yahoo_search_state, 1);

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
	struct yahoo_input_data *yid = find_input_by_id_and_type(id, YAHOO_CONNECTION_PAGER);
	struct yahoo_search_state *yss;

	if(!yid || !yid->ys)
		return;

	yss = yid->ys;

	if(start == -1)
		start = yss->lsearch_nstart + yss->lsearch_nfound;

	yahoo_search_internal(id, yss->lsearch_type, yss->lsearch_text, 
			yss->lsearch_gender, yss->lsearch_agerange, 
			yss->lsearch_photo, yss->lsearch_yahoo_only, 
			start, yss->lsearch_ntotal);
}

struct send_file_data {
	struct yahoo_packet *pkt;
	yahoo_get_fd_callback callback;
	void *user_data;
};

static void _yahoo_send_picture_connected(int id, int fd, int error, void *data)
{
	struct yahoo_input_data *yid = find_input_by_id_and_type(id, YAHOO_CONNECTION_FT);
	struct send_file_data *sfd = data;
	struct yahoo_packet *pkt = sfd->pkt;
	unsigned char buff[1024];

	if(fd <= 0) {
		sfd->callback(id, fd, error, sfd->user_data);
		FREE(sfd);
		yahoo_packet_free(pkt);
		inputs = y_list_remove(inputs, yid);
		FREE(yid);
		return;
	}

	yid->fd = fd;
	yahoo_send_packet(yid, pkt, 8);
	yahoo_packet_free(pkt);

	snprintf((char *)buff, sizeof(buff), "29");
	buff[2] = 0xc0;
	buff[3] = 0x80;
	
	write(yid->fd, buff, 4);

	/*	YAHOO_CALLBACK(ext_yahoo_add_handler)(nyd->fd, YAHOO_INPUT_READ); */

	sfd->callback(id, fd, error, sfd->user_data);
	FREE(sfd);
	inputs = y_list_remove(inputs, yid);
	/*
	while(yahoo_tcp_readline(buff, sizeof(buff), nyd->fd) > 0) {
	if(!strcmp(buff, ""))
	break;
}

	*/
	yahoo_input_close(yid);
}

void yahoo_send_picture(int id, const char *name, unsigned long size,
							yahoo_get_fd_callback callback, void *data)
{
	struct yahoo_data *yd = find_conn_by_id(id);
	struct yahoo_input_data *yid;
	struct yahoo_server_settings *yss;
	struct yahoo_packet *pkt = NULL;
	char size_str[10];
	char expire_str[10];
	long content_length=0;
	unsigned char buff[1024];
	char url[255];
	struct send_file_data *sfd;

	if(!yd)
		return;

	yss = yd->server_settings;

	yid = y_new0(struct yahoo_input_data, 1);
	yid->yd = yd;
	yid->type = YAHOO_CONNECTION_FT;

	pkt = yahoo_packet_new(YAHOO_SERVICE_PICTURE_UPLOAD, YAHOO_STATUS_AVAILABLE, yd->session_id);

	snprintf(size_str, sizeof(size_str), "%ld", size);
	snprintf(expire_str, sizeof(expire_str), "%ld", (long)604800);

	yahoo_packet_hash(pkt, 0, yd->user);
	yahoo_packet_hash(pkt, 1, yd->user);
	yahoo_packet_hash(pkt, 14, "");
	yahoo_packet_hash(pkt, 27, name);
	yahoo_packet_hash(pkt, 28, size_str);
	yahoo_packet_hash(pkt, 38, expire_str);
	

	content_length = YAHOO_PACKET_HDRLEN + yahoo_packet_length(pkt);

	snprintf(url, sizeof(url), "http://%s:%d/notifyft",
				yss->filetransfer_host, yss->filetransfer_port);
	snprintf((char *)buff, sizeof(buff), "Y=%s; T=%s",
				 yd->cookie_y, yd->cookie_t);
	inputs = y_list_prepend(inputs, yid);

	sfd = y_new0(struct send_file_data, 1);
	sfd->pkt = pkt;
	sfd->callback = callback;
	sfd->user_data = data;
	yahoo_http_post(yid->yd->client_id, url, (char *)buff, content_length+4+size,
						_yahoo_send_picture_connected, sfd);
}

static void _yahoo_send_file_connected(int id, int fd, int error, void *data)
{
	struct yahoo_input_data *yid = find_input_by_id_and_type(id, YAHOO_CONNECTION_FT);
	struct send_file_data *sfd = data;
	struct yahoo_packet *pkt = sfd->pkt;
	unsigned char buff[1024];

	if(fd <= 0) {
		sfd->callback(id, fd, error, sfd->user_data);
		FREE(sfd);
		yahoo_packet_free(pkt);
		inputs = y_list_remove(inputs, yid);
		FREE(yid);
		return;
	}

	yid->fd = fd;
	yahoo_send_packet(yid, pkt, 8);
	yahoo_packet_free(pkt);

	snprintf((char *)buff, sizeof(buff), "29");
	buff[2] = 0xc0;
	buff[3] = 0x80;
	
	write(yid->fd, buff, 4);

/*	YAHOO_CALLBACK(ext_yahoo_add_handler)(nyd->fd, YAHOO_INPUT_READ); */

	sfd->callback(id, fd, error, sfd->user_data);
	FREE(sfd);
	inputs = y_list_remove(inputs, yid);
	/*
	while(yahoo_tcp_readline(buff, sizeof(buff), nyd->fd) > 0) {
		if(!strcmp(buff, ""))
			break;
	}

	*/
	yahoo_input_close(yid);
}

void yahoo_send_file(int id, const char *who, const char *msg, 
		const char *name, unsigned long size, 
		yahoo_get_fd_callback callback, void *data)
{
	struct yahoo_data *yd = find_conn_by_id(id);
	struct yahoo_input_data *yid;
	struct yahoo_server_settings *yss;
	struct yahoo_packet *pkt = NULL;
	char size_str[10];
	long content_length=0;
	unsigned char buff[1024];
	char url[255];
	struct send_file_data *sfd;

	if(!yd)
		return;

	yss = yd->server_settings;

	yid = y_new0(struct yahoo_input_data, 1);
	yid->yd = yd;
	yid->type = YAHOO_CONNECTION_FT;

	pkt = yahoo_packet_new(YAHOO_SERVICE_FILETRANSFER, YAHOO_STATUS_AVAILABLE, yd->session_id);

	snprintf(size_str, sizeof(size_str), "%ld", size);

	yahoo_packet_hash(pkt, 0, yd->user);
	yahoo_packet_hash(pkt, 5, who);
	yahoo_packet_hash(pkt, 14, msg);
	yahoo_packet_hash(pkt, 27, name);
	yahoo_packet_hash(pkt, 28, size_str);

	content_length = YAHOO_PACKET_HDRLEN + yahoo_packet_length(pkt);

	snprintf(url, sizeof(url), "http://%s:%d/notifyft", 
			yss->filetransfer_host, yss->filetransfer_port);
	snprintf((char *)buff, sizeof(buff), "Y=%s; T=%s",
			yd->cookie_y, yd->cookie_t);
	inputs = y_list_prepend(inputs, yid);

	sfd = y_new0(struct send_file_data, 1);
	sfd->pkt = pkt;
	sfd->callback = callback;
	sfd->user_data = data;
	yahoo_http_post(yid->yd->client_id, url, (char *)buff, content_length+4+size,
			_yahoo_send_file_connected, sfd);
}


enum yahoo_status yahoo_current_status(int id)
{
	struct yahoo_data *yd = find_conn_by_id(id);
	if(!yd)
		return YAHOO_STATUS_OFFLINE;
	return yd->current_status;
}

const YList * yahoo_get_buddylist(int id)
{
	struct yahoo_data *yd = find_conn_by_id(id);
	if(!yd)
		return NULL;
	return yd->buddies;
}

const YList * yahoo_get_ignorelist(int id)
{
	struct yahoo_data *yd = find_conn_by_id(id);
	if(!yd)
		return NULL;
	return yd->ignore;
}

const YList * yahoo_get_identities(int id)
{
	struct yahoo_data *yd = find_conn_by_id(id);
	if(!yd)
		return NULL;
	return yd->identities;
}

const char * yahoo_get_cookie(int id, const char *which)
{
	struct yahoo_data *yd = find_conn_by_id(id);
	if(!yd)
		return NULL;
	if(!strncasecmp(which, "y", 1))
		return yd->cookie_y;
	if(!strncasecmp(which, "t", 1))
		return yd->cookie_t;
	if(!strncasecmp(which, "c", 1))
		return yd->cookie_c;
	if(!strncasecmp(which, "login", 5))
		return yd->login_cookie;
	return NULL;
}

void yahoo_get_url_handle(int id, const char *url, 
		yahoo_get_url_handle_callback callback, void *data)
{
	struct yahoo_data *yd = find_conn_by_id(id);
	if(!yd)
		return;

	yahoo_get_url_fd(id, url, yd, callback, data);
}

const char * yahoo_get_profile_url( void )
{
	return profile_url;
}

