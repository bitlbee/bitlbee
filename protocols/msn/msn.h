  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2012 Wilmer van der Gaast and others                *
  \********************************************************************/

/* MSN module                                                           */

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

#ifndef _MSN_H
#define _MSN_H

/* Some hackish magicstrings to make special-purpose messages/switchboards.
 */
#define TYPING_NOTIFICATION_MESSAGE "\r\r\rBEWARE, ME R TYPINK MESSAGE!!!!\r\r\r"
#define NUDGE_MESSAGE "\r\r\rSHAKE THAT THING\r\r\r"
#define GROUPCHAT_SWITCHBOARD_MESSAGE "\r\r\rME WANT TALK TO MANY PEOPLE\r\r\r"
#define SB_KEEPALIVE_MESSAGE "\r\r\rDONT HANG UP ON ME!\r\r\r"

#ifdef DEBUG_MSN
#define debug( text... ) imcb_log( ic, text );
#else
#define debug( text... )
#endif

/* This should be MSN Messenger 7.0.0813
#define MSNP11_PROD_KEY "CFHUR$52U_{VIX5T"
#define MSNP11_PROD_ID  "PROD0101{0RM?UBW"
*/

#define MSN_NS_HOST "messenger.hotmail.com"
#define MSN_NS_PORT 1863

/* Some other version.
#define MSNP11_PROD_KEY "O4BG@C7BWLYQX?5G"
#define MSNP11_PROD_ID  "PROD01065C%ZFN6F"
*/

/* <= BitlBee 3.0.5
#define MSNP11_PROD_KEY "ILTXC!4IXB5FB*PX"
#define MSNP11_PROD_ID  "PROD0119GSJUC$18"
*/

#define MSNP11_PROD_KEY "C1BX{V4W}Q3*10SM"
#define MSNP11_PROD_ID  "PROD0120PW!CCV9@"
#define MSNP_VER        "MSNP18"
#define MSNP_BUILD      "14.0.8117.416"

#define MSN_SB_NEW         -24062002

#define MSN_CAP1        0xC000
#define MSN_CAP2        0x0000

#define MSN_MESSAGE_HEADERS "MIME-Version: 1.0\r\n" \
                            "Content-Type: text/plain; charset=UTF-8\r\n" \
                            "User-Agent: BitlBee " BITLBEE_VERSION "\r\n" \
                            "X-MMS-IM-Format: FN=MS%20Shell%20Dlg; EF=; CO=0; CS=0; PF=0\r\n" \
                            "\r\n"

#define MSN_TYPING_HEADERS "MIME-Version: 1.0\r\n" \
                           "Content-Type: text/x-msmsgscontrol\r\n" \
                           "TypingUser: %s\r\n" \
                           "\r\n\r\n"

#define MSN_NUDGE_HEADERS "MIME-Version: 1.0\r\n" \
			  "Content-Type: text/x-msnmsgr-datacast\r\n" \
			  "\r\n" \
			  "ID: 1\r\n" \
			  "\r\n"

#define MSN_SB_KEEPALIVE_HEADERS "MIME-Version: 1.0\r\n" \
                                 "Content-Type: text/x-ping\r\n" \
                                 "\r\n\r\n"

#define PROFILE_URL "http://members.msn.com/"

typedef enum
{
	MSN_GOT_PROFILE = 1,
	MSN_GOT_PROFILE_DN = 2,
	MSN_DONE_ADL = 4,
	MSN_REAUTHING = 8,
	MSN_EMAIL_UNVERIFIED = 16,
} msn_flags_t;

struct msn_handler_data
{
	int fd, inpa;
	int rxlen;
	char *rxq;
	
	int msglen;
	char *cmd_text;
	
	/* Either ic or sb */
	gpointer data;
	
	int (*exec_command) ( struct msn_handler_data *handler, char **cmd, int count );
	int (*exec_message) ( struct msn_handler_data *handler, char *msg, int msglen, char **cmd, int count );
};

struct msn_data
{
	struct im_connection *ic;
	
	struct msn_handler_data ns[1];
	msn_flags_t flags;
	
	int trId;
	char *tokens[4];
	char *lock_key, *pp_policy;
	char *uuid;
	
	GSList *msgq, *grpq, *soapq;
	GSList *switchboards;
	int sb_failures;
	time_t first_sb_failure;
	
	const struct msn_away_state *away_state;
	GSList *groups;
	char *profile_rid;
	
	/* Mostly used for sending the ADL command; since MSNP13 the client
	   is responsible for downloading the contact list and then sending
	   it to the MSNP server. */
	GTree *domaintree;
	int adl_todo;
};

struct msn_switchboard
{
	struct im_connection *ic;
	
	/* The following two are also in the handler. TODO: Clean up. */
	int fd;
	gint inp;
	struct msn_handler_data *handler;
	gint keepalive;
	
	int trId;
	int ready;
	
	int session;
	char *key;
	
	GSList *msgq;
	char *who;
	struct groupchat *chat;
};

struct msn_away_state
{
	char code[4];
	char name[16];
};

struct msn_status_code
{
	int number;
	char *text;
	int flags;
};

struct msn_message
{
	char *who;
	char *text;
};

struct msn_groupadd
{
	char *who;
	char *group;
};

typedef enum
{
	MSN_BUDDY_FL = 1,   /* Warning: FL,AL,BL *must* be 1,2,4. */
	MSN_BUDDY_AL = 2,
	MSN_BUDDY_BL = 4,
	MSN_BUDDY_RL = 8,
	MSN_BUDDY_PL = 16,
	MSN_BUDDY_ADL_SYNCED = 256,
} msn_buddy_flags_t;

struct msn_buddy_data
{
	char *cid;
	msn_buddy_flags_t flags;
};

struct msn_group
{
	char *name;
	char *id;
};

/* Bitfield values for msn_status_code.flags */
#define STATUS_FATAL            1
#define STATUS_SB_FATAL         2
#define STATUS_SB_IM_SPARE	4	/* Make one-to-one conversation switchboard available again, invite failed. */
#define STATUS_SB_CHAT_SPARE	8	/* Same, but also for groupchats (not used yet). */

extern int msn_chat_id;
extern const struct msn_away_state msn_away_state_list[];
extern const struct msn_status_code msn_status_code_list[];

/* Keep a list of all the active connections. We need these lists because
   "connected" callbacks might be called when the connection they belong too
   is down already (for example, when an impatient user disabled the
   connection), the callback should check whether it's still listed here
   before doing *anything* else. */
extern GSList *msn_connections;
extern GSList *msn_switchboards;

/* ns.c */
int msn_ns_write( struct im_connection *ic, int fd, const char *fmt, ... ) G_GNUC_PRINTF( 3, 4 );
gboolean msn_ns_connect( struct im_connection *ic, struct msn_handler_data *handler, const char *host, int port );
void msn_ns_close( struct msn_handler_data *handler );
void msn_auth_got_passport_token( struct im_connection *ic, const char *token, const char *error );
void msn_auth_got_contact_list( struct im_connection *ic );
int msn_ns_finish_login( struct im_connection *ic );
int msn_ns_sendmessage( struct im_connection *ic, struct bee_user *bu, const char *text );
void msn_ns_oim_send_queue( struct im_connection *ic, GSList **msgq );

/* msn_util.c */
int msn_logged_in( struct im_connection *ic );
int msn_buddy_list_add( struct im_connection *ic, msn_buddy_flags_t list, const char *who, const char *realname_, const char *group );
int msn_buddy_list_remove( struct im_connection *ic, msn_buddy_flags_t list, const char *who, const char *group );
void msn_buddy_ask( bee_user_t *bu );
char **msn_linesplit( char *line );
int msn_handler( struct msn_handler_data *h );
void msn_msgq_purge( struct im_connection *ic, GSList **list );
char *msn_p11_challenge( char *challenge );
gint msn_domaintree_cmp( gconstpointer a_, gconstpointer b_ );
struct msn_group *msn_group_by_name( struct im_connection *ic, const char *name );
struct msn_group *msn_group_by_id( struct im_connection *ic, const char *id );
int msn_ns_set_display_name( struct im_connection *ic, const char *value );
const char *msn_normalize_handle( const char *handle );

/* tables.c */
const struct msn_away_state *msn_away_state_by_number( int number );
const struct msn_away_state *msn_away_state_by_code( char *code );
const struct msn_away_state *msn_away_state_by_name( char *name );
const struct msn_status_code *msn_status_by_number( int number );

/* sb.c */
int msn_sb_write( struct msn_switchboard *sb, const char *fmt, ... ) G_GNUC_PRINTF( 2, 3 );;
struct msn_switchboard *msn_sb_create( struct im_connection *ic, char *host, int port, char *key, int session );
struct msn_switchboard *msn_sb_by_handle( struct im_connection *ic, const char *handle );
struct msn_switchboard *msn_sb_by_chat( struct groupchat *c );
struct msn_switchboard *msn_sb_spare( struct im_connection *ic );
int msn_sb_sendmessage( struct msn_switchboard *sb, char *text );
struct groupchat *msn_sb_to_chat( struct msn_switchboard *sb );
void msn_sb_destroy( struct msn_switchboard *sb );
gboolean msn_sb_connected( gpointer data, gint source, b_input_condition cond );
int msn_sb_write_msg( struct im_connection *ic, struct msn_message *m );
void msn_sb_start_keepalives( struct msn_switchboard *sb, gboolean initial );
void msn_sb_stop_keepalives( struct msn_switchboard *sb );

#endif //_MSN_H
