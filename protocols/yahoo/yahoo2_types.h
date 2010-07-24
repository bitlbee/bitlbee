/*
 * libyahoo2: yahoo2_types.h
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

#ifndef YAHOO2_TYPES_H
#define YAHOO2_TYPES_H

#include "yahoo_list.h"

#ifdef __cplusplus
extern "C" {
#endif

	enum yahoo_service {	/* these are easier to see in hex */
		YAHOO_SERVICE_LOGON = 1,
		YAHOO_SERVICE_LOGOFF,
		YAHOO_SERVICE_ISAWAY,
		YAHOO_SERVICE_ISBACK,
		YAHOO_SERVICE_IDLE,	/* 5 (placemarker) */
		YAHOO_SERVICE_MESSAGE,
		YAHOO_SERVICE_IDACT,
		YAHOO_SERVICE_IDDEACT,
		YAHOO_SERVICE_MAILSTAT,
		YAHOO_SERVICE_USERSTAT,	/* 0xa */
		YAHOO_SERVICE_NEWMAIL,
		YAHOO_SERVICE_CHATINVITE,
		YAHOO_SERVICE_CALENDAR,
		YAHOO_SERVICE_NEWPERSONALMAIL,
		YAHOO_SERVICE_NEWCONTACT,
		YAHOO_SERVICE_ADDIDENT,	/* 0x10 */
		YAHOO_SERVICE_ADDIGNORE,
		YAHOO_SERVICE_PING,
		YAHOO_SERVICE_GOTGROUPRENAME,	/* < 1, 36(old), 37(new) */
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
		YAHOO_SERVICE_IGNORECONTACT,	/* > 1, 7, 13 < 1, 66, 13, 0 */
		YAHOO_SERVICE_REJECTCONTACT,
		YAHOO_SERVICE_GROUPRENAME = 0x89,	/* > 1, 65(new), 66(0), 67(old) */
		YAHOO_SERVICE_Y7_PING = 0x8A,
		YAHOO_SERVICE_CHATONLINE = 0x96,	/* > 109(id), 1, 6(abcde) < 0,1 */
		YAHOO_SERVICE_CHATGOTO,
		YAHOO_SERVICE_CHATJOIN,	/* > 1 104-room 129-1600326591 62-2 */
		YAHOO_SERVICE_CHATLEAVE,
		YAHOO_SERVICE_CHATEXIT = 0x9b,
		YAHOO_SERVICE_CHATADDINVITE = 0x9d,
		YAHOO_SERVICE_CHATLOGOUT = 0xa0,
		YAHOO_SERVICE_CHATPING,
		YAHOO_SERVICE_COMMENT = 0xa8,
		YAHOO_SERVICE_GAME_INVITE = 0xb7,
		YAHOO_SERVICE_STEALTH_PERM = 0xb9,
		YAHOO_SERVICE_STEALTH_SESSION = 0xba,
		YAHOO_SERVICE_AVATAR = 0xbc,
		YAHOO_SERVICE_PICTURE_CHECKSUM = 0xbd,
		YAHOO_SERVICE_PICTURE = 0xbe,
		YAHOO_SERVICE_PICTURE_UPDATE = 0xc1,
		YAHOO_SERVICE_PICTURE_UPLOAD = 0xc2,
		YAHOO_SERVICE_YAB_UPDATE = 0xc4,
		YAHOO_SERVICE_Y6_VISIBLE_TOGGLE = 0xc5,	/* YMSG13, key 13: 2 = invisible, 1 = visible */
		YAHOO_SERVICE_Y6_STATUS_UPDATE = 0xc6,	/* YMSG13 */
		YAHOO_SERVICE_PICTURE_STATUS = 0xc7,	/* YMSG13, key 213: 0 = none, 1 = avatar, 2 = picture */
		YAHOO_SERVICE_VERIFY_ID_EXISTS = 0xc8,
		YAHOO_SERVICE_AUDIBLE = 0xd0,
		YAHOO_SERVICE_Y7_PHOTO_SHARING = 0xd2,
		YAHOO_SERVICE_Y7_CONTACT_DETAILS = 0xd3,	/* YMSG13 */
		YAHOO_SERVICE_Y7_CHAT_SESSION = 0xd4,
		YAHOO_SERVICE_Y7_AUTHORIZATION = 0xd6,	/* YMSG13 */
		YAHOO_SERVICE_Y7_FILETRANSFER = 0xdc,	/* YMSG13 */
		YAHOO_SERVICE_Y7_FILETRANSFERINFO,	/* YMSG13 */
		YAHOO_SERVICE_Y7_FILETRANSFERACCEPT,	/* YMSG13 */
		YAHOO_SERVICE_Y7_MINGLE = 0xe1,	/* YMSG13 */
		YAHOO_SERVICE_Y7_CHANGE_GROUP = 0xe7,	/* YMSG13 */
		YAHOO_SERVICE_MYSTERY = 0xef,	/* Don't know what this is for */
		YAHOO_SERVICE_Y8_STATUS = 0xf0,	/* YMSG15 */
		YAHOO_SERVICE_Y8_LIST = 0Xf1,	/* YMSG15 */
		YAHOO_SERVICE_MESSAGE_CONFIRM = 0xfb,
		YAHOO_SERVICE_WEBLOGIN = 0x0226,
		YAHOO_SERVICE_SMS_MSG = 0x02ea
	};

	enum yahoo_status {
		YAHOO_STATUS_AVAILABLE = 0,
		YAHOO_STATUS_BRB,
		YAHOO_STATUS_BUSY,
		YAHOO_STATUS_NOTATHOME,
		YAHOO_STATUS_NOTATDESK,
		YAHOO_STATUS_NOTINOFFICE,
		YAHOO_STATUS_ONPHONE,
		YAHOO_STATUS_ONVACATION,
		YAHOO_STATUS_OUTTOLUNCH,
		YAHOO_STATUS_STEPPEDOUT,
		YAHOO_STATUS_INVISIBLE = 12,
		YAHOO_STATUS_CUSTOM = 99,
		YAHOO_STATUS_IDLE = 999,
		YAHOO_STATUS_OFFLINE = 0x5a55aa56	/* don't ask */
	};

	enum ypacket_status {
		YPACKET_STATUS_DISCONNECTED = -1,
		YPACKET_STATUS_DEFAULT = 0,
		YPACKET_STATUS_SERVERACK = 1,
		YPACKET_STATUS_GAME = 0x2,
		YPACKET_STATUS_AWAY = 0x4,
		YPACKET_STATUS_CONTINUED = 0x5,
		YPACKET_STATUS_INVISIBLE = 12,
		YPACKET_STATUS_NOTIFY = 0x16,	/* TYPING */
		YPACKET_STATUS_WEBLOGIN = 0x5a55aa55,
		YPACKET_STATUS_OFFLINE = 0x5a55aa56
	};

#define YAHOO_STATUS_GAME	0x2	/* Games don't fit into the regular status model */

	enum yahoo_login_status {
		YAHOO_LOGIN_OK = 0,
		YAHOO_LOGIN_LOGOFF = 1,
		YAHOO_LOGIN_UNAME = 3,
		YAHOO_LOGIN_PASSWD = 13,
		YAHOO_LOGIN_LOCK = 14,
		YAHOO_LOGIN_DUPL = 99,
		YAHOO_LOGIN_SOCK = -1,
		YAHOO_LOGIN_UNKNOWN = 999
	};

	enum yahoo_error {
		E_UNKNOWN = -1,
		E_CONNECTION = -2,
		E_SYSTEM = -3,
		E_CUSTOM = 0,

		/* responses from ignore buddy */
		E_IGNOREDUP = 2,
		E_IGNORENONE = 3,
		E_IGNORECONF = 12,

		/* conference */
		E_CONFNOTAVAIL = 20
	};

	enum yahoo_log_level {
		YAHOO_LOG_NONE = 0,
		YAHOO_LOG_FATAL,
		YAHOO_LOG_ERR,
		YAHOO_LOG_WARNING,
		YAHOO_LOG_NOTICE,
		YAHOO_LOG_INFO,
		YAHOO_LOG_DEBUG
	};

	enum yahoo_file_transfer {
		YAHOO_FILE_TRANSFER_INIT = 1,
		YAHOO_FILE_TRANSFER_ACCEPT = 3,
		YAHOO_FILE_TRANSFER_REJECT = 4,
		YAHOO_FILE_TRANSFER_DONE = 5,
		YAHOO_FILE_TRANSFER_RELAY,
		YAHOO_FILE_TRANSFER_FAILED,
		YAHOO_FILE_TRANSFER_UNKNOWN
	};

#define YAHOO_PROTO_VER 0x0010

/* Yahoo style/color directives */
#define YAHOO_COLOR_BLACK "\033[30m"
#define YAHOO_COLOR_BLUE "\033[31m"
#define YAHOO_COLOR_LIGHTBLUE "\033[32m"
#define YAHOO_COLOR_GRAY "\033[33m"
#define YAHOO_COLOR_GREEN "\033[34m"
#define YAHOO_COLOR_PINK "\033[35m"
#define YAHOO_COLOR_PURPLE "\033[36m"
#define YAHOO_COLOR_ORANGE "\033[37m"
#define YAHOO_COLOR_RED "\033[38m"
#define YAHOO_COLOR_OLIVE "\033[39m"
#define YAHOO_COLOR_ANY "\033[#"
#define YAHOO_STYLE_ITALICON "\033[2m"
#define YAHOO_STYLE_ITALICOFF "\033[x2m"
#define YAHOO_STYLE_BOLDON "\033[1m"
#define YAHOO_STYLE_BOLDOFF "\033[x1m"
#define YAHOO_STYLE_UNDERLINEON "\033[4m"
#define YAHOO_STYLE_UNDERLINEOFF "\033[x4m"
#define YAHOO_STYLE_URLON "\033[lm"
#define YAHOO_STYLE_URLOFF "\033[xlm"

	enum yahoo_connection_type {
		YAHOO_CONNECTION_PAGER = 0,
		YAHOO_CONNECTION_FT,
		YAHOO_CONNECTION_YAB,
		YAHOO_CONNECTION_WEBCAM_MASTER,
		YAHOO_CONNECTION_WEBCAM,
		YAHOO_CONNECTION_CHATCAT,
		YAHOO_CONNECTION_SEARCH,
		YAHOO_CONNECTION_AUTH
	};

	enum yahoo_webcam_direction_type {
		YAHOO_WEBCAM_DOWNLOAD = 0,
		YAHOO_WEBCAM_UPLOAD
	};

	enum yahoo_stealth_visibility_type {
		YAHOO_STEALTH_DEFAULT = 0,
		YAHOO_STEALTH_ONLINE,
		YAHOO_STEALTH_PERM_OFFLINE
	};

/* chat member attribs */
#define YAHOO_CHAT_MALE 0x8000
#define YAHOO_CHAT_FEMALE 0x10000
#define YAHOO_CHAT_FEMALE 0x10000
#define YAHOO_CHAT_DUNNO 0x400
#define YAHOO_CHAT_WEBCAM 0x10

	enum yahoo_webcam_conn_type { Y_WCM_DIALUP, Y_WCM_DSL, Y_WCM_T1 };

	struct yahoo_webcam {
		int direction;	/* Uploading or downloading */
		int conn_type;	/* 0=Dialup, 1=DSL/Cable, 2=T1/Lan */

		char *user;	/* user we are viewing */
		char *server;	/* webcam server to connect to */
		int port;	/* webcam port to connect on */
		char *key;	/* key to connect to the server with */
		char *description;	/* webcam description */
		char *my_ip;	/* own ip number */
	};

	struct yahoo_webcam_data {
		unsigned int data_size;
		unsigned int to_read;
		unsigned int timestamp;
		unsigned char packet_type;
	};

	struct yahoo_data {
		char *user;
		char *password;

		char *cookie_y;
		char *cookie_t;
		char *cookie_c;
		char *cookie_b;
		char *login_cookie;
		char *crumb;
		char *seed;

		YList *buddies;
		YList *ignore;
		YList *identities;
		char *login_id;

		int current_status;
		int initial_status;
		int logged_in;

		int session_id;

		int client_id;

		char *rawbuddylist;
		char *ignorelist;

		void *server_settings;

		struct yahoo_process_status_entry *half_user;
	};

	struct yab {
		int yid;
		char *id;
		char *fname;
		char *lname;
		char *nname;
		char *email;
		char *hphone;
		char *wphone;
		char *mphone;
		int dbid;
	};

	struct yahoo_buddy {
		char *group;
		char *id;
		char *real_name;
		struct yab *yab_entry;
	};

	enum yahoo_search_type {
		YAHOO_SEARCH_KEYWORD = 0,
		YAHOO_SEARCH_YID,
		YAHOO_SEARCH_NAME
	};

	enum yahoo_search_gender {
		YAHOO_GENDER_NONE = 0,
		YAHOO_GENDER_MALE,
		YAHOO_GENDER_FEMALE
	};

	enum yahoo_search_agerange {
		YAHOO_AGERANGE_NONE = 0
	};

	struct yahoo_found_contact {
		char *id;
		char *gender;
		char *location;
		int age;
		int online;
	};

/*
 * Function pointer to be passed to http get/post and send file
 */
	typedef void (*yahoo_get_fd_callback) (int id, void *fd, int error,
		void *data);

/*
 * Function pointer to be passed to yahoo_get_url_handle
 */
	typedef void (*yahoo_get_url_handle_callback) (int id, void *fd,
		int error, const char *filename, unsigned long size,
		void *data);

	struct yahoo_chat_member {
		char *id;
		int age;
		int attribs;
		char *alias;
		char *location;
	};

	struct yahoo_process_status_entry { 
		char *name;     /* 7      name */ 
		int state;      /* 10     state */ 
		int flags;      /* 13     flags, bit 0 = pager, bit 1 = chat, bit 2 = game */ 
		int mobile;     /* 60     mobile */ 
		char *msg;      /* 19     custom status message */ 
		int away;       /* 47     away (or invisible) */ 
		int buddy_session; /* 11  state */ 
		int f17;        /* 17     in chat? then what about flags? */ 
		int idle;       /* 137    seconds idle */ 
		int f138;       /* 138    state */ 
		char *f184;     /* 184    state */ 
		int f192;       /* 192    state */ 
		int f10001;     /* 10001  state */ 
		int f10002;     /* 10002  state */ 
		int f198;       /* 198    state */ 
		char *f197;     /* 197    state */ 
		char *f205;     /* 205    state */ 
		int f213;       /* 213    state */ 
	};

#ifdef __cplusplus
}
#endif
#endif
