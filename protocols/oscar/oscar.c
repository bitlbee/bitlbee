/*
 * gaim
 *
 * Some code copyright (C) 1998-1999, Mark Spencer <markster@marko.net>
 * libfaim code copyright 1998, 1999 Adam Fritzler <afritz@auk.cx>
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

#include "sock.h"
#include <errno.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sys/stat.h>
#include <glib.h>
#include "nogaim.h"
#include "bitlbee.h"
#include "proxy.h"

#include "aim.h"
#include "icq.h"
#include "bos.h"
#include "ssi.h"
#include "im.h"
#include "info.h"
#include "buddylist.h"
#include "chat.h"
#include "chatnav.h"

/* constants to identify proto_opts */
#define USEROPT_AUTH      0
#define USEROPT_AUTHPORT  1

#define UC_AOL		0x02
#define UC_ADMIN	0x04
#define UC_UNCONFIRMED	0x08
#define UC_NORMAL	0x10
#define UC_AB		0x20
#define UC_WIRELESS	0x40

#define AIMHASHDATA "http://gaim.sourceforge.net/aim_data.php3"

#define OSCAR_GROUP "Friends"

/* Don't know if support for UTF8 is really working. For now it's UTF16 here.
   static int gaim_caps = AIM_CAPS_UTF8; */

static int gaim_caps = AIM_CAPS_INTEROP | AIM_CAPS_ICHAT | AIM_CAPS_ICQSERVERRELAY | AIM_CAPS_CHAT;
static guint8 gaim_features[] = {0x01, 0x01, 0x01, 0x02};

struct oscar_data {
	aim_session_t *sess;
	aim_conn_t *conn;

	guint cnpa;
	guint paspa;

	GSList *create_rooms;

	gboolean conf;
	gboolean reqemail;
	gboolean setemail;
	char *email;
	gboolean setnick;
	char *newsn;
	gboolean chpass;
	char *oldp;
	char *newp;

	GSList *oscar_chats;

	gboolean killme;
	gboolean icq;
	GSList *evilhack;

	struct {
		guint maxbuddies; /* max users you can watch */
		guint maxwatchers; /* max users who can watch you */
		guint maxpermits; /* max users on permit list */
		guint maxdenies; /* max users on deny list */
		guint maxsiglen; /* max size (bytes) of profile */
		guint maxawaymsglen; /* max size (bytes) of posted away message */
	} rights;
};

struct create_room {
	char *name;
	int exchange;
};

struct chat_connection {
	char *name;
	char *show; /* AOL did something funny to us */
	guint16 exchange;
	guint16 instance;
	int fd; /* this is redundant since we have the conn below */
	aim_conn_t *conn;
	int inpa;
	int id;
	struct gaim_connection *gc; /* i hate this. */
	struct conversation *cnv; /* bah. */
	int maxlen;
	int maxvis;
};

struct ask_direct {
	struct gaim_connection *gc;
	char *sn;
	char ip[64];
	guint8 cookie[8];
};

struct icq_auth {
	struct gaim_connection *gc;
	guint32 uin;
};

static char *extract_name(const char *name) {
	char *tmp;
	int i, j;
	char *x = strchr(name, '-');
	if (!x) return NULL;
	x = strchr(++x, '-');
	if (!x) return NULL;
	tmp = g_strdup(++x);

	for (i = 0, j = 0; x[i]; i++) {
		char hex[3];
		if (x[i] != '%') {
			tmp[j++] = x[i];
			continue;
		}
		strncpy(hex, x + ++i, 2); hex[2] = 0;
		i++;
		tmp[j++] = (char)strtol(hex, NULL, 16);
	}

	tmp[j] = 0;
	return tmp;
}

static struct chat_connection *find_oscar_chat(struct gaim_connection *gc, int id) {
	GSList *g = ((struct oscar_data *)gc->proto_data)->oscar_chats;
	struct chat_connection *c = NULL;

	while (g) {
		c = (struct chat_connection *)g->data;
		if (c->id == id)
			break;
		g = g->next;
		c = NULL;
	}

	return c;
}


static struct chat_connection *find_oscar_chat_by_conn(struct gaim_connection *gc,
							aim_conn_t *conn) {
	GSList *g = ((struct oscar_data *)gc->proto_data)->oscar_chats;
	struct chat_connection *c = NULL;

	while (g) {
		c = (struct chat_connection *)g->data;
		if (c->conn == conn)
			break;
		g = g->next;
		c = NULL;
	}

	return c;
}

static int gaim_parse_auth_resp  (aim_session_t *, aim_frame_t *, ...);
static int gaim_parse_login      (aim_session_t *, aim_frame_t *, ...);
static int gaim_handle_redirect  (aim_session_t *, aim_frame_t *, ...);
static int gaim_parse_oncoming   (aim_session_t *, aim_frame_t *, ...);
static int gaim_parse_offgoing   (aim_session_t *, aim_frame_t *, ...);
static int gaim_parse_incoming_im(aim_session_t *, aim_frame_t *, ...);
static int gaim_parse_misses     (aim_session_t *, aim_frame_t *, ...);
static int gaim_parse_motd       (aim_session_t *, aim_frame_t *, ...);
static int gaim_chatnav_info     (aim_session_t *, aim_frame_t *, ...);
static int gaim_chat_join        (aim_session_t *, aim_frame_t *, ...);
static int gaim_chat_leave       (aim_session_t *, aim_frame_t *, ...);
static int gaim_chat_info_update (aim_session_t *, aim_frame_t *, ...);
static int gaim_chat_incoming_msg(aim_session_t *, aim_frame_t *, ...);
static int gaim_parse_ratechange (aim_session_t *, aim_frame_t *, ...);
static int gaim_bosrights        (aim_session_t *, aim_frame_t *, ...);
static int conninitdone_bos      (aim_session_t *, aim_frame_t *, ...);
static int conninitdone_admin    (aim_session_t *, aim_frame_t *, ...);
static int conninitdone_chat     (aim_session_t *, aim_frame_t *, ...);
static int conninitdone_chatnav  (aim_session_t *, aim_frame_t *, ...);
static int gaim_parse_msgerr     (aim_session_t *, aim_frame_t *, ...);
static int gaim_parse_locaterights(aim_session_t *, aim_frame_t *, ...);
static int gaim_parse_buddyrights(aim_session_t *, aim_frame_t *, ...);
static int gaim_parse_locerr     (aim_session_t *, aim_frame_t *, ...);
static int gaim_icbm_param_info  (aim_session_t *, aim_frame_t *, ...);
static int gaim_parse_genericerr (aim_session_t *, aim_frame_t *, ...);
static int gaim_memrequest       (aim_session_t *, aim_frame_t *, ...);
static int gaim_selfinfo         (aim_session_t *, aim_frame_t *, ...);
static int gaim_offlinemsg       (aim_session_t *, aim_frame_t *, ...);
static int gaim_offlinemsgdone   (aim_session_t *, aim_frame_t *, ...);
static int gaim_ssi_parserights  (aim_session_t *, aim_frame_t *, ...);
static int gaim_ssi_parselist    (aim_session_t *, aim_frame_t *, ...);
static int gaim_ssi_parseack     (aim_session_t *, aim_frame_t *, ...);
static int gaim_parsemtn         (aim_session_t *, aim_frame_t *, ...);
static int gaim_icqinfo          (aim_session_t *, aim_frame_t *, ...);
static int gaim_parseaiminfo     (aim_session_t *, aim_frame_t *, ...);

static char *msgerrreason[] = {
	"Invalid error",
	"Invalid SNAC",
	"Rate to host",
	"Rate to client",
	"Not logged in",
	"Service unavailable",
	"Service not defined",
	"Obsolete SNAC",
	"Not supported by host",
	"Not supported by client",
	"Refused by client",
	"Reply too big",
	"Responses lost",
	"Request denied",
	"Busted SNAC payload",
	"Insufficient rights",
	"In local permit/deny",
	"Too evil (sender)",
	"Too evil (receiver)",
	"User temporarily unavailable",
	"No match",
	"List overflow",
	"Request ambiguous",
	"Queue full",
	"Not while on AOL"
};
static int msgerrreasonlen = 25;

static void oscar_callback(gpointer data, gint source,
				GaimInputCondition condition) {
	aim_conn_t *conn = (aim_conn_t *)data;
	aim_session_t *sess = aim_conn_getsess(conn);
	struct gaim_connection *gc = sess ? sess->aux_data : NULL;
	struct oscar_data *odata;

	if (!gc) {
		/* gc is null. we return, else we seg SIGSEG on next line. */
		return;
	}
      
	if (!g_slist_find(get_connections(), gc)) {
		/* oh boy. this is probably bad. i guess the only thing we 
		 * can really do is return? */
		return;
	}

	odata = (struct oscar_data *)gc->proto_data;

	if (condition & GAIM_INPUT_READ) {
		if (aim_get_command(odata->sess, conn) >= 0) {
			aim_rxdispatch(odata->sess);
                               if (odata->killme)
                                       signoff(gc);
		} else {
			if ((conn->type == AIM_CONN_TYPE_BOS) ||
				   !(aim_getconn_type(odata->sess, AIM_CONN_TYPE_BOS))) {
				hide_login_progress_error(gc, _("Disconnected."));
				signoff(gc);
			} else if (conn->type == AIM_CONN_TYPE_CHAT) {
				struct chat_connection *c = find_oscar_chat_by_conn(gc, conn);
				char buf[BUF_LONG];
				c->conn = NULL;
				if (c->inpa > 0)
					gaim_input_remove(c->inpa);
				c->inpa = 0;
				c->fd = -1;
				aim_conn_kill(odata->sess, &conn);
				sprintf(buf, _("You have been disconnected from chat room %s."), c->name);
				do_error_dialog(sess->aux_data, buf, _("Chat Error!"));
			} else if (conn->type == AIM_CONN_TYPE_CHATNAV) {
				if (odata->cnpa > 0)
					gaim_input_remove(odata->cnpa);
				odata->cnpa = 0;
				while (odata->create_rooms) {
					struct create_room *cr = odata->create_rooms->data;
					g_free(cr->name);
					odata->create_rooms =
						g_slist_remove(odata->create_rooms, cr);
					g_free(cr);
					do_error_dialog(sess->aux_data, _("Chat is currently unavailable"),
							_("Gaim - Chat"));
				}
				aim_conn_kill(odata->sess, &conn);
			} else if (conn->type == AIM_CONN_TYPE_AUTH) {
				if (odata->paspa > 0)
					gaim_input_remove(odata->paspa);
				odata->paspa = 0;
				aim_conn_kill(odata->sess, &conn);
			} else {
				aim_conn_kill(odata->sess, &conn);
			}
		}
	}
}

static void oscar_login_connect(gpointer data, gint source, GaimInputCondition cond)
{
	struct gaim_connection *gc = data;
	struct oscar_data *odata;
	aim_session_t *sess;
	aim_conn_t *conn;

	if (!g_slist_find(get_connections(), gc)) {
		closesocket(source);
		return;
	}

	odata = gc->proto_data;
	sess = odata->sess;
	conn = aim_getconn_type_all(sess, AIM_CONN_TYPE_AUTH);

	if (source < 0) {
		hide_login_progress(gc, _("Couldn't connect to host"));
		signoff(gc);
		return;
	}

	aim_conn_completeconnect(sess, conn);
	gc->inpa = gaim_input_add(conn->fd, GAIM_INPUT_READ,
			oscar_callback, conn);
}

static void oscar_login(struct aim_user *user) {
	aim_session_t *sess;
	aim_conn_t *conn;
	char buf[256];
	struct gaim_connection *gc = new_gaim_conn(user);
	struct oscar_data *odata = gc->proto_data = g_new0(struct oscar_data, 1);

	if (isdigit(*user->username)) {
		odata->icq = TRUE;
		/* this is odd but it's necessary for a proper do_import and do_export */
		gc->password[8] = 0;
	} else {
		gc->flags |= OPT_CONN_HTML;
	}

	sess = g_new0(aim_session_t, 1);

	aim_session_init(sess, AIM_SESS_FLAGS_NONBLOCKCONNECT, 0);

	/* we need an immediate queue because we don't use a while-loop to
	 * see if things need to be sent. */
	aim_tx_setenqueue(sess, AIM_TX_IMMEDIATE, NULL);
	odata->sess = sess;
	sess->aux_data = gc;

	conn = aim_newconn(sess, AIM_CONN_TYPE_AUTH, NULL);
	if (conn == NULL) {
		hide_login_progress(gc, _("Unable to login to AIM"));
		signoff(gc);
		return;
	}
	
	if (g_strcasecmp(user->proto_opt[USEROPT_AUTH], "login.icq.com") != 0 &&
	    g_strcasecmp(user->proto_opt[USEROPT_AUTH], "login.oscar.aol.com") != 0) {
		serv_got_crap(gc, "Warning: Unknown OSCAR server: `%s'. Please review your configuration if the connection fails.");
	}
	
	g_snprintf(buf, sizeof(buf), _("Signon: %s"), gc->username);
	set_login_progress(gc, 2, buf);

	aim_conn_addhandler(sess, conn, 0x0017, 0x0007, gaim_parse_login, 0);
	aim_conn_addhandler(sess, conn, 0x0017, 0x0003, gaim_parse_auth_resp, 0);

	conn->status |= AIM_CONN_STATUS_INPROGRESS;
	conn->fd = proxy_connect(user->proto_opt[USEROPT_AUTH][0] ?
					user->proto_opt[USEROPT_AUTH] : AIM_DEFAULT_LOGIN_SERVER,
				 user->proto_opt[USEROPT_AUTHPORT][0] ?
					atoi(user->proto_opt[USEROPT_AUTHPORT]) : AIM_LOGIN_PORT,
				 oscar_login_connect, gc);
	if (conn->fd < 0) {
		hide_login_progress(gc, _("Couldn't connect to host"));
		signoff(gc);
		return;
	}
	aim_request_login(sess, conn, gc->username);
}

static void oscar_close(struct gaim_connection *gc) {
	struct oscar_data *odata = (struct oscar_data *)gc->proto_data;
	
	while (odata->oscar_chats) {
		struct chat_connection *n = odata->oscar_chats->data;
		if (n->inpa > 0)
			gaim_input_remove(n->inpa);
		g_free(n->name);
		g_free(n->show);
		odata->oscar_chats = g_slist_remove(odata->oscar_chats, n);
		g_free(n);
	}
	while (odata->create_rooms) {
		struct create_room *cr = odata->create_rooms->data;
		g_free(cr->name);
		odata->create_rooms = g_slist_remove(odata->create_rooms, cr);
		g_free(cr);
	}
	if (odata->email)
		g_free(odata->email);
	if (odata->newp)
		g_free(odata->newp);
	if (odata->oldp)
		g_free(odata->oldp);
	if (gc->inpa > 0)
		gaim_input_remove(gc->inpa);
	if (odata->cnpa > 0)
		gaim_input_remove(odata->cnpa);
	if (odata->paspa > 0)
		gaim_input_remove(odata->paspa);
	aim_session_kill(odata->sess);
	g_free(odata->sess);
	odata->sess = NULL;
	g_free(gc->proto_data);
	gc->proto_data = NULL;
}

static void oscar_bos_connect(gpointer data, gint source, GaimInputCondition cond) {
	struct gaim_connection *gc = data;
	struct oscar_data *odata;
	aim_session_t *sess;
	aim_conn_t *bosconn;

	if (!g_slist_find(get_connections(), gc)) {
		closesocket(source);
		return;
	}

	odata = gc->proto_data;
	sess = odata->sess;
	bosconn = odata->conn;

	if (source < 0) {
		hide_login_progress(gc, _("Could Not Connect"));
		signoff(gc);
		return;
	}

	aim_conn_completeconnect(sess, bosconn);
	gc->inpa = gaim_input_add(bosconn->fd, GAIM_INPUT_READ,
			oscar_callback, bosconn);
	set_login_progress(gc, 4, _("Connection established, cookie sent"));
}

static int gaim_parse_auth_resp(aim_session_t *sess, aim_frame_t *fr, ...) {
	va_list ap;
	struct aim_authresp_info *info;
	int i; char *host; int port;
	struct aim_user *user;
	aim_conn_t *bosconn;

	struct gaim_connection *gc = sess->aux_data;
        struct oscar_data *od = gc->proto_data;
	user = gc->user;
	port = user->proto_opt[USEROPT_AUTHPORT][0] ?
		atoi(user->proto_opt[USEROPT_AUTHPORT]) : AIM_LOGIN_PORT,

	va_start(ap, fr);
	info = va_arg(ap, struct aim_authresp_info *);
	va_end(ap);

	if (info->errorcode || !info->bosip || !info->cookie) {
		switch (info->errorcode) {
		case 0x05:
			/* Incorrect nick/password */
			hide_login_progress(gc, _("Incorrect nickname or password."));
//			plugin_event(event_error, (void *)980, 0, 0, 0);
			break;
		case 0x11:
			/* Suspended account */
			hide_login_progress(gc, _("Your account is currently suspended."));
			break;
		case 0x18:
			/* connecting too frequently */
			hide_login_progress(gc, _("You have been connecting and disconnecting too frequently. Wait ten minutes and try again. If you continue to try, you will need to wait even longer."));
			break;
		case 0x1c:
			/* client too old */
			hide_login_progress(gc, _("The client version you are using is too old. Please upgrade at " WEBSITE));
			break;
		default:
			hide_login_progress(gc, _("Authentication Failed"));
			break;
		}
		od->killme = TRUE;
		return 1;
	}


	aim_conn_kill(sess, &fr->conn);

	bosconn = aim_newconn(sess, AIM_CONN_TYPE_BOS, NULL);
	if (bosconn == NULL) {
		hide_login_progress(gc, _("Internal Error"));
		od->killme = TRUE;
		return 0;
	}

	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_SPECIAL, AIM_CB_SPECIAL_CONNINITDONE, conninitdone_bos, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_BOS, AIM_CB_BOS_RIGHTS, gaim_bosrights, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_ACK, AIM_CB_ACK_ACK, NULL, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_GEN, AIM_CB_GEN_REDIRECT, gaim_handle_redirect, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_LOC, AIM_CB_LOC_RIGHTSINFO, gaim_parse_locaterights, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_BUD, AIM_CB_BUD_RIGHTSINFO, gaim_parse_buddyrights, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_BUD, AIM_CB_BUD_ONCOMING, gaim_parse_oncoming, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_BUD, AIM_CB_BUD_OFFGOING, gaim_parse_offgoing, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_MSG, AIM_CB_MSG_INCOMING, gaim_parse_incoming_im, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_LOC, AIM_CB_LOC_ERROR, gaim_parse_locerr, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_MSG, AIM_CB_MSG_MISSEDCALL, gaim_parse_misses, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_GEN, AIM_CB_GEN_RATECHANGE, gaim_parse_ratechange, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_MSG, AIM_CB_MSG_ERROR, gaim_parse_msgerr, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_GEN, AIM_CB_GEN_MOTD, gaim_parse_motd, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_MSG, AIM_CB_MSG_PARAMINFO, gaim_icbm_param_info, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_GEN, AIM_CB_GEN_ERROR, gaim_parse_genericerr, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_BUD, AIM_CB_BUD_ERROR, gaim_parse_genericerr, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_BOS, AIM_CB_BOS_ERROR, gaim_parse_genericerr, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_GEN, 0x1f, gaim_memrequest, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_GEN, AIM_CB_GEN_SELFINFO, gaim_selfinfo, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_ICQ, AIM_CB_ICQ_OFFLINEMSG, gaim_offlinemsg, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_ICQ, AIM_CB_ICQ_OFFLINEMSGCOMPLETE, gaim_offlinemsgdone, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_ICQ, AIM_CB_ICQ_INFO, gaim_icqinfo, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_SSI, AIM_CB_SSI_RIGHTSINFO, gaim_ssi_parserights, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_SSI, AIM_CB_SSI_LIST, gaim_ssi_parselist, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_SSI, AIM_CB_SSI_SRVACK, gaim_ssi_parseack, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_LOC, AIM_CB_LOC_USERINFO, gaim_parseaiminfo, 0);
	aim_conn_addhandler(sess, bosconn, AIM_CB_FAM_MSG, AIM_CB_MSG_MTN, gaim_parsemtn, 0);

	((struct oscar_data *)gc->proto_data)->conn = bosconn;
	for (i = 0; i < (int)strlen(info->bosip); i++) {
		if (info->bosip[i] == ':') {
			port = atoi(&(info->bosip[i+1]));
			break;
		}
	}
	host = g_strndup(info->bosip, i);
	bosconn->status |= AIM_CONN_STATUS_INPROGRESS;
	bosconn->fd = proxy_connect(host, port, oscar_bos_connect, gc);
	g_free(host);
	if (bosconn->fd < 0) {
		hide_login_progress(gc, _("Could Not Connect"));
		od->killme = TRUE;
		return 0;
	}
	aim_sendcookie(sess, bosconn, info->cookie);
	gaim_input_remove(gc->inpa);

	return 1;
}

struct pieceofcrap {
	struct gaim_connection *gc;
	unsigned long offset;
	unsigned long len;
	char *modname;
	int fd;
	aim_conn_t *conn;
	unsigned int inpa;
};

static void damn_you(gpointer data, gint source, GaimInputCondition c)
{
	struct pieceofcrap *pos = data;
	struct oscar_data *od = pos->gc->proto_data;
	char in = '\0';
	int x = 0;
	unsigned char m[17];

	while (read(pos->fd, &in, 1) == 1) {
		if (in == '\n')
			x++;
		else if (in != '\r')
			x = 0;
		if (x == 2)
			break;
		in = '\0';
	}
	if (in != '\n') {
		do_error_dialog(pos->gc, "Gaim was unable to get a valid hash for logging into AIM."
				" You may be disconnected shortly.", "Login Error");
		gaim_input_remove(pos->inpa);
		closesocket(pos->fd);
		g_free(pos);
		return;
	}
	read(pos->fd, m, 16);
	m[16] = '\0';
	gaim_input_remove(pos->inpa);
	closesocket(pos->fd);
	aim_sendmemblock(od->sess, pos->conn, 0, 16, m, AIM_SENDMEMBLOCK_FLAG_ISHASH);
	g_free(pos);
}

static void straight_to_hell(gpointer data, gint source, GaimInputCondition cond) {
	struct pieceofcrap *pos = data;
	char buf[BUF_LONG];

	if (source < 0) {
		do_error_dialog(pos->gc, "Gaim was unable to get a valid hash for logging into AIM."
				" You may be disconnected shortly.", "Login Error");
		if (pos->modname)
			g_free(pos->modname);
		g_free(pos);
		return;
	}

	g_snprintf(buf, sizeof(buf), "GET " AIMHASHDATA
			"?offset=%ld&len=%ld&modname=%s HTTP/1.0\n\n",
			pos->offset, pos->len, pos->modname ? pos->modname : "");
	write(pos->fd, buf, strlen(buf));
	if (pos->modname)
		g_free(pos->modname);
	pos->inpa = gaim_input_add(pos->fd, GAIM_INPUT_READ, damn_you, pos);
	return;
}

/* size of icbmui.ocm, the largest module in AIM 3.5 */
#define AIM_MAX_FILE_SIZE 98304

int gaim_memrequest(aim_session_t *sess, aim_frame_t *fr, ...) {
	va_list ap;
	struct pieceofcrap *pos;
	guint32 offset, len;
	char *modname;
	int fd;

	va_start(ap, fr);
	offset = (guint32)va_arg(ap, unsigned long);
	len = (guint32)va_arg(ap, unsigned long);
	modname = va_arg(ap, char *);
	va_end(ap);

	if (len == 0) {
		aim_sendmemblock(sess, fr->conn, offset, len, NULL,
				AIM_SENDMEMBLOCK_FLAG_ISREQUEST);
		return 1;
	}
	/* uncomment this when you're convinced it's right. remember, it's been wrong before.
	if (offset > AIM_MAX_FILE_SIZE || len > AIM_MAX_FILE_SIZE) {
		char *buf;
		int i = 8;
		if (modname)
			i += strlen(modname);
		buf = g_malloc(i);
		i = 0;
		if (modname) {
			memcpy(buf, modname, strlen(modname));
			i += strlen(modname);
		}
		buf[i++] = offset & 0xff;
		buf[i++] = (offset >> 8) & 0xff;
		buf[i++] = (offset >> 16) & 0xff;
		buf[i++] = (offset >> 24) & 0xff;
		buf[i++] = len & 0xff;
		buf[i++] = (len >> 8) & 0xff;
		buf[i++] = (len >> 16) & 0xff;
		buf[i++] = (len >> 24) & 0xff;
		aim_sendmemblock(sess, command->conn, offset, i, buf, AIM_SENDMEMBLOCK_FLAG_ISREQUEST);
		g_free(buf);
		return 1;
	}
	*/

	pos = g_new0(struct pieceofcrap, 1);
	pos->gc = sess->aux_data;
	pos->conn = fr->conn;

	pos->offset = offset;
	pos->len = len;
	pos->modname = modname ? g_strdup(modname) : NULL;

	fd = proxy_connect("gaim.sourceforge.net", 80, straight_to_hell, pos);
	if (fd < 0) {
		if (pos->modname)
			g_free(pos->modname);
		g_free(pos);
		do_error_dialog(sess->aux_data, "Gaim was unable to get a valid hash for logging into AIM."
				" You may be disconnected shortly.", "Login Error");
	}
	pos->fd = fd;

	return 1;
}

static int gaim_parse_login(aim_session_t *sess, aim_frame_t *fr, ...) {
#if 0
	struct client_info_s info = {"gaim", 4, 1, 2010, "us", "en", 0x0004, 0x0000, 0x04b};
#else
	struct client_info_s info = AIM_CLIENTINFO_KNOWNGOOD;
#endif
	char *key;
	va_list ap;
	struct gaim_connection *gc = sess->aux_data;

	va_start(ap, fr);
	key = va_arg(ap, char *);
	va_end(ap);

	aim_send_login(sess, fr->conn, gc->username, gc->password, &info, key);

	return 1;
}

static int conninitdone_chat(aim_session_t *sess, aim_frame_t *fr, ...) {
	struct gaim_connection *gc = sess->aux_data;
	struct chat_connection *chatcon;
	static int id = 1;

	aim_conn_addhandler(sess, fr->conn, 0x000e, 0x0001, gaim_parse_genericerr, 0);
	aim_conn_addhandler(sess, fr->conn, AIM_CB_FAM_CHT, AIM_CB_CHT_USERJOIN, gaim_chat_join, 0);
	aim_conn_addhandler(sess, fr->conn, AIM_CB_FAM_CHT, AIM_CB_CHT_USERLEAVE, gaim_chat_leave, 0);
	aim_conn_addhandler(sess, fr->conn, AIM_CB_FAM_CHT, AIM_CB_CHT_ROOMINFOUPDATE, gaim_chat_info_update, 0);
	aim_conn_addhandler(sess, fr->conn, AIM_CB_FAM_CHT, AIM_CB_CHT_INCOMINGMSG, gaim_chat_incoming_msg, 0);

	aim_clientready(sess, fr->conn);

	chatcon = find_oscar_chat_by_conn(gc, fr->conn);
	chatcon->id = id;
	chatcon->cnv = serv_got_joined_chat(gc, id++, chatcon->show);

	return 1;
}

static int conninitdone_chatnav(aim_session_t *sess, aim_frame_t *fr, ...) {

	aim_conn_addhandler(sess, fr->conn, AIM_CB_FAM_CTN, AIM_CB_CTN_ERROR, gaim_parse_genericerr, 0);
	aim_conn_addhandler(sess, fr->conn, AIM_CB_FAM_CTN, AIM_CB_CTN_INFO, gaim_chatnav_info, 0);

	aim_clientready(sess, fr->conn);

	aim_chatnav_reqrights(sess, fr->conn);

	return 1;
}

static void oscar_chatnav_connect(gpointer data, gint source, GaimInputCondition cond) {
	struct gaim_connection *gc = data;
	struct oscar_data *odata;
	aim_session_t *sess;
	aim_conn_t *tstconn;

	if (!g_slist_find(get_connections(), gc)) {
		closesocket(source);
		return;
	}

	odata = gc->proto_data;
	sess = odata->sess;
	tstconn = aim_getconn_type_all(sess, AIM_CONN_TYPE_CHATNAV);

	if (source < 0) {
		aim_conn_kill(sess, &tstconn);
		return;
	}

	aim_conn_completeconnect(sess, tstconn);
	odata->cnpa = gaim_input_add(tstconn->fd, GAIM_INPUT_READ,
					oscar_callback, tstconn);
}

static void oscar_auth_connect(gpointer data, gint source, GaimInputCondition cond)
{
	struct gaim_connection *gc = data;
	struct oscar_data *odata;
	aim_session_t *sess;
	aim_conn_t *tstconn;

	if (!g_slist_find(get_connections(), gc)) {
		closesocket(source);
		return;
	}

	odata = gc->proto_data;
	sess = odata->sess;
	tstconn = aim_getconn_type_all(sess, AIM_CONN_TYPE_AUTH);

	if (source < 0) {
		aim_conn_kill(sess, &tstconn);
		return;
	}

	aim_conn_completeconnect(sess, tstconn);
	odata->paspa = gaim_input_add(tstconn->fd, GAIM_INPUT_READ,
				oscar_callback, tstconn);
}

static void oscar_chat_connect(gpointer data, gint source, GaimInputCondition cond)
{
	struct chat_connection *ccon = data;
	struct gaim_connection *gc = ccon->gc;
	struct oscar_data *odata;
	aim_session_t *sess;
	aim_conn_t *tstconn;

	if (!g_slist_find(get_connections(), gc)) {
		closesocket(source);
		g_free(ccon->show);
		g_free(ccon->name);
		g_free(ccon);
		return;
	}

	odata = gc->proto_data;
	sess = odata->sess;
	tstconn = ccon->conn;

	if (source < 0) {
		aim_conn_kill(sess, &tstconn);
		g_free(ccon->show);
		g_free(ccon->name);
		g_free(ccon);
		return;
	}

	aim_conn_completeconnect(sess, ccon->conn);
	ccon->inpa = gaim_input_add(tstconn->fd,
			GAIM_INPUT_READ,
			oscar_callback, tstconn);
	odata->oscar_chats = g_slist_append(odata->oscar_chats, ccon);
}

/* Hrmph. I don't know how to make this look better. --mid */
static int gaim_handle_redirect(aim_session_t *sess, aim_frame_t *fr, ...) {
	va_list ap;
	struct aim_redirect_data *redir;
	struct gaim_connection *gc = sess->aux_data;
	struct aim_user *user = gc->user;
	aim_conn_t *tstconn;
	int i;
	char *host;
	int port;

	port = user->proto_opt[USEROPT_AUTHPORT][0] ?
		atoi(user->proto_opt[USEROPT_AUTHPORT]) : AIM_LOGIN_PORT,

	va_start(ap, fr);
	redir = va_arg(ap, struct aim_redirect_data *);
	va_end(ap);

	for (i = 0; i < (int)strlen(redir->ip); i++) {
		if (redir->ip[i] == ':') {
			port = atoi(&(redir->ip[i+1]));
			break;
		}
	}
	host = g_strndup(redir->ip, i);

	switch(redir->group) {
	case 0x7: /* Authorizer */
		tstconn = aim_newconn(sess, AIM_CONN_TYPE_AUTH, NULL);
		if (tstconn == NULL) {
			g_free(host);
			return 1;
		}
		aim_conn_addhandler(sess, tstconn, AIM_CB_FAM_SPECIAL, AIM_CB_SPECIAL_CONNINITDONE, conninitdone_admin, 0);
//		aim_conn_addhandler(sess, tstconn, 0x0007, 0x0003, gaim_info_change, 0);
//		aim_conn_addhandler(sess, tstconn, 0x0007, 0x0005, gaim_info_change, 0);
//		aim_conn_addhandler(sess, tstconn, 0x0007, 0x0007, gaim_account_confirm, 0);

		tstconn->status |= AIM_CONN_STATUS_INPROGRESS;
		tstconn->fd = proxy_connect(host, port, oscar_auth_connect, gc);
		if (tstconn->fd < 0) {
			aim_conn_kill(sess, &tstconn);
			g_free(host);
			return 1;
		}
		aim_sendcookie(sess, tstconn, redir->cookie);
		break;
	case 0xd: /* ChatNav */
		tstconn = aim_newconn(sess, AIM_CONN_TYPE_CHATNAV, NULL);
		if (tstconn == NULL) {
			g_free(host);
			return 1;
		}
		aim_conn_addhandler(sess, tstconn, AIM_CB_FAM_SPECIAL, AIM_CB_SPECIAL_CONNINITDONE, conninitdone_chatnav, 0);

		tstconn->status |= AIM_CONN_STATUS_INPROGRESS;
		tstconn->fd = proxy_connect(host, port, oscar_chatnav_connect, gc);
		if (tstconn->fd < 0) {
			aim_conn_kill(sess, &tstconn);
			g_free(host);
			return 1;
		}
		aim_sendcookie(sess, tstconn, redir->cookie);
		break;
	case 0xe: /* Chat */
		{
		struct chat_connection *ccon;

		tstconn = aim_newconn(sess, AIM_CONN_TYPE_CHAT, NULL);
		if (tstconn == NULL) {
			g_free(host);
			return 1;
		}

		aim_conn_addhandler(sess, tstconn, AIM_CB_FAM_SPECIAL, AIM_CB_SPECIAL_CONNINITDONE, conninitdone_chat, 0);

		ccon = g_new0(struct chat_connection, 1);
		ccon->conn = tstconn;
		ccon->gc = gc;
		ccon->fd = -1;
		ccon->name = g_strdup(redir->chat.room);
		ccon->exchange = redir->chat.exchange;
		ccon->instance = redir->chat.instance;
		ccon->show = extract_name(redir->chat.room);
		
		ccon->conn->status |= AIM_CONN_STATUS_INPROGRESS;
		ccon->conn->fd = proxy_connect(host, port, oscar_chat_connect, ccon);
		if (ccon->conn->fd < 0) {
			aim_conn_kill(sess, &tstconn);
			g_free(host);
			g_free(ccon->show);
			g_free(ccon->name);
			g_free(ccon);
			return 1;
		}
		aim_sendcookie(sess, tstconn, redir->cookie);
		}
		break;
	default: /* huh? */
		break;
	}

	g_free(host);
	return 1;
}

static int gaim_parse_oncoming(aim_session_t *sess, aim_frame_t *fr, ...) {
	struct gaim_connection *gc = sess->aux_data;
	struct oscar_data *od = gc->proto_data;
	aim_userinfo_t *info;
	time_t time_idle = 0, signon = 0;
	int type = 0;
	int caps = 0;
	char *tmp;

	va_list ap;
	va_start(ap, fr);
	info = va_arg(ap, aim_userinfo_t *);
	va_end(ap);

	if (info->present & AIM_USERINFO_PRESENT_CAPABILITIES)
		caps = info->capabilities;
	if (info->flags & AIM_FLAG_ACTIVEBUDDY)
		type |= UC_AB;

	if ((!od->icq) && (info->present & AIM_USERINFO_PRESENT_FLAGS)) {
		if (info->flags & AIM_FLAG_UNCONFIRMED)
			type |= UC_UNCONFIRMED;
		if (info->flags & AIM_FLAG_ADMINISTRATOR)
			type |= UC_ADMIN;
		if (info->flags & AIM_FLAG_AOL)
			type |= UC_AOL;
		if (info->flags & AIM_FLAG_FREE)
			type |= UC_NORMAL;
		if (info->flags & AIM_FLAG_AWAY)
			type |= UC_UNAVAILABLE;
		if (info->flags & AIM_FLAG_WIRELESS)
			type |= UC_WIRELESS;
	}
	if (info->present & AIM_USERINFO_PRESENT_ICQEXTSTATUS) {
		type = (info->icqinfo.status << 7);
		if (!(info->icqinfo.status & AIM_ICQ_STATE_CHAT) &&
		      (info->icqinfo.status != AIM_ICQ_STATE_NORMAL)) {
			type |= UC_UNAVAILABLE;
		}
	}

	if (caps & AIM_CAPS_ICQ)
		caps ^= AIM_CAPS_ICQ;

	if (info->present & AIM_USERINFO_PRESENT_IDLE) {
		time(&time_idle);
		time_idle -= info->idletime*60;
	}

	if (info->present & AIM_USERINFO_PRESENT_SESSIONLEN)
		signon = time(NULL) - info->sessionlen;

	tmp = g_strdup(normalize(gc->username));
	if (!strcmp(tmp, normalize(info->sn)))
		g_snprintf(gc->displayname, sizeof(gc->displayname), "%s", info->sn);
	g_free(tmp);

	serv_got_update(gc, info->sn, 1, info->warnlevel/10, signon,
			time_idle, type, caps);

	return 1;
}

static int gaim_parse_offgoing(aim_session_t *sess, aim_frame_t *fr, ...) {
	aim_userinfo_t *info;
	va_list ap;
	struct gaim_connection *gc = sess->aux_data;

	va_start(ap, fr);
	info = va_arg(ap, aim_userinfo_t *);
	va_end(ap);

	serv_got_update(gc, info->sn, 0, 0, 0, 0, 0, 0);

	return 1;
}

static int incomingim_chan1(aim_session_t *sess, aim_conn_t *conn, aim_userinfo_t *userinfo, struct aim_incomingim_ch1_args *args) {
	char *tmp = g_malloc(BUF_LONG + 1);
	struct gaim_connection *gc = sess->aux_data;
	int flags = 0;
	
	if (args->icbmflags & AIM_IMFLAGS_AWAY)
		flags |= IM_FLAG_AWAY;
	
	if ((args->icbmflags & AIM_IMFLAGS_UNICODE) || (args->icbmflags & AIM_IMFLAGS_ISO_8859_1)) {
		char *src;
		
		if (args->icbmflags & AIM_IMFLAGS_UNICODE)
			src = "UNICODEBIG";
		else
			src = "ISO8859-1";
		
		/* Try to use iconv first to convert the message to UTF8 - which is what BitlBee expects */
		if (do_iconv(src, "UTF-8", args->msg, tmp, args->msglen, BUF_LONG) >= 0) {
			// Successfully converted!
		} else if (args->icbmflags & AIM_IMFLAGS_UNICODE) {
			int i;
			
			for (i = 0, tmp[0] = '\0'; i < args->msglen; i += 2) {
				unsigned short uni;
				
				uni = ((args->msg[i] & 0xff) << 8) | (args->msg[i+1] & 0xff);
	
				if ((uni < 128) || ((uni >= 160) && (uni <= 255))) { /* ISO 8859-1 */
					g_snprintf(tmp+strlen(tmp), BUF_LONG-strlen(tmp), "%c", uni);
				} else { /* something else, do UNICODE entity */
					g_snprintf(tmp+strlen(tmp), BUF_LONG-strlen(tmp), "&#%04x;", uni);
				}
			}
		} else {
			g_snprintf(tmp, BUF_LONG, "%s", args->msg);
		}
	} else
		g_snprintf(tmp, BUF_LONG, "%s", args->msg);
	
	strip_linefeed(tmp);
	serv_got_im(gc, userinfo->sn, tmp, flags, time(NULL), -1);
	g_free(tmp);
	
	return 1;
}

void oscar_accept_chat(gpointer w, struct aim_chat_invitation * inv);
void oscar_reject_chat(gpointer w, struct aim_chat_invitation * inv);
	
static int incomingim_chan2(aim_session_t *sess, aim_conn_t *conn, aim_userinfo_t *userinfo, struct aim_incomingim_ch2_args *args) {
	struct gaim_connection *gc = sess->aux_data;

	if (args->status != AIM_RENDEZVOUS_PROPOSE)
		return 1;

	if (args->reqclass & AIM_CAPS_CHAT) {
		char *name = extract_name(args->info.chat.roominfo.name);
		int *exch = g_new0(int, 1);
		GList *m = NULL;
		char txt[1024];
		struct aim_chat_invitation * inv = g_new0(struct aim_chat_invitation, 1);

		m = g_list_append(m, g_strdup(name ? name : args->info.chat.roominfo.name));
		*exch = args->info.chat.roominfo.exchange;
		m = g_list_append(m, exch);

		g_snprintf( txt, 1024, "Got an invitation to chatroom %s from %s: %s", name, userinfo->sn, args->msg );

		inv->gc = gc;
		inv->exchange = *exch;
		inv->name = g_strdup(name);
		
		do_ask_dialog( gc, txt, inv, oscar_accept_chat, oscar_reject_chat);
	
		if (name)
			g_free(name);
	}

	return 1;
}

static void gaim_icq_authgrant(gpointer w, struct icq_auth *data) {
	char *uin, message;
	struct oscar_data *od = (struct oscar_data *)data->gc->proto_data;
	
	uin = g_strdup_printf("%u", data->uin);
	message = 0;
	aim_ssi_auth_reply(od->sess, od->conn, uin, 1, "");
	// aim_send_im_ch4(od->sess, uin, AIM_ICQMSG_AUTHGRANTED, &message);
	show_got_added(data->gc, NULL, uin, NULL, NULL);
	
	g_free(uin);
	g_free(data);
}

static void gaim_icq_authdeny(gpointer w, struct icq_auth *data) {
	char *uin, *message;
	struct oscar_data *od = (struct oscar_data *)data->gc->proto_data;
	
	uin = g_strdup_printf("%u", data->uin);
	message = g_strdup_printf("No reason given.");
	aim_ssi_auth_reply(od->sess, od->conn, uin, 0, "");
	// aim_send_im_ch4(od->sess, uin, AIM_ICQMSG_AUTHDENIED, message);
	g_free(message);
	
	g_free(uin);
	g_free(data);
}

/*
 * For when other people ask you for authorization
 */
static void gaim_icq_authask(struct gaim_connection *gc, guint32 uin, char *msg) {
	struct icq_auth *data = g_new(struct icq_auth, 1);
	char *reason = NULL;
	char *dialog_msg;
	
	if (strlen(msg) > 6)
		reason = msg + 6;
	
	dialog_msg = g_strdup_printf("The user %u wants to add you to their buddy list for the following reason:\n\n%s", uin, reason ? reason : "No reason given.");
	data->gc = gc;
	data->uin = uin;
	do_ask_dialog(gc, dialog_msg, data, gaim_icq_authgrant, gaim_icq_authdeny);
	g_free(dialog_msg);
}

static int incomingim_chan4(aim_session_t *sess, aim_conn_t *conn, aim_userinfo_t *userinfo, struct aim_incomingim_ch4_args *args) {
	struct gaim_connection *gc = sess->aux_data;

	switch (args->type) {
		case 0x0001: { /* An almost-normal instant message.  Mac ICQ sends this.  It's peculiar. */
			char *uin, *message;
			uin = g_strdup_printf("%u", args->uin);
			message = g_strdup(args->msg);
			strip_linefeed(message);
			serv_got_im(gc, uin, message, 0, time(NULL), -1);
			g_free(uin);
			g_free(message);
		} break;

		case 0x0004: { /* Someone sent you a URL */
		  	char *uin, *message;
			char **m;
	
			uin = g_strdup_printf("%u", args->uin);
			m = g_strsplit(args->msg, "\376", 2);

			if ((strlen(m[0]) != 0)) {
			  message = g_strjoinv(" -- ", m);
			} else {
			  message = m[1];
			}

			strip_linefeed(message);
			serv_got_im(gc, uin, message, 0, time(NULL), -1);
			g_free(uin);
			g_free(m);
			g_free(message);
		} break;
		
		case 0x0006: { /* Someone requested authorization */
			gaim_icq_authask(gc, args->uin, args->msg);
		} break;

		case 0x0007: { /* Someone has denied you authorization */
			serv_got_crap(sess->aux_data, "The user %u has denied your request to add them to your contact list for the following reason:\n%s", args->uin, args->msg ? args->msg : _("No reason given.") );
		} break;

		case 0x0008: { /* Someone has granted you authorization */
			serv_got_crap(sess->aux_data, "The user %u has granted your request to add them to your contact list for the following reason:\n%s", args->uin, args->msg ? args->msg : _("No reason given.") );
		} break;

		case 0x0012: {
			/* Ack for authorizing/denying someone.  Or possibly an ack for sending any system notice */
		} break;

		default: {;
		} break;
	}

	return 1;
}

static int gaim_parse_incoming_im(aim_session_t *sess, aim_frame_t *fr, ...) {
	int channel, ret = 0;
	aim_userinfo_t *userinfo;
	va_list ap;

	va_start(ap, fr);
	channel = va_arg(ap, int);
	userinfo = va_arg(ap, aim_userinfo_t *);

    if (set_getint(sess->aux_data, "debug")) {
        serv_got_crap(sess->aux_data, "channel %i called", channel);
    }

	switch (channel) {
		case 1: { /* standard message */
			struct aim_incomingim_ch1_args *args;
			args = va_arg(ap, struct aim_incomingim_ch1_args *);
			ret = incomingim_chan1(sess, fr->conn, userinfo, args);
		} break;

		case 2: { /* rendevous */
			struct aim_incomingim_ch2_args *args;
			args = va_arg(ap, struct aim_incomingim_ch2_args *);
			ret = incomingim_chan2(sess, fr->conn, userinfo, args);
		} break;

		case 4: { /* ICQ */
			struct aim_incomingim_ch4_args *args;
			args = va_arg(ap, struct aim_incomingim_ch4_args *);
			ret = incomingim_chan4(sess, fr->conn, userinfo, args);
		} break;

		default: {;
		} break;
	}

	va_end(ap);

	return ret;
}

static int gaim_parse_misses(aim_session_t *sess, aim_frame_t *fr, ...) {
	va_list ap;
	guint16 chan, nummissed, reason;
	aim_userinfo_t *userinfo;
	char buf[1024];

	va_start(ap, fr);
	chan = (guint16)va_arg(ap, unsigned int);
	userinfo = va_arg(ap, aim_userinfo_t *);
	nummissed = (guint16)va_arg(ap, unsigned int);
	reason = (guint16)va_arg(ap, unsigned int);
	va_end(ap);

	switch(reason) {
		case 0:
			/* Invalid (0) */
			g_snprintf(buf,
				   sizeof(buf),
				   nummissed == 1 ? 
				   _("You missed %d message from %s because it was invalid.") :
				   _("You missed %d messages from %s because they were invalid."),
				   nummissed,
				   userinfo->sn);
			break;
		case 1:
			/* Message too large */
			g_snprintf(buf,
				   sizeof(buf),
				   nummissed == 1 ?
				   _("You missed %d message from %s because it was too large.") :
				   _("You missed %d messages from %s because they were too large."),
				   nummissed,
				   userinfo->sn);
			break;
		case 2:
			/* Rate exceeded */
			g_snprintf(buf,
				   sizeof(buf),
				   nummissed == 1 ? 
				   _("You missed %d message from %s because the rate limit has been exceeded.") :
				   _("You missed %d messages from %s because the rate limit has been exceeded."),
				   nummissed,
				   userinfo->sn);
			break;
		case 3:
			/* Evil Sender */
			g_snprintf(buf,
				   sizeof(buf),
				   nummissed == 1 ?
				   _("You missed %d message from %s because it was too evil.") : 
				   _("You missed %d messages from %s because they are too evil."),
				   nummissed,
				   userinfo->sn);
			break;
		case 4:
			/* Evil Receiver */
			g_snprintf(buf,
				   sizeof(buf),
				   nummissed == 1 ? 
				   _("You missed %d message from %s because you are too evil.") :
				   _("You missed %d messages from %s because you are too evil."),
				   nummissed,
				   userinfo->sn);
			break;
		default:
			g_snprintf(buf,
				   sizeof(buf),
				   nummissed == 1 ? 
				   _("You missed %d message from %s for unknown reasons.") :
				   _("You missed %d messages from %s for unknown reasons."),
				   nummissed,
				   userinfo->sn);
			break;
	}
	do_error_dialog(sess->aux_data, buf, _("Gaim - Error"));

	return 1;
}

static int gaim_parse_genericerr(aim_session_t *sess, aim_frame_t *fr, ...) {
	va_list ap;
	guint16 reason;
	char *m;

	va_start(ap, fr);
	reason = (guint16)va_arg(ap, unsigned int);
	va_end(ap);

	m = g_strdup_printf(_("SNAC threw error: %s\n"),
			reason < msgerrreasonlen ? msgerrreason[reason] : "Unknown error");
	do_error_dialog(sess->aux_data, m, _("Gaim - Oscar SNAC Error"));
	g_free(m);

	return 1;
}

static int gaim_parse_msgerr(aim_session_t *sess, aim_frame_t *fr, ...) {
	va_list ap;
	char *destn;
	guint16 reason;
	char buf[1024];

	va_start(ap, fr);
	reason = (guint16)va_arg(ap, unsigned int);
	destn = va_arg(ap, char *);
	va_end(ap);

	sprintf(buf, _("Your message to %s did not get sent: %s"), destn,
			(reason < msgerrreasonlen) ? msgerrreason[reason] : _("Reason unknown"));
	do_error_dialog(sess->aux_data, buf, _("Gaim - Error"));

	return 1;
}

static int gaim_parse_locerr(aim_session_t *sess, aim_frame_t *fr, ...) {
	va_list ap;
	char *destn;
	guint16 reason;
	char buf[1024];

	va_start(ap, fr);
	reason = (guint16)va_arg(ap, unsigned int);
	destn = va_arg(ap, char *);
	va_end(ap);

	sprintf(buf, _("User information for %s unavailable: %s"), destn,
			(reason < msgerrreasonlen) ? msgerrreason[reason] : _("Reason unknown"));
	do_error_dialog(sess->aux_data, buf, _("Gaim - Error"));


	return 1;
}

static int gaim_parse_motd(aim_session_t *sess, aim_frame_t *fr, ...) {
	char *msg;
	guint16 id;
	va_list ap;

	va_start(ap, fr);
	id  = (guint16)va_arg(ap, unsigned int);
	msg = va_arg(ap, char *);
	va_end(ap);

	if (id < 4)
		do_error_dialog(sess->aux_data, _("Your connection may be lost."),
				_("AOL error"));

	return 1;
}

static int gaim_chatnav_info(aim_session_t *sess, aim_frame_t *fr, ...) {
	va_list ap;
	guint16 type;
	struct gaim_connection *gc = sess->aux_data;
	struct oscar_data *odata = (struct oscar_data *)gc->proto_data;

	va_start(ap, fr);
	type = (guint16)va_arg(ap, unsigned int);

	switch(type) {
		case 0x0002: {
			guint8 maxrooms;
			struct aim_chat_exchangeinfo *exchanges;
			int exchangecount; // i;

			maxrooms = (guint8)va_arg(ap, unsigned int);
			exchangecount = va_arg(ap, int);
			exchanges = va_arg(ap, struct aim_chat_exchangeinfo *);
			va_end(ap);

			while (odata->create_rooms) {
				struct create_room *cr = odata->create_rooms->data;
				aim_chatnav_createroom(sess, fr->conn, cr->name, cr->exchange);
				g_free(cr->name);
				odata->create_rooms = g_slist_remove(odata->create_rooms, cr);
				g_free(cr);
			}
			}
			break;
		case 0x0008: {
			char *fqcn, *name, *ck;
			guint16 instance, flags, maxmsglen, maxoccupancy, unknown, exchange;
			guint8 createperms;
			guint32 createtime;

			fqcn = va_arg(ap, char *);
			instance = (guint16)va_arg(ap, unsigned int);
			exchange = (guint16)va_arg(ap, unsigned int);
			flags = (guint16)va_arg(ap, unsigned int);
			createtime = va_arg(ap, guint32);
			maxmsglen = (guint16)va_arg(ap, unsigned int);
			maxoccupancy = (guint16)va_arg(ap, unsigned int);
			createperms = (guint8)va_arg(ap, int);
			unknown = (guint16)va_arg(ap, unsigned int);
			name = va_arg(ap, char *);
			ck = va_arg(ap, char *);
			va_end(ap);

			aim_chat_join(odata->sess, odata->conn, exchange, ck, instance);
			}
			break;
		default:
			va_end(ap);
			break;
	}
	return 1;
}

static int gaim_chat_join(aim_session_t *sess, aim_frame_t *fr, ...) {
	va_list ap;
	int count, i;
	aim_userinfo_t *info;
	struct gaim_connection *g = sess->aux_data;

	struct chat_connection *c = NULL;

	va_start(ap, fr);
	count = va_arg(ap, int);
	info  = va_arg(ap, aim_userinfo_t *);
	va_end(ap);

	c = find_oscar_chat_by_conn(g, fr->conn);
	if (!c)
		return 1;

	for (i = 0; i < count; i++)
		add_chat_buddy(c->cnv, info[i].sn);

	return 1;
}

static int gaim_chat_leave(aim_session_t *sess, aim_frame_t *fr, ...) {
	va_list ap;
	int count, i;
	aim_userinfo_t *info;
	struct gaim_connection *g = sess->aux_data;

	struct chat_connection *c = NULL;

	va_start(ap, fr);
	count = va_arg(ap, int);
	info  = va_arg(ap, aim_userinfo_t *);
	va_end(ap);

	c = find_oscar_chat_by_conn(g, fr->conn);
	if (!c)
		return 1;

	for (i = 0; i < count; i++)
		remove_chat_buddy(c->cnv, info[i].sn, NULL);

	return 1;
}

static int gaim_chat_info_update(aim_session_t *sess, aim_frame_t *fr, ...) {
	va_list ap;
	aim_userinfo_t *userinfo;
	struct aim_chat_roominfo *roominfo;
	char *roomname;
	int usercount;
	char *roomdesc;
	guint16 unknown_c9, unknown_d2, unknown_d5, maxmsglen, maxvisiblemsglen;
	guint32 creationtime;
	struct gaim_connection *gc = sess->aux_data;
	struct chat_connection *ccon = find_oscar_chat_by_conn(gc, fr->conn);

	va_start(ap, fr);
	roominfo = va_arg(ap, struct aim_chat_roominfo *);
	roomname = va_arg(ap, char *);
	usercount= va_arg(ap, int);
	userinfo = va_arg(ap, aim_userinfo_t *);
	roomdesc = va_arg(ap, char *);
	unknown_c9 = (guint16)va_arg(ap, int);
	creationtime = (guint32)va_arg(ap, unsigned long);
	maxmsglen = (guint16)va_arg(ap, int);
	unknown_d2 = (guint16)va_arg(ap, int);
	unknown_d5 = (guint16)va_arg(ap, int);
	maxvisiblemsglen = (guint16)va_arg(ap, int);
	va_end(ap);

	ccon->maxlen = maxmsglen;
	ccon->maxvis = maxvisiblemsglen;

	return 1;
}

static int gaim_chat_incoming_msg(aim_session_t *sess, aim_frame_t *fr, ...) {
	va_list ap;
	aim_userinfo_t *info;
	char *msg;
	struct gaim_connection *gc = sess->aux_data;
	struct chat_connection *ccon = find_oscar_chat_by_conn(gc, fr->conn);
	char *tmp;

	va_start(ap, fr);
	info = va_arg(ap, aim_userinfo_t *);
	msg  = va_arg(ap, char *);

	tmp = g_malloc(BUF_LONG);
	g_snprintf(tmp, BUF_LONG, "%s", msg);
	serv_got_chat_in(gc, ccon->id, info->sn, 0, tmp, time((time_t)NULL));
	g_free(tmp);

	return 1;
}

static int gaim_parse_ratechange(aim_session_t *sess, aim_frame_t *fr, ...) {
#if 0
	static const char *codes[5] = {
		"invalid",
		 "change",
		 "warning",
		 "limit",
		 "limit cleared",
	};
#endif
	va_list ap;
	guint16 code, rateclass;
	guint32 windowsize, clear, alert, limit, disconnect, currentavg, maxavg;

	va_start(ap, fr); 
	code = (guint16)va_arg(ap, unsigned int);
	rateclass= (guint16)va_arg(ap, unsigned int);
	windowsize = (guint32)va_arg(ap, unsigned long);
	clear = (guint32)va_arg(ap, unsigned long);
	alert = (guint32)va_arg(ap, unsigned long);
	limit = (guint32)va_arg(ap, unsigned long);
	disconnect = (guint32)va_arg(ap, unsigned long);
	currentavg = (guint32)va_arg(ap, unsigned long);
	maxavg = (guint32)va_arg(ap, unsigned long);
	va_end(ap);

	/* XXX fix these values */
	if (code == AIM_RATE_CODE_CHANGE) {
		if (currentavg >= clear)
			aim_conn_setlatency(fr->conn, 0);
	} else if (code == AIM_RATE_CODE_WARNING) {
		aim_conn_setlatency(fr->conn, windowsize/4);
	} else if (code == AIM_RATE_CODE_LIMIT) {
		do_error_dialog(sess->aux_data, _("The last message was not sent because you are over the rate limit. "
				  "Please wait 10 seconds and try again."), _("Gaim - Error"));
		aim_conn_setlatency(fr->conn, windowsize/2);
	} else if (code == AIM_RATE_CODE_CLEARLIMIT) {
		aim_conn_setlatency(fr->conn, 0);
	}

	return 1;
}

static int gaim_selfinfo(aim_session_t *sess, aim_frame_t *fr, ...) {
	va_list ap;
	aim_userinfo_t *info;
	struct gaim_connection *gc = sess->aux_data;

	va_start(ap, fr);
	info = va_arg(ap, aim_userinfo_t *);
	va_end(ap);

	gc->evil = info->warnlevel/10;
	/* gc->correction_time = (info->onlinesince - gc->login_time); */

	return 1;
}

static int conninitdone_bos(aim_session_t *sess, aim_frame_t *fr, ...) {

	aim_reqpersonalinfo(sess, fr->conn);
	aim_bos_reqlocaterights(sess, fr->conn);
	aim_bos_reqbuddyrights(sess, fr->conn);

	aim_reqicbmparams(sess);

	aim_bos_reqrights(sess, fr->conn);
	aim_bos_setgroupperm(sess, fr->conn, AIM_FLAG_ALLUSERS);
	aim_bos_setprivacyflags(sess, fr->conn, AIM_PRIVFLAGS_ALLOWIDLE |
						     AIM_PRIVFLAGS_ALLOWMEMBERSINCE);

	return 1;
}

static int conninitdone_admin(aim_session_t *sess, aim_frame_t *fr, ...) {
	struct gaim_connection *gc = sess->aux_data;
	struct oscar_data *od = gc->proto_data;

	aim_clientready(sess, fr->conn);

	if (od->chpass) {
		aim_admin_changepasswd(sess, fr->conn, od->newp, od->oldp);
		g_free(od->oldp);
		od->oldp = NULL;
		g_free(od->newp);
		od->newp = NULL;
		od->chpass = FALSE;
	}
	if (od->setnick) {
		aim_admin_setnick(sess, fr->conn, od->newsn);
		g_free(od->newsn);
		od->newsn = NULL;
		od->setnick = FALSE;
	}
	if (od->conf) {
		aim_admin_reqconfirm(sess, fr->conn);
		od->conf = FALSE;
	}
	if (od->reqemail) {
		aim_admin_getinfo(sess, fr->conn, 0x0011);
		od->reqemail = FALSE;
	}
	if (od->setemail) {
		aim_admin_setemail(sess, fr->conn, od->email);
		g_free(od->email);
		od->setemail = FALSE;
	}

	return 1;
}

static int gaim_icbm_param_info(aim_session_t *sess, aim_frame_t *fr, ...) {
	struct aim_icbmparameters *params;
	va_list ap;

	va_start(ap, fr);
	params = va_arg(ap, struct aim_icbmparameters *);
	va_end(ap);

	/* Maybe senderwarn and recverwarn should be user preferences... */
	params->flags = 0x0000000b;
	params->maxmsglen = 8000;
	params->minmsginterval = 0;

	aim_seticbmparam(sess, params);

	return 1;
}

static int gaim_parse_locaterights(aim_session_t *sess, aim_frame_t *fr, ...)
{
	va_list ap;
	guint16 maxsiglen;
	struct gaim_connection *gc = sess->aux_data;
	struct oscar_data *odata = (struct oscar_data *)gc->proto_data;

	va_start(ap, fr);
	maxsiglen = va_arg(ap, int);
	va_end(ap);

	odata->rights.maxsiglen = odata->rights.maxawaymsglen = (guint)maxsiglen;

	aim_bos_setprofile(sess, fr->conn, gc->user->user_info, NULL, gaim_caps);

	return 1;
}

static int gaim_parse_buddyrights(aim_session_t *sess, aim_frame_t *fr, ...) {
	va_list ap;
	guint16 maxbuddies, maxwatchers;
	struct gaim_connection *gc = sess->aux_data;
	struct oscar_data *odata = (struct oscar_data *)gc->proto_data;

	va_start(ap, fr);
	maxbuddies = (guint16)va_arg(ap, unsigned int);
	maxwatchers = (guint16)va_arg(ap, unsigned int);
	va_end(ap);

	odata->rights.maxbuddies = (guint)maxbuddies;
	odata->rights.maxwatchers = (guint)maxwatchers;

	return 1;
}

static int gaim_bosrights(aim_session_t *sess, aim_frame_t *fr, ...) {
	guint16 maxpermits, maxdenies;
	va_list ap;
	struct gaim_connection *gc = sess->aux_data;
	struct oscar_data *odata = (struct oscar_data *)gc->proto_data;

	va_start(ap, fr);
	maxpermits = (guint16)va_arg(ap, unsigned int);
	maxdenies = (guint16)va_arg(ap, unsigned int);
	va_end(ap);

	odata->rights.maxpermits = (guint)maxpermits;
	odata->rights.maxdenies = (guint)maxdenies;

//	serv_finish_login(gc);

	if (bud_list_cache_exists(gc))
		do_import(gc, NULL);

	aim_clientready(sess, fr->conn);

	aim_reqservice(sess, fr->conn, AIM_CONN_TYPE_CHATNAV);

	aim_ssi_reqrights(sess, fr->conn);
	aim_ssi_reqalldata(sess, fr->conn);

	return 1;
}

static int gaim_offlinemsg(aim_session_t *sess, aim_frame_t *fr, ...) {
	va_list ap;
	struct aim_icq_offlinemsg *msg;
	struct gaim_connection *gc = sess->aux_data;

	va_start(ap, fr);
	msg = va_arg(ap, struct aim_icq_offlinemsg *);
	va_end(ap);

	switch (msg->type) {
		case 0x0001: { /* Basic offline message */
			char sender[32];
			char *dialog_msg = g_strdup(msg->msg);
			time_t t = get_time(msg->year, msg->month, msg->day, msg->hour, msg->minute, 0);
			g_snprintf(sender, sizeof(sender), "%u", msg->sender);
			strip_linefeed(dialog_msg);
			serv_got_im(gc, sender, dialog_msg, 0, t, -1);
			g_free(dialog_msg);
		} break;

		case 0x0004: { /* Someone sent you a URL */
		  	char sender[32];
		  	char *dialog_msg;
			char **m;

			time_t t = get_time(msg->year, msg->month, msg->day, msg->hour, msg->minute, 0);
			g_snprintf(sender, sizeof(sender), "%u", msg->sender);

			m = g_strsplit(msg->msg, "\376", 2);

			if ((strlen(m[0]) != 0)) {
			  dialog_msg = g_strjoinv(" -- ", m);
			} else {
			  dialog_msg = m[1];
			}

			strip_linefeed(dialog_msg);
			serv_got_im(gc, sender, dialog_msg, 0, t, -1);
			g_free(dialog_msg);
			g_free(m);
		} break;
		
		case 0x0006: { /* Authorization request */
			gaim_icq_authask(gc, msg->sender, msg->msg);
		} break;

		case 0x0007: { /* Someone has denied you authorization */
			serv_got_crap(sess->aux_data, "The user %u has denied your request to add them to your contact list for the following reason:\n%s", msg->sender, msg->msg ? msg->msg : _("No reason given.") );
		} break;

		case 0x0008: { /* Someone has granted you authorization */
			serv_got_crap(sess->aux_data, "The user %u has granted your request to add them to your contact list for the following reason:\n%s", msg->sender, msg->msg ? msg->msg : _("No reason given.") );
		} break;

		case 0x0012: {
			/* Ack for authorizing/denying someone.  Or possibly an ack for sending any system notice */
		} break;

		default: {;
		}
	}

	return 1;
}

static int gaim_offlinemsgdone(aim_session_t *sess, aim_frame_t *fr, ...)
{
	aim_icq_ackofflinemsgs(sess);
	return 1;
}

static void oscar_keepalive(struct gaim_connection *gc) {
	struct oscar_data *odata = (struct oscar_data *)gc->proto_data;
	aim_flap_nop(odata->sess, odata->conn);
}

static int oscar_send_im(struct gaim_connection *gc, char *name, char *message, int len, int imflags) {
	struct oscar_data *odata = (struct oscar_data *)gc->proto_data;
	int ret = 0;
	if (imflags & IM_FLAG_AWAY) {
		ret = aim_send_im(odata->sess, name, AIM_IMFLAGS_AWAY, message);
	} else {
		struct aim_sendimext_args args;
		char *s;
		
		args.flags = AIM_IMFLAGS_ACK;
		if (odata->icq)
			args.flags |= AIM_IMFLAGS_OFFLINE;
		for (s = message; *s; s++)
			if (*s & 128)
				break;
		
		/* Message contains high ASCII chars, time for some translation! */
		if (*s) {
			s = g_malloc(BUF_LONG);
			/* Try if we can put it in an ISO8859-1 string first.
			   If we can't, fall back to UTF16. */
			if ((ret = do_iconv("UTF-8", "ISO8859-1", message, s, len, BUF_LONG)) >= 0) {
				args.flags |= AIM_IMFLAGS_ISO_8859_1;
				len = ret;
			} else if ((ret = do_iconv("UTF-8", "UNICODEBIG", message, s, len, BUF_LONG)) >= 0) {
				args.flags |= AIM_IMFLAGS_UNICODE;
				len = ret;
			} else {
				/* OOF, translation failed... Oh well.. */
				g_free( s );
				s = message;
			}
		} else {
			s = message;
		}
		
		args.features = gaim_features;
		args.featureslen = sizeof(gaim_features);
		
		args.destsn = name;
		args.msg    = s;
		args.msglen = len;
		
		ret = aim_send_im_ext(odata->sess, &args);
		
		if (s != message) {
			g_free(s);
		}
	}
	if (ret >= 0)
		return 1;
	return ret;
}

static void oscar_get_info(struct gaim_connection *g, char *name) {
	struct oscar_data *odata = (struct oscar_data *)g->proto_data;
	if (odata->icq)
		aim_icq_getallinfo(odata->sess, name);
	else {
		aim_getinfo(odata->sess, odata->conn, name, AIM_GETINFO_AWAYMESSAGE);
		aim_getinfo(odata->sess, odata->conn, name, AIM_GETINFO_GENERALINFO);
	}
}

static void oscar_get_away(struct gaim_connection *g, char *who) {
	struct oscar_data *odata = (struct oscar_data *)g->proto_data;
	if (odata->icq) {
		struct buddy *budlight = find_buddy(g, who);
		if (budlight)
			if ((budlight->uc & 0xff80) >> 7)
				if (budlight->caps & AIM_CAPS_ICQSERVERRELAY)
					aim_send_im_ch2_geticqmessage(odata->sess, who, (budlight->uc & 0xff80) >> 7);
	} else
		aim_getinfo(odata->sess, odata->conn, who, AIM_GETINFO_AWAYMESSAGE);
}

static void oscar_set_away_aim(struct gaim_connection *gc, struct oscar_data *od, const char *state, const char *message)
{

	if (!g_strcasecmp(state, _("Visible"))) {
		aim_setextstatus(od->sess, od->conn, AIM_ICQ_STATE_NORMAL);
		return;
	} else if (!g_strcasecmp(state, _("Invisible"))) {
		aim_setextstatus(od->sess, od->conn, AIM_ICQ_STATE_INVISIBLE);
		return;
	} /* else... */

	if (od->rights.maxawaymsglen == 0)
		do_error_dialog(gc, "oscar_set_away_aim called before locate rights received", "Protocol Error");

	aim_setextstatus(od->sess, od->conn, AIM_ICQ_STATE_NORMAL);

	if (gc->away)
		g_free(gc->away);
	gc->away = NULL;

	if (!message) {
		aim_bos_setprofile(od->sess, od->conn, NULL, "", gaim_caps);
		return;
	}

	if (strlen(message) > od->rights.maxawaymsglen) {
		gchar *errstr;

		errstr = g_strdup_printf("Maximum away message length of %d bytes exceeded, truncating", od->rights.maxawaymsglen);

		do_error_dialog(gc, errstr, "Away Message Too Long");

		g_free(errstr);
	}

	gc->away = g_strndup(message, od->rights.maxawaymsglen);
	aim_bos_setprofile(od->sess, od->conn, NULL, gc->away, gaim_caps);

	return;
}

static void oscar_set_away_icq(struct gaim_connection *gc, struct oscar_data *od, const char *state, const char *message)
{
    const char *msg = NULL;
	gboolean no_message = FALSE;

	/* clean old states */
    if (gc->away) {
		g_free(gc->away);
		gc->away = NULL;
    }
	od->sess->aim_icq_state = 0;

	/* if no message, then use an empty message */
    if (message) {
        msg = message;
    } else {
        msg = "";
		no_message = TRUE;
    }

	if (!g_strcasecmp(state, "Online")) {
		aim_setextstatus(od->sess, od->conn, AIM_ICQ_STATE_NORMAL);
	} else if (!g_strcasecmp(state, "Away")) {
		aim_setextstatus(od->sess, od->conn, AIM_ICQ_STATE_AWAY);
        gc->away = g_strdup(msg);
		od->sess->aim_icq_state = AIM_MTYPE_AUTOAWAY;
	} else if (!g_strcasecmp(state, "Do Not Disturb")) {
		aim_setextstatus(od->sess, od->conn, AIM_ICQ_STATE_AWAY | AIM_ICQ_STATE_DND | AIM_ICQ_STATE_BUSY);
        gc->away = g_strdup(msg);
		od->sess->aim_icq_state = AIM_MTYPE_AUTODND;
	} else if (!g_strcasecmp(state, "Not Available")) {
		aim_setextstatus(od->sess, od->conn, AIM_ICQ_STATE_OUT | AIM_ICQ_STATE_AWAY);
        gc->away = g_strdup(msg);
		od->sess->aim_icq_state = AIM_MTYPE_AUTONA;
	} else if (!g_strcasecmp(state, "Occupied")) {
		aim_setextstatus(od->sess, od->conn, AIM_ICQ_STATE_AWAY | AIM_ICQ_STATE_BUSY);
        gc->away = g_strdup(msg);
		od->sess->aim_icq_state = AIM_MTYPE_AUTOBUSY;
	} else if (!g_strcasecmp(state, "Free For Chat")) {
		aim_setextstatus(od->sess, od->conn, AIM_ICQ_STATE_CHAT);
        gc->away = g_strdup(msg);
		od->sess->aim_icq_state = AIM_MTYPE_AUTOFFC;
	} else if (!g_strcasecmp(state, "Invisible")) {
		aim_setextstatus(od->sess, od->conn, AIM_ICQ_STATE_INVISIBLE);
        gc->away = g_strdup(msg);
	} else if (!g_strcasecmp(state, GAIM_AWAY_CUSTOM)) {
	 	if (no_message) {
			aim_setextstatus(od->sess, od->conn, AIM_ICQ_STATE_NORMAL);
		} else {
			aim_setextstatus(od->sess, od->conn, AIM_ICQ_STATE_AWAY);
            gc->away = g_strdup(msg);
			od->sess->aim_icq_state = AIM_MTYPE_AUTOAWAY;
		}
	}

	return;
}

static void oscar_set_away(struct gaim_connection *gc, char *state, char *message)
{
	struct oscar_data *od = (struct oscar_data *)gc->proto_data;

    oscar_set_away_aim(gc, od, state, message);
	if (od->icq)
		oscar_set_away_icq(gc, od, state, message);

	return;
}

static void oscar_add_buddy(struct gaim_connection *g, char *name) {
	struct oscar_data *odata = (struct oscar_data *)g->proto_data;
	aim_ssi_addbuddies(odata->sess, odata->conn, OSCAR_GROUP, &name, 1, 0);
}

static void oscar_remove_buddy(struct gaim_connection *g, char *name, char *group) {
	struct oscar_data *odata = (struct oscar_data *)g->proto_data;
	struct aim_ssi_item *ssigroup;
	while ((ssigroup = aim_ssi_itemlist_findparent(odata->sess->ssi.items, name)) && !aim_ssi_delbuddies(odata->sess, odata->conn, ssigroup->name, &name, 1));
}

static int gaim_ssi_parserights(aim_session_t *sess, aim_frame_t *fr, ...) {
	return 1;
}

static int gaim_ssi_parselist(aim_session_t *sess, aim_frame_t *fr, ...) {
	struct gaim_connection *gc = sess->aux_data;
	struct aim_ssi_item *curitem;
	int tmp;

	/* Add from server list to local list */
	tmp = 0;
	for (curitem=sess->ssi.items; curitem; curitem=curitem->next) {
		switch (curitem->type) {
			case 0x0000: /* Buddy */
				if ((curitem->name) && (!find_buddy(gc, curitem->name))) {
					char *realname = NULL;

					if (curitem->data && aim_gettlv(curitem->data, 0x0131, 1))
						    realname = aim_gettlv_str(curitem->data, 0x0131, 1);
						
					add_buddy(gc, NULL, curitem->name, realname);
					
					if (realname)
					    g_free(realname);
				}
				break;

			case 0x0002: /* Permit buddy */
				if (curitem->name) {
					GSList *list;
					for (list=gc->permit; (list && aim_sncmp(curitem->name, list->data)); list=list->next);
					if (!list) {
						char *name;
						name = g_strdup(normalize(curitem->name));
						gc->permit = g_slist_append(gc->permit, name);
						build_allow_list();
						tmp++;
					}
				}
				break;

			case 0x0003: /* Deny buddy */
				if (curitem->name) {
					GSList *list;
					for (list=gc->deny; (list && aim_sncmp(curitem->name, list->data)); list=list->next);
					if (!list) {
						char *name;
						name = g_strdup(normalize(curitem->name));
						gc->deny = g_slist_append(gc->deny, name);
						build_block_list();
						tmp++;
					}
				}
				break;

			case 0x0004: /* Permit/deny setting */
				if (curitem->data) {
					guint8 permdeny;
					if ((permdeny = aim_ssi_getpermdeny(sess->ssi.items)) && (permdeny != gc->permdeny)) {
						gc->permdeny = permdeny;
						tmp++;
					}
				}
				break;

			case 0x0005: /* Presence setting */
				/* We don't want to change Gaim's setting because it applies to all accounts */
				break;
		} /* End of switch on curitem->type */
	} /* End of for loop */

	if (tmp)
		do_export(gc);
	aim_ssi_enable(sess, fr->conn);
	
	/* Request offline messages, now that the buddy list is complete. */
	aim_icq_reqofflinemsgs(sess);
	
	/* Now that we have a buddy list, we can tell BitlBee that we're online. */
	account_online(gc);
	
	return 1;
}

static int gaim_ssi_parseack( aim_session_t *sess, aim_frame_t *fr, ... )
{
	aim_snac_t *origsnac;
	va_list ap;

	va_start( ap, fr );
	origsnac = va_arg( ap, aim_snac_t * );
	va_end( ap );
	
	if( origsnac && origsnac->family == AIM_CB_FAM_SSI && origsnac->type == AIM_CB_SSI_ADD && origsnac->data )
	{
		int i, st, count = aim_bstream_empty( &fr->data );
		char *list;
		
		if( count & 1 )
		{
			/* Hmm, the length should be even... */
			do_error_dialog( sess->aux_data, "Received SSI ACK package with non-even length", "Gaim - Error" );
			return( 0 );
		}
		count >>= 1;
		
		list = (char *) origsnac->data;
		for( i = 0; i < count; i ++ )
		{
			st = aimbs_get16( &fr->data );
			if( st == 0x0E )
			{
				serv_got_crap( sess->aux_data, "Buddy %s can't be added without authorization, requesting authorization", list );
				
				aim_ssi_auth_request( sess, fr->conn, list, "" );
				aim_ssi_addbuddies( sess, fr->conn, OSCAR_GROUP, &list, 1, 1 );
			}
			list += strlen( list ) + 1;
		}
	}
	
	return( 1 );
}

static void oscar_set_permit_deny(struct gaim_connection *gc) {
	struct oscar_data *od = (struct oscar_data *)gc->proto_data;
	if (od->icq) {
		GSList *list;
		char buf[MAXMSGLEN];
		int at;

		switch(gc->permdeny) {
		case 1:
			aim_bos_changevisibility(od->sess, od->conn, AIM_VISIBILITYCHANGE_DENYADD, gc->username);
			break;
		case 2:
			aim_bos_changevisibility(od->sess, od->conn, AIM_VISIBILITYCHANGE_PERMITADD, gc->username);
			break;
		case 3:
			list = gc->permit;
			at = 0;
			while (list) {
				at += g_snprintf(buf + at, sizeof(buf) - at, "%s&", (char *)list->data);
				list = list->next;
			}
			aim_bos_changevisibility(od->sess, od->conn, AIM_VISIBILITYCHANGE_PERMITADD, buf);
			break;
		case 4:
			list = gc->deny;
			at = 0;
			while (list) {
				at += g_snprintf(buf + at, sizeof(buf) - at, "%s&", (char *)list->data);
				list = list->next;
			}
			aim_bos_changevisibility(od->sess, od->conn, AIM_VISIBILITYCHANGE_DENYADD, buf);
			break;
			default:
			break;
		}
		signoff_blocked(gc);
	} else {
		if (od->sess->ssi.received_data)
			aim_ssi_setpermdeny(od->sess, od->conn, gc->permdeny, 0xffffffff);
	}
}

static void oscar_add_permit(struct gaim_connection *gc, char *who) {
	struct oscar_data *od = (struct oscar_data *)gc->proto_data;
	if (od->icq) {
		aim_ssi_auth_reply(od->sess, od->conn, who, 1, "");
	} else {
		if (od->sess->ssi.received_data)
			aim_ssi_addpord(od->sess, od->conn, &who, 1, AIM_SSI_TYPE_PERMIT);
	}
}

static void oscar_add_deny(struct gaim_connection *gc, char *who) {
	struct oscar_data *od = (struct oscar_data *)gc->proto_data;
	if (od->icq) {
		aim_ssi_auth_reply(od->sess, od->conn, who, 0, "");
	} else {
		if (od->sess->ssi.received_data)
			aim_ssi_addpord(od->sess, od->conn, &who, 1, AIM_SSI_TYPE_DENY);
	}
}

static void oscar_rem_permit(struct gaim_connection *gc, char *who) {
	struct oscar_data *od = (struct oscar_data *)gc->proto_data;
	if (!od->icq) {
		if (od->sess->ssi.received_data)
			aim_ssi_delpord(od->sess, od->conn, &who, 1, AIM_SSI_TYPE_PERMIT);
	}
}

static void oscar_rem_deny(struct gaim_connection *gc, char *who) {
	struct oscar_data *od = (struct oscar_data *)gc->proto_data;
	if (!od->icq) {
		if (od->sess->ssi.received_data)
			aim_ssi_delpord(od->sess, od->conn, &who, 1, AIM_SSI_TYPE_DENY);
	}
}

static GList *oscar_away_states(struct gaim_connection *gc)
{
	struct oscar_data *od = gc->proto_data;
	GList *m = NULL;

	if (!od->icq)
		return g_list_append(m, GAIM_AWAY_CUSTOM);

	m = g_list_append(m, "Online");
	m = g_list_append(m, "Away");
	m = g_list_append(m, "Do Not Disturb");
	m = g_list_append(m, "Not Available");
	m = g_list_append(m, "Occupied");
	m = g_list_append(m, "Free For Chat");
	m = g_list_append(m, "Invisible");

	return m;
}

static int gaim_icqinfo(aim_session_t *sess, aim_frame_t *fr, ...)
{
        struct gaim_connection *gc = sess->aux_data;
        gchar who[16];
        GString *str;
        va_list ap;
        struct aim_icq_info *info;

        va_start(ap, fr);
        info = va_arg(ap, struct aim_icq_info *);
        va_end(ap);

        if (!info->uin)
                return 0;

        str = g_string_sized_new(100);
        g_snprintf(who, sizeof(who), "%u", info->uin);

        g_string_sprintfa(str, "%s: %s - %s: %s", _("UIN"), who, _("Nick"), 
				info->nick ? info->nick : "-");
        info_string_append(str, "\n", _("First Name"), info->first);
        info_string_append(str, "\n", _("Last Name"), info->last);
		info_string_append(str, "\n", _("Email Address"), info->email);
        if (info->numaddresses && info->email2) {
                int i;
                for (i = 0; i < info->numaddresses; i++) {
					info_string_append(str, "\n", _("Email Address"), info->email2[i]);
                }
        }
        info_string_append(str, "\n", _("Mobile Phone"), info->mobile);
        info_string_append(str, "\n", _("Gender"), info->gender==1 ? _("Female") : _("Male"));
        if (info->birthyear || info->birthmonth || info->birthday) {
                char date[30];
                struct tm tm;
                tm.tm_mday = (int)info->birthday;
                tm.tm_mon = (int)info->birthmonth-1;
                tm.tm_year = (int)info->birthyear-1900;
                strftime(date, sizeof(date), "%Y-%m-%d", &tm);
                info_string_append(str, "\n", _("Birthday"), date);
        }
        if (info->age) {
                char age[5];
                g_snprintf(age, sizeof(age), "%hhd", info->age);
                info_string_append(str, "\n", _("Age"), age);
        }
		info_string_append(str, "\n", _("Personal Web Page"), info->personalwebpage);
        if (info->info && info->info[0]) {
                g_string_sprintfa(str, "\n%s:\n%s\n%s", _("Additional Information"), 
						info->info, _("End of Additional Information"));
        }
        g_string_sprintfa(str, "\n");
        if ((info->homeaddr && (info->homeaddr[0])) || (info->homecity && info->homecity[0]) || (info->homestate && info->homestate[0]) || (info->homezip && info->homezip[0])) {
                g_string_sprintfa(str, "%s:", _("Home Address"));
                info_string_append(str, "\n", _("Address"), info->homeaddr);
                info_string_append(str, "\n", _("City"), info->homecity);
                info_string_append(str, "\n", _("State"), info->homestate); 
				info_string_append(str, "\n", _("Zip Code"), info->homezip);
                g_string_sprintfa(str, "\n");
        }
        if ((info->workaddr && info->workaddr[0]) || (info->workcity && info->workcity[0]) || (info->workstate && info->workstate[0]) || (info->workzip && info->workzip[0])) {
                g_string_sprintfa(str, "%s:", _("Work Address"));
                info_string_append(str, "\n", _("Address"), info->workaddr);
                info_string_append(str, "\n", _("City"), info->workcity);
                info_string_append(str, "\n", _("State"), info->workstate);
				info_string_append(str, "\n", _("Zip Code"), info->workzip);
                g_string_sprintfa(str, "\n");
        }
        if ((info->workcompany && info->workcompany[0]) || (info->workdivision && info->workdivision[0]) || (info->workposition && info->workposition[0]) || (info->workwebpage && info->workwebpage[0])) {
                g_string_sprintfa(str, "%s:", _("Work Information"));
                info_string_append(str, "\n", _("Company"), info->workcompany);
                info_string_append(str, "\n", _("Division"), info->workdivision);
                info_string_append(str, "\n", _("Position"), info->workposition);
                if (info->workwebpage && info->workwebpage[0]) {
                        info_string_append(str, "\n", _("Web Page"), info->workwebpage);
                }
                g_string_sprintfa(str, "\n");
        }

		serv_got_crap(gc, "%s\n%s", _("User Info"), str->str);
        g_string_free(str, TRUE);

        return 1;

}

static char *oscar_encoding_extract(const char *encoding)
{
	char *ret = NULL;
	char *begin, *end;

	g_return_val_if_fail(encoding != NULL, NULL);

	/* Make sure encoding begins with charset= */
	if (strncmp(encoding, "text/plain; charset=", 20) &&
		strncmp(encoding, "text/aolrtf; charset=", 21) &&
		strncmp(encoding, "text/x-aolrtf; charset=", 23))
	{
		return NULL;
	}

	begin = strchr(encoding, '"');
	end = strrchr(encoding, '"');

	if ((begin == NULL) || (end == NULL) || (begin >= end))
		return NULL;

	ret = g_strndup(begin+1, (end-1) - begin);

	return ret;
}

static char *oscar_encoding_to_utf8(char *encoding, char *text, int textlen)
{
	char *utf8 = g_new0(char, 8192);

	if ((encoding == NULL) || encoding[0] == '\0') {
		/*		gaim_debug_info("oscar", "Empty encoding, assuming UTF-8\n");*/
	} else if (!g_strcasecmp(encoding, "iso-8859-1")) {
		do_iconv("iso-8859-1", "UTF-8", text, utf8, textlen, 8192);
	} else if (!g_strcasecmp(encoding, "ISO-8859-1-Windows-3.1-Latin-1")) {
		do_iconv("Windows-1252", "UTF-8", text, utf8, textlen, 8192);
	} else if (!g_strcasecmp(encoding, "unicode-2-0")) {
		do_iconv("UCS-2BE", "UTF-8", text, utf8, textlen, 8192);
	} else if (g_strcasecmp(encoding, "us-ascii") && strcmp(encoding, "utf-8")) {
		/*		gaim_debug_warning("oscar", "Unrecognized character encoding \"%s\", "
		  "attempting to convert to UTF-8 anyway\n", encoding);*/
		do_iconv(encoding, "UTF-8", text, utf8, textlen, 8192);
	}

	/*
	 * If utf8 is still NULL then either the encoding is us-ascii/utf-8 or
	 * we have been unable to convert the text to utf-8 from the encoding
	 * that was specified.  So we assume it's UTF-8 and hope for the best.
	 */
	if (*utf8 == 0) {
	    strncpy(utf8, text, textlen);
	}

	return utf8;
}

static int gaim_parseaiminfo(aim_session_t *sess, aim_frame_t *fr, ...)
{
	struct gaim_connection *gc = sess->aux_data;
	va_list ap;
	aim_userinfo_t *userinfo;
	guint16 infotype;
	char *text_encoding = NULL, *text = NULL, *extracted_encoding = NULL;
	guint16 text_length;
	char *utf8 = NULL;

	va_start(ap, fr);
	userinfo = va_arg(ap, aim_userinfo_t *);
	infotype = va_arg(ap, int);
	text_encoding = va_arg(ap, char*);
	text = va_arg(ap, char*);
	text_length = va_arg(ap, int);
	va_end(ap);

	if(text_encoding)
		extracted_encoding = oscar_encoding_extract(text_encoding);
	if(infotype == AIM_GETINFO_GENERALINFO) {
		/*Display idle time*/
		char buff[256];
		struct tm idletime;
		if(userinfo->idletime) {
			memset(&idletime, 0, sizeof(struct tm));
			idletime.tm_mday = (userinfo->idletime / 60) / 24;
			idletime.tm_hour = (userinfo->idletime / 60) % 24;
			idletime.tm_min = userinfo->idletime % 60;
			idletime.tm_sec = 0;
			strftime(buff, 256, _("%d days %H hours %M minutes"), &idletime);
			serv_got_crap(gc, "%s: %s", _("Idle Time"), buff);
		}
		
		if(text) {
			utf8 = oscar_encoding_to_utf8(extracted_encoding, text, text_length);
			serv_got_crap(gc, "%s\n%s", _("User Info"), utf8);
		} else {
			serv_got_crap(gc, _("No user info available."));
		}
	} else if(infotype == AIM_GETINFO_AWAYMESSAGE && userinfo->flags & AIM_FLAG_AWAY) {
		utf8 = oscar_encoding_to_utf8(extracted_encoding, text, text_length);
		serv_got_crap(gc, "%s\n%s", _("Away Message"), utf8);
	}

	g_free(utf8);
   
	return 1;
}

int gaim_parsemtn(aim_session_t *sess, aim_frame_t *fr, ...)
{
	struct gaim_connection * gc = sess->aux_data;
	va_list ap;
	guint16 type1, type2;
	char * sn;

	va_start(ap, fr);
	type1 = va_arg(ap, int);
	sn = va_arg(ap, char*);
	type2 = va_arg(ap, int);
	va_end(ap);
    
	if(type2 == 0x0002) {
		/* User is typing */
		serv_got_typing(gc, sn, 0, 1);
	} 
	else if (type2 == 0x0001) {
		/* User has typed something, but is not actively typing (stale) */
		serv_got_typing(gc, sn, 0, 2);
	}
	else {
		/* User has stopped typing */
		serv_got_typing(gc, sn, 0, 0);
	}        
	
	return 1;
}

static char *oscar_get_status_string( struct gaim_connection *gc, int number )
{
	struct oscar_data *od = gc->proto_data;
	
	if( ! number & UC_UNAVAILABLE )
	{
		return( NULL );
	}
	else if( od->icq )
	{
		number >>= 7;
		if( number & AIM_ICQ_STATE_DND )
			return( "Do Not Disturb" );
		else if( number & AIM_ICQ_STATE_OUT )
			return( "Not Available" );
		else if( number & AIM_ICQ_STATE_BUSY )
			return( "Occupied" );
		else if( number & AIM_ICQ_STATE_INVISIBLE )
			return( "Invisible" );
		else
			return( "Away" );
	}
	else
	{
		return( "Away" );
	}
}

int oscar_send_typing(struct gaim_connection *gc, char * who, int typing)
{
	struct oscar_data *od = gc->proto_data;
	return( aim_im_sendmtn(od->sess, 1, who, typing ? 0x0002 : 0x0000) );
}

int oscar_chat_send(struct gaim_connection * gc, int id, char *message)
{
	struct oscar_data * od = (struct oscar_data*)gc->proto_data;
	struct chat_connection * ccon;
	int ret;
	guint8 len = strlen(message);
	char *s;
	
	if(!(ccon = find_oscar_chat(gc, id)))
		return -1;
	  	
	for (s = message; *s; s++)
		if (*s & 128)
			break;
	  	
	/* Message contains high ASCII chars, time for some translation! */
	if (*s) {
		s = g_malloc(BUF_LONG);
		/* Try if we can put it in an ISO8859-1 string first.
		   If we can't, fall back to UTF16. */
		if ((ret = do_iconv("UTF-8", "ISO8859-1", message, s, len, BUF_LONG)) >= 0) {
			len = ret;
		} else if ((ret = do_iconv("UTF-8", "UNICODEBIG", message, s, len, BUF_LONG)) >= 0) {
			len = ret;
		} else {
			/* OOF, translation failed... Oh well.. */
			g_free( s );
			s = message;
		}
	} else {
		s = message;
	}
	  	
	ret = aim_chat_send_im(od->sess, ccon->conn, AIM_CHATFLAGS_NOREFLECT, s, len);
	  	
	if (s != message) {	
		g_free(s);
  }
  
  return (ret >= 0);
}

void oscar_chat_invite(struct gaim_connection * gc, int id, char *message, char *who)
{
	struct oscar_data * od = (struct oscar_data *)gc->proto_data;
	struct chat_connection *ccon = find_oscar_chat(gc, id);
	
	if (ccon == NULL)
		return;
	
	aim_chat_invite(od->sess, od->conn, who, message ? message : "",
					ccon->exchange, ccon->name, 0x0);
}

void oscar_chat_kill(struct gaim_connection *gc, struct chat_connection *cc)
{
	struct oscar_data *od = (struct oscar_data *)gc->proto_data;

	/* Notify the conversation window that we've left the chat */
	serv_got_chat_left(gc, cc->id);

	/* Destroy the chat_connection */
	od->oscar_chats = g_slist_remove(od->oscar_chats, cc);
	if (cc->inpa > 0)
		gaim_input_remove(cc->inpa);
	aim_conn_kill(od->sess, &cc->conn);
	g_free(cc->name);
	g_free(cc->show);
	g_free(cc);
}

void oscar_chat_leave(struct gaim_connection * gc, int id)
{
	struct chat_connection * ccon = find_oscar_chat(gc, id);

	if(ccon == NULL)
		return;

	oscar_chat_kill(gc, ccon);
}

int oscar_chat_join(struct gaim_connection * gc, char * name)
{
    struct oscar_data * od = (struct oscar_data *)gc->proto_data;
	
	aim_conn_t * cur;

	if((cur = aim_getconn_type(od->sess, AIM_CONN_TYPE_CHATNAV))) {
	
		return (aim_chatnav_createroom(od->sess, cur, name, 4) == 0);
	
	} else {
		struct create_room * cr = g_new0(struct create_room, 1);
		cr->exchange = 4;
		cr->name = g_strdup(name);
		od->create_rooms = g_slist_append(od->create_rooms, cr);
		aim_reqservice(od->sess, od->conn, AIM_CONN_TYPE_CHATNAV);
		return 1;
	}
}

int oscar_chat_open(struct gaim_connection * gc, char *who)
{
	struct oscar_data * od = (struct oscar_data *)gc->proto_data;
	int ret;
	static int chat_id = 0;
	char * chatname = g_new0(char, strlen(gc->username)+4);
	
	g_snprintf(chatname, strlen(gc->username) + 4, "%s%d", gc->username, chat_id++);
  
	ret = oscar_chat_join(gc, chatname);

	aim_chat_invite(od->sess, od->conn, who, "", 4, chatname, 0x0);

	g_free(chatname);
	
	return ret;
}

void oscar_accept_chat(gpointer w, struct aim_chat_invitation * inv)
{
	oscar_chat_join(inv->gc, inv->name);
	g_free(inv->name);
	g_free(inv);
}

void oscar_reject_chat(gpointer w, struct aim_chat_invitation * inv)
{
	g_free(inv->name);
	g_free(inv);
}

void oscar_init() 
{
	struct prpl *ret = g_new0(struct prpl, 1);
	ret->name = "oscar";
	ret->away_states = oscar_away_states;
	ret->login = oscar_login;
	ret->close = oscar_close;
	ret->send_im = oscar_send_im;
	ret->get_info = oscar_get_info;
	ret->set_away = oscar_set_away;
	ret->get_away = oscar_get_away;
	ret->add_buddy = oscar_add_buddy;
	ret->remove_buddy = oscar_remove_buddy;
	ret->chat_send = oscar_chat_send;
	ret->chat_invite = oscar_chat_invite;
	ret->chat_leave = oscar_chat_leave;
	ret->chat_open = oscar_chat_open;
	ret->add_permit = oscar_add_permit;
	ret->add_deny = oscar_add_deny;
	ret->rem_permit = oscar_rem_permit;
	ret->rem_deny = oscar_rem_deny;
	ret->set_permit_deny = oscar_set_permit_deny;
	ret->keepalive = oscar_keepalive;
	ret->cmp_buddynames = aim_sncmp;
	ret->get_status_string = oscar_get_status_string;
	ret->send_typing = oscar_send_typing;

	register_protocol(ret);
}
