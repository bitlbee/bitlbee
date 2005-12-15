  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2004 Wilmer van der Gaast and others                *
  \********************************************************************/

/*
 * nogaim
 *
 * Gaim without gaim - for BitlBee
 *
 * This file contains functions called by the Gaim IM-modules. It contains
 * some struct and type definitions from Gaim.
 *
 * Copyright (C) 1998-1999, Mark Spencer <markster@marko.net>
 *                          (and possibly other members of the Gaim team)
 * Copyright 2002-2004 Wilmer van der Gaast <lintux@lintux.cx>
 */

/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License with
  the Debian GNU/Linux distribution in /usr/share/common-licenses/GPL;
  if not, write to the Free Software Foundation, Inc., 59 Temple Place,
  Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef _NOGAIM_H
#define _NOGAIM_H

#include "bitlbee.h"
#include "proxy.h"
#include "md5.h"
#include "sha.h"


#define BUF_LEN MSG_LEN
#define BUF_LONG ( BUF_LEN * 2 )
#define MSG_LEN 2048
#define BUF_LEN MSG_LEN

#define SELF_ALIAS_LEN 400
#define BUDDY_ALIAS_MAXLEN 388   /* because MSN names can be 387 characters */

#define PERMIT_ALL      1
#define PERMIT_NONE     2
#define PERMIT_SOME     3
#define DENY_SOME       4

#define WEBSITE "http://www.bitlee.org/"
#define IM_FLAG_AWAY 0x0020
#define OPT_CONN_HTML 0x00000001
#define OPT_LOGGED_IN 0x00010000
#define GAIM_AWAY_CUSTOM "Custom"

#define GAIM_LOGO	0
#define GAIM_ERROR	1
#define GAIM_WARNING	2
#define GAIM_INFO	3

/* ok. now the fun begins. first we create a connection structure */
struct gaim_connection {
	/* we need to do either oscar or TOC */
	/* we make this as an int in case if we want to add more protocols later */
	struct prpl *prpl;
	guint32 flags;
	
	/* all connections need an input watcher */
	int inpa;
	
	/* buddy list stuff. there is still a global groups for the buddy list, but
	 * we need to maintain our own set of buddies, and our own permit/deny lists */
	GSList *permit;
	GSList *deny;
	int permdeny;
	
	/* all connections need a list of chats, even if they don't have chat */
	GSList *buddy_chats;
	
	/* each connection then can have its own protocol-specific data */
	void *proto_data;
	
	struct aim_user *user;
	
	char username[64];
	char displayname[128];
	char password[32];
	guint keepalive;
	/* stuff needed for per-connection idle times */
	guint idle_timer;
	time_t login_time;
	time_t lastsent;
	int is_idle;
	
	char *away;
	int is_auto_away;
	
	int evil;
	gboolean wants_to_die; /* defaults to FALSE */
	
	/* BitlBee */
	irc_t *irc;
	int lstitems;  /* added for msnP8 */
	
	struct conversation *conversations;
};

/* struct buddy_chat went away and got merged with this. */
struct conversation {
	struct gaim_connection *gc;

	/* stuff used just for chat */
        GList *in_room;
        GList *ignored;
        int id;
        
        /* BitlBee */
        struct conversation *next;
        char *channel;
        char *title;
        char joined;
        void *data;
};

struct buddy {
	char name[80];
	char show[BUDDY_ALIAS_MAXLEN];
        int present;
	int evil;
	time_t signon;
	time_t idle;
        int uc;
	guint caps; /* woohoo! */
	void *proto_data; /* what a hack */
	struct gaim_connection *gc; /* the connection it belongs to */
};

struct aim_user {
	char username[64];
	char alias[SELF_ALIAS_LEN]; 
	char password[32];
	char user_info[2048];
	int options;
	struct prpl *prpl;
	/* prpls can use this to save information about the user,
	 * like which server to connect to, etc */
	char proto_opt[7][256];

	struct gaim_connection *gc;
	irc_t *irc;
};

struct prpl {
	int options;
	const char *name;

	/* for ICQ and Yahoo, who have off/on per-conversation options */
	/* char *checkbox; this should be per-connection */

	GList *(* away_states)(struct gaim_connection *gc);
	GList *(* actions)();
	void   (* do_action)(struct gaim_connection *, char *);
	/* user_opts returns a GList* of g_malloc'd struct proto_user_opts */
	GList *(* user_opts)();
	GList *(* chat_info)(struct gaim_connection *);

	/* all the server-related functions */

	/* a lot of these (like get_dir) are protocol-dependent and should be removed. ones like
	 * set_dir (which is also protocol-dependent) can stay though because there's a dialog
	 * (i.e. the prpl says you can set your dir info, the ui shows a dialog and needs to call
	 * set_dir in order to set it) */

	void (* login)		(struct aim_user *);
	void (* close)		(struct gaim_connection *);
	int  (* send_im)	(struct gaim_connection *, char *who, char *message, int len, int away);
	int  (* send_typing)	(struct gaim_connection *, char *who, int typing);
	void (* set_info)	(struct gaim_connection *, char *info);
	void (* get_info)	(struct gaim_connection *, char *who);
	void (* set_away)	(struct gaim_connection *, char *state, char *message);
	void (* get_away)       (struct gaim_connection *, char *who);
	void (* set_idle)	(struct gaim_connection *, int idletime);
	void (* add_buddy)	(struct gaim_connection *, char *name);
	void (* remove_buddy)	(struct gaim_connection *, char *name, char *group);
	void (* add_permit)	(struct gaim_connection *, char *name);
	void (* add_deny)	(struct gaim_connection *, char *name);
	void (* rem_permit)	(struct gaim_connection *, char *name);
	void (* rem_deny)	(struct gaim_connection *, char *name);
	void (* set_permit_deny)(struct gaim_connection *);
	void (* join_chat)	(struct gaim_connection *, GList *data);
	void (* chat_invite)	(struct gaim_connection *, int id, char *who, char *message);
	void (* chat_leave)	(struct gaim_connection *, int id);
	void (* chat_whisper)	(struct gaim_connection *, int id, char *who, char *message);
	int  (* chat_send)	(struct gaim_connection *, int id, char *message);
	int  (* chat_open)	(struct gaim_connection *, char *who);
	void (* keepalive)	(struct gaim_connection *);

	/* get "chat buddy" info and away message */
	void (* get_cb_info)	(struct gaim_connection *, int, char *who);
	void (* get_cb_away)	(struct gaim_connection *, int, char *who);

	/* save/store buddy's alias on server list/roster */
	void (* alias_buddy)	(struct gaim_connection *, char *who);

	/* change a buddy's group on a server list/roster */
	void (* group_buddy)	(struct gaim_connection *, char *who, char *old_group, char *new_group);

	void (* buddy_free)	(struct buddy *);

	char *(* get_status_string) (struct gaim_connection *gc, int stat);

	int (* cmp_buddynames) (const char *who1, const char *who2);
};

#define UC_UNAVAILABLE  1

/* JABBER */
#define UC_AWAY (0x02 | UC_UNAVAILABLE)
#define UC_CHAT  0x04
#define UC_XA   (0x08 | UC_UNAVAILABLE)
#define UC_DND  (0x10 | UC_UNAVAILABLE)

G_MODULE_EXPORT GSList *get_connections();
G_MODULE_EXPORT struct prpl *find_protocol(const char *name);
G_MODULE_EXPORT void register_protocol(struct prpl *);

/* nogaim.c */
int serv_send_im(irc_t *irc, user_t *u, char *msg, int flags);
int serv_send_chat(irc_t *irc, struct gaim_connection *gc, int id, char *msg );

G_MODULE_EXPORT signed int do_iconv( char *from_cs, char *to_cs, char *src, char *dst, size_t size, size_t maxbuf );
char *set_eval_charset( irc_t *irc, set_t *set, char *value );

void nogaim_init();
int proto_away( struct gaim_connection *gc, char *away );
char *set_eval_away_devoice( irc_t *irc, set_t *set, char *value );

gboolean auto_reconnect( gpointer data );
void cancel_auto_reconnect( struct account *a );

/* multi.c */
G_MODULE_EXPORT struct gaim_connection *new_gaim_conn( struct aim_user *user );
G_MODULE_EXPORT void destroy_gaim_conn( struct gaim_connection *gc );
G_MODULE_EXPORT void set_login_progress( struct gaim_connection *gc, int step, char *msg );
G_MODULE_EXPORT void hide_login_progress( struct gaim_connection *gc, char *msg );
G_MODULE_EXPORT void hide_login_progress_error( struct gaim_connection *gc, char *msg );
G_MODULE_EXPORT void serv_got_crap( struct gaim_connection *gc, char *format, ... );
G_MODULE_EXPORT void account_online( struct gaim_connection *gc );
G_MODULE_EXPORT void account_offline( struct gaim_connection *gc );
G_MODULE_EXPORT void signoff( struct gaim_connection *gc );

/* dialogs.c */
G_MODULE_EXPORT void do_error_dialog( struct gaim_connection *gc, char *msg, char *title );
G_MODULE_EXPORT void do_ask_dialog( struct gaim_connection *gc, char *msg, void *data, void *doit, void *dont );

/* list.c */
G_MODULE_EXPORT int bud_list_cache_exists( struct gaim_connection *gc );
G_MODULE_EXPORT void do_import( struct gaim_connection *gc, void *null );
G_MODULE_EXPORT void add_buddy( struct gaim_connection *gc, char *group, char *handle, char *realname );
G_MODULE_EXPORT struct buddy *find_buddy( struct gaim_connection *gc, char *handle );
G_MODULE_EXPORT void do_export( struct gaim_connection *gc );
G_MODULE_EXPORT void signoff_blocked( struct gaim_connection *gc );

G_MODULE_EXPORT void serv_buddy_rename( struct gaim_connection *gc, char *handle, char *realname );

/* buddy_chat.c */
G_MODULE_EXPORT void add_chat_buddy( struct conversation *b, char *handle );
G_MODULE_EXPORT void remove_chat_buddy( struct conversation *b, char *handle, char *reason );

/* prpl.c */
G_MODULE_EXPORT void show_got_added( struct gaim_connection *gc, char *id, char *handle, const char *realname, const char *msg );

/* server.c */                    
G_MODULE_EXPORT void serv_got_update( struct gaim_connection *gc, char *handle, int loggedin, int evil, time_t signon, time_t idle, int type, guint caps );
G_MODULE_EXPORT void serv_got_im( struct gaim_connection *gc, char *handle, char *msg, guint32 flags, time_t mtime, gint len );
G_MODULE_EXPORT void serv_got_typing( struct gaim_connection *gc, char *handle, int timeout, int type );
G_MODULE_EXPORT void serv_got_chat_invite( struct gaim_connection *gc, char *handle, char *who, char *msg, GList *data );
G_MODULE_EXPORT struct conversation *serv_got_joined_chat( struct gaim_connection *gc, int id, char *handle );
G_MODULE_EXPORT void serv_got_chat_in( struct gaim_connection *gc, int id, char *who, int whisper, char *msg, time_t mtime );
G_MODULE_EXPORT void serv_got_chat_left( struct gaim_connection *gc, int id );
/* void serv_finish_login( struct gaim_connection *gc ); */

/* util.c */
G_MODULE_EXPORT char *utf8_to_str( const char *in );
G_MODULE_EXPORT char *str_to_utf8( const char *in );
G_MODULE_EXPORT void strip_linefeed( gchar *text );
G_MODULE_EXPORT char *add_cr( char *text );
G_MODULE_EXPORT char *tobase64( const char *text );
G_MODULE_EXPORT char *normalize( const char *s );
G_MODULE_EXPORT time_t get_time( int year, int month, int day, int hour, int min, int sec );
G_MODULE_EXPORT void strip_html( char *msg );
G_MODULE_EXPORT char * escape_html(const char *html);
G_MODULE_EXPORT void info_string_append(GString *str, char *newline, char *name, char *value);

/* prefs.c */
G_MODULE_EXPORT void build_block_list();
G_MODULE_EXPORT void build_allow_list();

struct conversation *conv_findchannel( char *channel );


#endif
