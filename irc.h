/********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2013 Wilmer van der Gaast and others                *
  \********************************************************************/

/* The IRC-based UI (for now the only one)                              */

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
  if not, write to the Free Software Foundation, Inc., 51 Franklin St.,
  Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef _IRC_H
#define _IRC_H

#define IRC_MAX_LINE 512
#define IRC_MAX_ARGS 16

#define IRC_LOGIN_TIMEOUT 60
#define IRC_PING_STRING "PinglBee"

#define UMODES "abisw"     /* Allowed umodes (although they mostly do nothing) */
#define UMODES_PRIV "Ro"   /* Allowed, but not by user directly */
#define UMODES_KEEP "R"    /* Don't allow unsetting using /MODE */
#define CMODES "ntC"       /* Allowed modes */
#define CMODE "t"          /* Default mode */
#define UMODE "s"          /* Default mode */

#define CTYPES "&#"        /* Valid channel name prefixes */

typedef enum {
	USTATUS_OFFLINE = 0,
	USTATUS_AUTHORIZED = 1, /* Gave the correct server password (PASS). */
	USTATUS_LOGGED_IN = 2,  /* USER+NICK(+PASS) finished. */
	USTATUS_IDENTIFIED = 4, /* To NickServ (root). */
	USTATUS_SHUTDOWN = 8,   /* Now used to indicate we're shutting down.
	                           Currently just blocks irc_vawrite(). */

	/* Not really status stuff, but other kinds of flags: For slightly
	   better password security, since the only way to send passwords
	   to the IRC server securely (i.e. not echoing to screen or written
	   to logfiles) is the /OPER command, try to use that command for
	   stuff that matters. */
	OPER_HACK_IDENTIFY = 0x100,
	OPER_HACK_IDENTIFY_NOLOAD = 0x01100,
	OPER_HACK_IDENTIFY_FORCE  = 0x02100,
	OPER_HACK_REGISTER = 0x200,
	OPER_HACK_ACCOUNT_PASSWORD = 0x400,
	OPER_HACK_ANY = 0x3700, /* To check for them all at once. */

	IRC_UTF8_NICKS = 0x10000, /* Disable ASCII restrictions on buddy nicks. */
} irc_status_t;

struct irc_user;

typedef struct irc {
	int fd;
	irc_status_t status;
	double last_pong;
	int pinging;
	char *sendbuffer;
	char *readbuffer;
	GIConv iconv, oconv;

	struct irc_user *root;
	struct irc_user *user;

	char *password; /* HACK: Used to save the user's password, but before
	                   logging in, this may contain a password we should
	                   send to identify after USER/NICK are received. */

	char umode[8];

	struct query *queries;
	GSList *file_transfers;

	GSList *users, *channels;
	struct irc_channel *default_channel;
	GHashTable *nick_user_hash;
	GHashTable *watches; /* See irc_cmd_watch() */

	gint r_watch_source_id;
	gint w_watch_source_id;
	gint ping_source_id;
	gint login_source_id; /* To slightly delay some events at login time. */

	struct otr *otr; /* OTR state and book keeping, used by the OTR plugin.
	                    TODO: Some mechanism for plugindata. */

	struct bee *b;
} irc_t;

typedef enum {
	/* Replaced with iu->last_channel IRC_USER_PRIVATE = 1, */
	IRC_USER_AWAY = 2,

	IRC_USER_OTR_ENCRYPTED = 0x10000,
	IRC_USER_OTR_TRUSTED   = 0x20000,
} irc_user_flags_t;

typedef struct irc_user {
	irc_t *irc;

	char *nick;
	char *user;
	char *host;
	char *fullname;

	/* Nickname in lowercase for case insensitive searches */
	char *key;

	irc_user_flags_t flags;
	struct irc_channel *last_channel;

	GString *pastebuf; /* Paste buffer (combine lines into a multiline msg). */
	guint pastebuf_timer;
	time_t away_reply_timeout; /* Only send a 301 if this time passed. */

	struct bee_user *bu;

	const struct irc_user_funcs *f;
} irc_user_t;

struct irc_user_funcs {
	gboolean (*privmsg)(irc_user_t *iu, const char *msg);
	gboolean (*ctcp)(irc_user_t *iu, char * const* ctcp);
};

extern const struct irc_user_funcs irc_user_root_funcs;
extern const struct irc_user_funcs irc_user_self_funcs;

typedef enum {
	IRC_CHANNEL_JOINED = 1, /* The user is currently in the channel. */
	IRC_CHANNEL_TEMP = 2,   /* Erase the channel when the user leaves,
	                           and don't save it. */

	/* Hack: Set this flag right before jumping into IM when we expect
	   a call to imcb_chat_new(). */
	IRC_CHANNEL_CHAT_PICKME = 0x10000,
} irc_channel_flags_t;

typedef struct irc_channel {
	irc_t *irc;
	char *name;
	char mode[8];
	int flags;

	char *topic;
	char *topic_who;
	time_t topic_time;

	GSList *users; /* struct irc_channel_user */
	struct irc_user *last_target;
	struct set *set;

	GString *pastebuf; /* Paste buffer (combine lines into a multiline msg). */
	guint pastebuf_timer;

	const struct irc_channel_funcs *f;
	void *data;
} irc_channel_t;

struct irc_channel_funcs {
	gboolean (*privmsg)(irc_channel_t *ic, const char *msg);
	gboolean (*join)(irc_channel_t *ic);
	gboolean (*part)(irc_channel_t *ic, const char *msg);
	gboolean (*topic)(irc_channel_t *ic, const char *new_topic);
	gboolean (*invite)(irc_channel_t *ic, irc_user_t *iu);
	void (*kick)(irc_channel_t *ic, irc_user_t *iu, const char *msg);

	gboolean (*_init)(irc_channel_t *ic);
	gboolean (*_free)(irc_channel_t *ic);
};

typedef enum {
	IRC_CHANNEL_USER_OP = 1,
	IRC_CHANNEL_USER_HALFOP = 2,
	IRC_CHANNEL_USER_VOICE = 4,
	IRC_CHANNEL_USER_NONE = 8,
} irc_channel_user_flags_t;

typedef struct irc_channel_user {
	irc_user_t *iu;
	int flags;
} irc_channel_user_t;

typedef enum {
	IRC_CC_TYPE_DEFAULT  = 0x00001,
	IRC_CC_TYPE_REST     = 0x00002, /* Still not implemented. */
	IRC_CC_TYPE_GROUP    = 0x00004,
	IRC_CC_TYPE_ACCOUNT  = 0x00008,
	IRC_CC_TYPE_PROTOCOL = 0x00010,
	IRC_CC_TYPE_MASK     = 0x000ff,
	IRC_CC_TYPE_INVERT   = 0x00100,
} irc_control_channel_type_t;

struct irc_control_channel {
	irc_control_channel_type_t type;
	struct bee_group *group;
	struct account *account;
	struct prpl *protocol;
	char modes[5];
};

extern const struct bee_ui_funcs irc_ui_funcs;

typedef enum {
	IRC_CDU_SILENT,
	IRC_CDU_PART,
	IRC_CDU_KICK,
} irc_channel_del_user_type_t;

/* These are a glued a little bit to the core/bee layer and a little bit to
   IRC. The first user is OTR, and I guess at some point we'll get to shape
   this a little bit more as other uses come up. */
typedef struct irc_plugin {
	/* Called at the end of irc_new(). Can be used to add settings, etc. */
	gboolean (*irc_new)(irc_t *irc);
	/* At the end of irc_free(). */
	void (*irc_free)(irc_t *irc);

	/* Problem with the following two functions is ordering if multiple
	   plugins are handling them. Let's keep fixing that problem for
	   whenever it becomes important. */

	/* Called by bee_irc_user_privmsg_cb(). Return NULL if you want to
	   abort sending the msg. */
	char* (*filter_msg_out)(irc_user_t * iu, char *msg, int flags);
	/* Called by bee_irc_user_msg(). Return NULL if you swallowed the
	   message and don't want anything to go to the user. */
	char* (*filter_msg_in)(irc_user_t * iu, char *msg, int flags);

	/* From storage.c functions. Ideally these should not be used
	   and instead data should be stored in settings which will get
	   saved automatically. Consider these deprecated! */
	void (*storage_load)(irc_t *irc);
	void (*storage_save)(irc_t *irc);
	void (*storage_remove)(const char *nick);
} irc_plugin_t;

extern GSList *irc_plugins; /* struct irc_plugin */

/* irc.c */
extern GSList *irc_connection_list;

irc_t *irc_new(int fd);
void irc_abort(irc_t *irc, int immed, char *format, ...) G_GNUC_PRINTF(3, 4);
void irc_free(irc_t *irc);
void irc_setpass(irc_t *irc, const char *pass);

void irc_process(irc_t *irc);
char **irc_parse_line(char *line);
char *irc_build_line(char **cmd);

void irc_write(irc_t *irc, char *format, ...) G_GNUC_PRINTF(2, 3);
void irc_write_all(int now, char *format, ...) G_GNUC_PRINTF(2, 3);
void irc_vawrite(irc_t *irc, char *format, va_list params);

void irc_flush(irc_t *irc);
void irc_switch_fd(irc_t *irc, int fd);
void irc_sync(irc_t *irc);
void irc_desync(irc_t *irc);

int irc_check_login(irc_t *irc);

void irc_umode_set(irc_t *irc, const char *s, gboolean allow_priv);

void register_irc_plugin(const struct irc_plugin *p);

/* irc_channel.c */
irc_channel_t *irc_channel_new(irc_t *irc, const char *name);
irc_channel_t *irc_channel_by_name(irc_t *irc, const char *name);
irc_channel_t *irc_channel_get(irc_t *irc, char *id);
int irc_channel_free(irc_channel_t *ic);
void irc_channel_free_soon(irc_channel_t *ic);
int irc_channel_add_user(irc_channel_t *ic, irc_user_t *iu);
int irc_channel_del_user(irc_channel_t *ic, irc_user_t *iu, irc_channel_del_user_type_t type, const char *msg);
irc_channel_user_t *irc_channel_has_user(irc_channel_t *ic, irc_user_t *iu);
struct irc_channel *irc_channel_with_user(irc_t *irc, irc_user_t *iu);
int irc_channel_set_topic(irc_channel_t *ic, const char *topic, const irc_user_t *who);
void irc_channel_user_set_mode(irc_channel_t *ic, irc_user_t *iu, irc_channel_user_flags_t flags);
void irc_channel_set_mode(irc_channel_t *ic, const char *s);
void irc_channel_auto_joins(irc_t *irc, struct account *acc);
void irc_channel_printf(irc_channel_t *ic, char *format, ...);
gboolean irc_channel_name_ok(const char *name);
void irc_channel_name_strip(char *name);
int irc_channel_name_cmp(const char *a_, const char *b_);
char *irc_channel_name_gen(irc_t *irc, const char *name);
gboolean irc_channel_name_hint(irc_channel_t *ic, const char *name);
void irc_channel_update_ops(irc_channel_t *ic, char *value);
char *set_eval_irc_channel_ops(struct set *set, char *value);
gboolean irc_channel_wants_user(irc_channel_t *ic, irc_user_t *iu);

/* irc_commands.c */
void irc_exec(irc_t *irc, char **cmd);

/* irc_send.c */
void irc_send_num(irc_t *irc, int code, char *format, ...) G_GNUC_PRINTF(3, 4);
void irc_send_login(irc_t *irc);
void irc_send_motd(irc_t *irc);
const char *irc_user_msgdest(irc_user_t *iu);
void irc_rootmsg(irc_t *irc, char *format, ...);
void irc_usermsg(irc_user_t *iu, char *format, ...);
void irc_usernotice(irc_user_t *iu, char *format, ...);
void irc_send_join(irc_channel_t *ic, irc_user_t *iu);
void irc_send_part(irc_channel_t *ic, irc_user_t *iu, const char *reason);
void irc_send_quit(irc_user_t *iu, const char *reason);
void irc_send_kick(irc_channel_t *ic, irc_user_t *iu, irc_user_t *kicker, const char *reason);
void irc_send_names(irc_channel_t *ic);
void irc_send_topic(irc_channel_t *ic, gboolean topic_change);
void irc_send_whois(irc_user_t *iu);
void irc_send_who(irc_t *irc, GSList *l, const char *channel);
void irc_send_msg(irc_user_t *iu, const char *type, const char *dst, const char *msg, const char *prefix);
void irc_send_msg_raw(irc_user_t *iu, const char *type, const char *dst, const char *msg);
void irc_send_msg_f(irc_user_t *iu, const char *type, const char *dst, const char *format, ...) G_GNUC_PRINTF(4, 5);
void irc_send_nick(irc_user_t *iu, const char *new_nick);
void irc_send_channel_user_mode_diff(irc_channel_t *ic, irc_user_t *iu,
                                     irc_channel_user_flags_t old_flags, irc_channel_user_flags_t new_flags);
void irc_send_invite(irc_user_t *iu, irc_channel_t *ic);

/* irc_user.c */
irc_user_t *irc_user_new(irc_t *irc, const char *nick);
int irc_user_free(irc_t *irc, irc_user_t *iu);
irc_user_t *irc_user_by_name(irc_t *irc, const char *nick);
int irc_user_set_nick(irc_user_t *iu, const char *new_nick);
gint irc_user_cmp(gconstpointer a_, gconstpointer b_);
const char *irc_user_get_away(irc_user_t *iu);
void irc_user_quit(irc_user_t *iu, const char *msg);

/* irc_util.c */
char *set_eval_timezone(struct set *set, char *value);
char *irc_format_timestamp(irc_t *irc, time_t msg_ts);

/* irc_im.c */
void bee_irc_channel_update(irc_t *irc, irc_channel_t *ic, irc_user_t *iu);
void bee_irc_user_nick_reset(irc_user_t *iu);

#endif
