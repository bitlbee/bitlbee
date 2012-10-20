/*
 * libyahoo2: yahoo2.h
 *
 * Copyright (C) 2002-2004, Philip S Tellis <philip.tellis AT gmx.net>
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

#ifndef YAHOO2_H
#define YAHOO2_H

#ifdef __cplusplus
extern "C" {
#endif

/* *** BitlBee: *** */
#include "bitlbee.h"
#undef free
#define free( x )		g_free( x )
#undef malloc
#define malloc( x )		g_malloc( x )
#undef calloc
#define calloc( x, y )		g_calloc( x, y )
#undef realloc
#define realloc( x, y )		g_realloc( x, y )
#undef strdup
#define strdup( x )		g_strdup( x )
#undef strndup
#define strndup( x,y )		g_strndup( x,y )
#undef snprintf
// #define snprintf( x... )	g_snprintf( x )
#undef strcasecmp
#define strcasecmp( x,y )	g_strcasecmp( x,y )
#undef strncasecmp
#define strncasecmp( x,y,z )	g_strncasecmp( x,y,z )


#include "yahoo2_types.h"

/* returns the socket descriptor object for a given pager connection. shouldn't be needed */
	void *yahoo_get_fd(int id);

/* says how much logging to do */
/* see yahoo2_types.h for the different values */
	int yahoo_set_log_level(enum yahoo_log_level level);
	enum yahoo_log_level yahoo_get_log_level(void);

/* these functions should be self explanatory */
/* who always means the buddy you're acting on */
/* id is the successful value returned by yahoo_init */

/* init returns a connection id used to identify the connection hereon */
/* or 0 on failure */
/* you must call init before calling any other function */
/*
 * The optional parameters to init are key/value pairs that specify 
 * server settings to use.  This list must be NULL terminated - even
 * if the list is empty.  If a parameter isn't set, a default value
 * will be used.  Parameter keys are strings, parameter values are
 * either strings or ints, depending on the key.  Values passed in
 * are copied, so you can use const/auto/static/pointers/whatever
 * you want.  Parameters are:
 * 	NAME			TYPE		DEFAULT
 *	pager_host		char *		scs.msg.yahoo.com
 *	pager_port		int		5050
 *	filetransfer_host	char *		filetransfer.msg.yahoo.com
 *	filetransfer_port	int		80
 *	webcam_host		char *		webcam.yahoo.com
 *	webcam_port		int		5100
 *	webcam_description	char *		""
 *	local_host		char *		""
 *	conn_type		int		Y_WCM_DSL
 *
 * You should set at least local_host if you intend to use webcams
 */
	int yahoo_init_with_attributes(const char *username,
		const char *password, ...);

/* yahoo_init does the same as yahoo_init_with_attributes, assuming defaults
 * for all attributes */
	int yahoo_init(const char *username, const char *password);

/* release all resources held by this session */
/* you need to call yahoo_close for a session only if
 * yahoo_logoff is never called for it (ie, it was never logged in) */
	void yahoo_close(int id);
/* login logs in to the server */
/* initial is of type enum yahoo_status.  see yahoo2_types.h */
	void yahoo_login(int id, int initial);
	void yahoo_logoff(int id);
/* reloads status of all buddies */
	void yahoo_refresh(int id);
/* activates/deactivates an identity */
	void yahoo_set_identity_status(int id, const char *identity,
		int active);
/* regets the entire buddy list from the server */
	void yahoo_get_list(int id);
/* download buddy contact information from your yahoo addressbook */
	void yahoo_get_yab(int id);
/* add/modify an address book entry.  if yab->dbid is set, it will */
/* modify that entry else it creates a new entry */
	void yahoo_set_yab(int id, struct yab *yab);
	void yahoo_keepalive(int id);
	void yahoo_chat_keepalive(int id);

/* from is the identity you're sending from.  if NULL, the default is used */
/* utf8 is whether msg is a utf8 string or not. */
	void yahoo_send_im(int id, const char *from, const char *who,
		const char *msg, int utf8, int picture);
//	void yahoo_send_buzz(int id, const char *from, const char *who);
/* if type is true, send typing notice, else send stopped typing notice */
	void yahoo_send_typing(int id, const char *from, const char *who,
		int typ);

/* used to set away/back status. */
/* away says whether the custom message is an away message or a sig */
	void yahoo_set_away(int id, enum yahoo_status state, const char *msg,
		int away);

	void yahoo_add_buddy(int id, const char *who, const char *group,
		const char *msg);
	void yahoo_remove_buddy(int id, const char *who, const char *group);
	void yahoo_confirm_buddy(int id, const char *who, int reject,
		const char *msg);
	void yahoo_stealth_buddy(int id, const char *who, int unstealth);
/* if unignore is true, unignore, else ignore */
	void yahoo_ignore_buddy(int id, const char *who, int unignore);
	void yahoo_change_buddy_group(int id, const char *who,
		const char *old_group, const char *new_group);
	void yahoo_group_rename(int id, const char *old_group,
		const char *new_group);

	void yahoo_conference_invite(int id, const char *from, YList *who,
		const char *room, const char *msg);
	void yahoo_conference_addinvite(int id, const char *from,
		const char *who, const char *room, const YList *members,
		const char *msg);
	void yahoo_conference_decline(int id, const char *from, YList *who,
		const char *room, const char *msg);
	void yahoo_conference_message(int id, const char *from, YList *who,
		const char *room, const char *msg, int utf8);
	void yahoo_conference_logon(int id, const char *from, YList *who,
		const char *room);
	void yahoo_conference_logoff(int id, const char *from, YList *who,
		const char *room);

/* Get a list of chatrooms */
	void yahoo_get_chatrooms(int id, int chatroomid);
/* join room with specified roomname and roomid */
	void yahoo_chat_logon(int id, const char *from, const char *room,
		const char *roomid);
/* Send message "msg" to room with specified roomname, msgtype is 1-normal message or 2-/me mesage */
	void yahoo_chat_message(int id, const char *from, const char *room,
		const char *msg, const int msgtype, const int utf8);
/* Log off chat */
	void yahoo_chat_logoff(int id, const char *from);

/* requests a webcam feed */
/* who is the person who's webcam you would like to view */
/* if who is null, then you're the broadcaster */
	void yahoo_webcam_get_feed(int id, const char *who);
	void yahoo_webcam_close_feed(int id, const char *who);

/* sends an image when uploading */
/* image points to a JPEG-2000 image, length is the length of the image */
/* in bytes. The timestamp is the time in milliseconds since we started the */
/* webcam. */
	void yahoo_webcam_send_image(int id, unsigned char *image,
		unsigned int length, unsigned int timestamp);

/* this function should be called if we want to allow a user to watch the */
/* webcam. Who is the user we want to accept. */
/* Accept user (accept = 1), decline user (accept = 0) */
	void yahoo_webcam_accept_viewer(int id, const char *who, int accept);

/* send an invitation to a user to view your webcam */
	void yahoo_webcam_invite(int id, const char *who);

/* will set up a connection and initiate file transfer.
 * callback will be called with the fd that you should write
 * the file data to
 */
	void yahoo_send_file(int id, const char *who, const char *msg,
		const char *name, unsigned long size,
		yahoo_get_fd_callback callback, void *data);

/*
 * Respond to a file transfer request. Be sure to provide the callback data
 * since that is your only chance to recognize future callbacks
 */
	void yahoo_send_file_transfer_response(int client_id, int response,
		char *id, void *data);


/* send a search request
 */
	void yahoo_search(int id, enum yahoo_search_type t, const char *text,
		enum yahoo_search_gender g, enum yahoo_search_agerange ar,
		int photo, int yahoo_only);

/* continue last search
 * should be called if only (start+found >= total)
 *
 * where the above three are passed to ext_yahoo_got_search_result
 */
	void yahoo_search_again(int id, int start);

/* these should be called when input is available on a fd */
/* registered by ext_yahoo_add_handler */
/* if these return negative values, errno may be set */
	int yahoo_read_ready(int id, void *fd, void *data);
	int yahoo_write_ready(int id, void *fd, void *data);

/* utility functions. these do not hit the server */
	enum yahoo_status yahoo_current_status(int id);
	const YList *yahoo_get_buddylist(int id);
	const YList *yahoo_get_ignorelist(int id);
	const YList *yahoo_get_identities(int id);
/* 'which' could be y, t, c or login.  This may change in later versions. */
	const char *yahoo_get_cookie(int id, const char *which);

/* returns the url used to get user profiles - you must append the user id */
/* as of now this is http://profiles.yahoo.com/ */
/* You'll have to do urlencoding yourself, but see yahoo_httplib.h first */
	const char *yahoo_get_profile_url(void);

	void yahoo_buddyicon_request(int id, const char *who);

#include "yahoo_httplib.h"

#ifdef __cplusplus
}
#endif
#endif
