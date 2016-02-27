/********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2012 Wilmer van der Gaast and others                *
  \********************************************************************/

/* Misc. functions                                                      */

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

#ifndef _MISC_H
#define _MISC_H

#include <gmodule.h>
#include <time.h>
#include <sys/socket.h>

struct ns_srv_reply {
	int prio;
	int weight;
	int port;
	char name[];
};

#ifndef NAMESER_HAS_NS_TYPES

#define NS_MAXDNAME 1025
#define NS_INT16SZ  2
#define NS_INT32SZ  4

#define NS_GET16(s, cp) do { \
		register const unsigned char *t_cp = (const unsigned char *) (cp); \
		(s) = ((guint16) t_cp[0] << 8) \
		      | ((guint16) t_cp[1]) \
		; \
		(cp) += NS_INT16SZ; \
} while (0)

#define NS_GET32(s, cp) do { \
		register const unsigned char *t_cp = (const unsigned char *) (cp); \
		(s) = ((guint16) t_cp[0] << 24) \
		      | ((guint16) t_cp[1] << 16) \
		      | ((guint16) t_cp[2] << 8) \
		      | ((guint16) t_cp[3]) \
		; \
		(cp) += NS_INT32SZ; \
} while (0)

#define ns_rr_rdlen(rr) ((rr).rdlength + 0)
#define ns_rr_rdata(rr) ((rr).rdata + 0)

struct _ns_flagdata { int mask, shift; };

typedef struct __ns_rr {
	char name[NS_MAXDNAME];
	guint16 type;
	guint16 rr_class;
	guint32 ttl;
	guint16 rdlength;
	const unsigned char* rdata;
} ns_rr;

typedef enum __ns_sect {
	ns_s_qd = 0,
	ns_s_zn = 0,
	ns_s_an = 1,
	ns_s_pr = 1,
	ns_s_ns = 2,
	ns_s_ud = 2,
	ns_s_ar = 3,
	ns_s_max = 4
} ns_sect;

typedef struct __ns_msg {
	const unsigned char* _msg;
	const unsigned char* _eom;
	guint16 _id;
	guint16 _flags;
	guint16 _counts[ns_s_max];
	const unsigned char* _sections[ns_s_max];
	ns_sect _sect;
	int _rrnum;
	const unsigned char* _msg_ptr;
} ns_msg;

typedef enum __ns_class {
	ns_c_invalid = 0,
	ns_c_in = 1,
	ns_c_2 = 2,
	ns_c_chaos = 3,
	ns_c_hs = 4,
	ns_c_none = 254,
	ns_c_any = 255,
	ns_c_max = 65536
} ns_class;


/* TODO : fill out the rest */
typedef enum __ns_type {
	ns_t_srv = 33
} ns_type;

#endif /* NAMESER_HAS_NS_INITPARSE */

G_MODULE_EXPORT void strip_linefeed(gchar *text);
G_MODULE_EXPORT char *add_cr(char *text);
G_MODULE_EXPORT char *strip_newlines(char *source);

G_MODULE_EXPORT time_t get_time(int year, int month, int day, int hour, int min, int sec);
G_MODULE_EXPORT time_t mktime_utc(struct tm *tp);
double gettime(void);

G_MODULE_EXPORT void strip_html(char *msg);
G_MODULE_EXPORT char *escape_html(const char *html);
G_MODULE_EXPORT void http_decode(char *s);
G_MODULE_EXPORT void http_encode(char *s);

G_MODULE_EXPORT signed int do_iconv(char *from_cs, char *to_cs, char *src, char *dst, size_t size, size_t maxbuf);

G_MODULE_EXPORT void random_bytes(unsigned char *buf, int count);

G_MODULE_EXPORT int is_bool(char *value);
G_MODULE_EXPORT int bool2int(char *value);

G_MODULE_EXPORT struct ns_srv_reply **srv_lookup(char *service, char *protocol, char *domain);
G_MODULE_EXPORT void srv_free(struct ns_srv_reply **srv);

G_MODULE_EXPORT char *word_wrap(const char *msg, int line_len);
G_MODULE_EXPORT gboolean ssl_sockerr_again(void *ssl);
G_MODULE_EXPORT char **split_command_parts(char *command, int limit);
G_MODULE_EXPORT char *get_rfc822_header(const char *text, const char *header, int len);
G_MODULE_EXPORT int truncate_utf8(char *string, int maxlen);
G_MODULE_EXPORT gboolean parse_int64(char *string, int base, guint64 *number);
G_MODULE_EXPORT char *str_reject_chars(char *string, const char *reject, char replacement);
G_MODULE_EXPORT char *str_pad_and_truncate(const char *string, long char_len, const char *ellipsis);

G_MODULE_EXPORT int b_istr_equal(gconstpointer v, gconstpointer v2);
G_MODULE_EXPORT guint b_istr_hash(gconstpointer v);

#endif
