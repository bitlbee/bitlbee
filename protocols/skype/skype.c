/*
 *  skype.c - Skype plugin for BitlBee
 *
 *  Copyright (c) 2007-2013 by Miklos Vajna <vmiklos@vmiklos.hu>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307,
 *  USA.
 */

#define _XOPEN_SOURCE
#define _BSD_SOURCE
#include <poll.h>
#include <stdio.h>
#include <bitlbee.h>
#include <ssl_client.h>

#define SKYPE_DEFAULT_SERVER "localhost"
#define SKYPE_DEFAULT_PORT "2727"
#define IRC_LINE_SIZE 1024
#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))

/*
 * Enumerations
 */

enum {
	SKYPE_CALL_RINGING = 1,
	SKYPE_CALL_MISSED,
	SKYPE_CALL_CANCELLED,
	SKYPE_CALL_FINISHED,
	SKYPE_CALL_REFUSED
};

enum {
	SKYPE_FILETRANSFER_NEW = 1,
	SKYPE_FILETRANSFER_TRANSFERRING,
	SKYPE_FILETRANSFER_COMPLETED,
	SKYPE_FILETRANSFER_FAILED
};

/*
 * Structures
 */

struct skype_data {
	struct im_connection *ic;
	char *username;
	/* The effective file descriptor. We store it here so any function can
	 * write() to it. */
	int fd;
	/* File descriptor returned by bitlbee. we store it so we know when
	 * we're connected and when we aren't. */
	int bfd;
	/* ssl_getfd() uses this to get the file desciptor. */
	void *ssl;
	/* When we receive a new message id, we query the properties, finally
	 * the chatname. Store the properties here so that we can use
	 * imcb_buddy_msg() when we got the chatname. */
	char *handle;
	/* List, because of multiline messages. */
	GList *body;
	char *type;
	/* This is necessary because we send a notification when we get the
	 * handle. So we store the state here and then we can send a
	 * notification about the handle is in a given status. */
	int call_status;
	char *call_id;
	char *call_duration;
	/* If the call is outgoing or not */
	int call_out;
	/* Same for file transfers. */
	int filetransfer_status;
	/* Path of the file being transferred. */
	char *filetransfer_path;
	/* Using /j #nick we want to have a groupchat with two people. Usually
	 * not (default). */
	char *groupchat_with;
	/* The user who invited us to the chat. */
	char *adder;
	/* If we are waiting for a confirmation about we changed the topic. */
	int topic_wait;
	/* These are used by the info command. */
	char *info_fullname;
	char *info_phonehome;
	char *info_phoneoffice;
	char *info_phonemobile;
	char *info_nrbuddies;
	char *info_tz;
	char *info_seen;
	char *info_birthday;
	char *info_sex;
	char *info_language;
	char *info_country;
	char *info_province;
	char *info_city;
	char *info_homepage;
	char *info_about;
	/* When a call fails, we get the reason and later we get the failure
	 * event, so store the failure code here till then */
	int failurereason;
	/* If this is just an update of an already received message. */
	int is_edit;
	/* List of struct skype_group* */
	GList *groups;
	/* Pending user which has to be added to the next group which is
	 * created. */
	char *pending_user;
};

struct skype_away_state {
	char *code;
	char *full_name;
};

struct skype_buddy_ask_data {
	struct im_connection *ic;
	/* This is also used for call IDs for simplicity */
	char *handle;
};

struct skype_group {
	int id;
	char *name;
	GList *users;
};

/*
 * Tables
 */

const struct skype_away_state skype_away_state_list[] = {
	{ "AWAY", "Away" },
	{ "NA", "Not available" },
	{ "DND", "Do Not Disturb" },
	{ "INVISIBLE", "Invisible" },
	{ "OFFLINE", "Offline" },
	{ "SKYPEME", "Skype Me" },
	{ "ONLINE", "Online" },
	{ NULL, NULL}
};

/*
 * Functions
 */

int skype_write(struct im_connection *ic, char *buf, int len)
{
	struct skype_data *sd = ic->proto_data;
	struct pollfd pfd[1];

	if (!sd->ssl)
		return FALSE;

	pfd[0].fd = sd->fd;
	pfd[0].events = POLLOUT;

	/* This poll is necessary or we'll get a SIGPIPE when we write() to
	 * sd->fd. */
	poll(pfd, 1, 1000);
	if (pfd[0].revents & POLLHUP) {
		imc_logout(ic, TRUE);
		return FALSE;
	}
	ssl_write(sd->ssl, buf, len);

	return TRUE;
}

int skype_printf(struct im_connection *ic, char *fmt, ...)
{
	va_list args;
	char str[IRC_LINE_SIZE];

	va_start(args, fmt);
	vsnprintf(str, IRC_LINE_SIZE, fmt, args);
	va_end(args);

	return skype_write(ic, str, strlen(str));
}

static void skype_buddy_ask_yes(void *data)
{
	struct skype_buddy_ask_data *bla = data;
	skype_printf(bla->ic, "SET USER %s ISAUTHORIZED TRUE",
		bla->handle);
	g_free(bla->handle);
	g_free(bla);
}

static void skype_buddy_ask_no(void *data)
{
	struct skype_buddy_ask_data *bla = data;
	skype_printf(bla->ic, "SET USER %s ISAUTHORIZED FALSE",
		bla->handle);
	g_free(bla->handle);
	g_free(bla);
}

void skype_buddy_ask(struct im_connection *ic, char *handle, char *message)
{
	struct skype_buddy_ask_data *bla = g_new0(struct skype_buddy_ask_data,
		1);
	char *buf;

	bla->ic = ic;
	bla->handle = g_strdup(handle);

	buf = g_strdup_printf("The user %s wants to add you to his/her buddy list, saying: '%s'.", handle, message);
	imcb_ask(ic, buf, bla, skype_buddy_ask_yes, skype_buddy_ask_no);
	g_free(buf);
}

static void skype_call_ask_yes(void *data)
{
	struct skype_buddy_ask_data *bla = data;
	skype_printf(bla->ic, "SET CALL %s STATUS INPROGRESS",
		bla->handle);
	g_free(bla->handle);
	g_free(bla);
}

static void skype_call_ask_no(void *data)
{
	struct skype_buddy_ask_data *bla = data;
	skype_printf(bla->ic, "SET CALL %s STATUS FINISHED",
		bla->handle);
	g_free(bla->handle);
	g_free(bla);
}

void skype_call_ask(struct im_connection *ic, char *call_id, char *message)
{
	struct skype_buddy_ask_data *bla = g_new0(struct skype_buddy_ask_data,
		1);

	bla->ic = ic;
	bla->handle = g_strdup(call_id);

	imcb_ask(ic, message, bla, skype_call_ask_yes, skype_call_ask_no);
}

static char *skype_call_strerror(int err)
{
	switch (err) {
	case 1:
		return "Miscellaneous error";
	case 2:
		return "User or phone number does not exist.";
	case 3:
		return "User is offline";
	case 4:
		return "No proxy found";
	case 5:
		return "Session terminated.";
	case 6:
		return "No common codec found.";
	case 7:
		return "Sound I/O error.";
	case 8:
		return "Problem with remote sound device.";
	case 9:
		return "Call blocked by recipient.";
	case 10:
		return "Recipient not a friend.";
	case 11:
		return "Current user not authorized by recipient.";
	case 12:
		return "Sound recording error.";
	default:
		return "Unknown error";
	}
}

static char *skype_group_by_username(struct im_connection *ic, char *username)
{
	struct skype_data *sd = ic->proto_data;
	int i, j;

	/* NEEDSWORK: we just search for the first group of the user, multiple
	 * groups / user is not yet supported by BitlBee. */

	for (i = 0; i < g_list_length(sd->groups); i++) {
		struct skype_group *sg = g_list_nth_data(sd->groups, i);
		for (j = 0; j < g_list_length(sg->users); j++) {
			if (!strcmp(g_list_nth_data(sg->users, j), username))
				return sg->name;
		}
	}
	return NULL;
}

static struct skype_group *skype_group_by_name(struct im_connection *ic, char *name)
{
	struct skype_data *sd = ic->proto_data;
	int i;

	for (i = 0; i < g_list_length(sd->groups); i++) {
		struct skype_group *sg = g_list_nth_data(sd->groups, i);
		if (!strcmp(sg->name, name))
			return sg;
	}
	return NULL;
}

static void skype_parse_users(struct im_connection *ic, char *line)
{
	char **i, **nicks;

	nicks = g_strsplit(line + 6, ", ", 0);
	for (i = nicks; *i; i++)
		skype_printf(ic, "GET USER %s ONLINESTATUS\n", *i);
	g_strfreev(nicks);
}

static void skype_parse_user(struct im_connection *ic, char *line)
{
	int flags = 0;
	char *ptr;
	struct skype_data *sd = ic->proto_data;
	char *user = strchr(line, ' ');
	char *status = strrchr(line, ' ');

	status++;
	ptr = strchr(++user, ' ');
	if (!ptr)
		return;
	*ptr = '\0';
	ptr++;
	if (!strncmp(ptr, "ONLINESTATUS ", 13)) {
			if (!strcmp(user, sd->username))
				return;
			if (!set_getbool(&ic->acc->set, "test_join")
				&& !strcmp(user, "echo123"))
				return;
		ptr = g_strdup_printf("%s@skype.com", user);
		imcb_add_buddy(ic, ptr, skype_group_by_username(ic, user));
		if (strcmp(status, "OFFLINE") && (strcmp(status, "SKYPEOUT") ||
			!set_getbool(&ic->acc->set, "skypeout_offline")))
			flags |= OPT_LOGGED_IN;
		if (strcmp(status, "ONLINE") && strcmp(status, "SKYPEME"))
			flags |= OPT_AWAY;
		imcb_buddy_status(ic, ptr, flags, NULL, NULL);
		g_free(ptr);
	} else if (!strncmp(ptr, "RECEIVEDAUTHREQUEST ", 20)) {
		char *message = ptr + 20;
		if (strlen(message))
			skype_buddy_ask(ic, user, message);
	} else if (!strncmp(ptr, "BUDDYSTATUS ", 12)) {
		char *st = ptr + 12;
		if (!strcmp(st, "3")) {
			char *buf = g_strdup_printf("%s@skype.com", user);
			imcb_add_buddy(ic, buf, skype_group_by_username(ic, user));
			g_free(buf);
		}
	} else if (!strncmp(ptr, "MOOD_TEXT ", 10)) {
		char *buf = g_strdup_printf("%s@skype.com", user);
		bee_user_t *bu = bee_user_by_handle(ic->bee, ic, buf);
		g_free(buf);
		buf = ptr + 10;
		if (bu)
			imcb_buddy_status(ic, bu->handle, bu->flags, NULL,
					*buf ? buf : NULL);
		if (set_getbool(&ic->acc->set, "show_moods"))
			imcb_log(ic, "User `%s' changed mood text to `%s'", user, buf);
	} else if (!strncmp(ptr, "FULLNAME ", 9))
		sd->info_fullname = g_strdup(ptr + 9);
	else if (!strncmp(ptr, "PHONE_HOME ", 11))
		sd->info_phonehome = g_strdup(ptr + 11);
	else if (!strncmp(ptr, "PHONE_OFFICE ", 13))
		sd->info_phoneoffice = g_strdup(ptr + 13);
	else if (!strncmp(ptr, "PHONE_MOBILE ", 13))
		sd->info_phonemobile = g_strdup(ptr + 13);
	else if (!strncmp(ptr, "NROF_AUTHED_BUDDIES ", 20))
		sd->info_nrbuddies = g_strdup(ptr + 20);
	else if (!strncmp(ptr, "TIMEZONE ", 9))
		sd->info_tz = g_strdup(ptr + 9);
	else if (!strncmp(ptr, "LASTONLINETIMESTAMP ", 20))
		sd->info_seen = g_strdup(ptr + 20);
	else if (!strncmp(ptr, "SEX ", 4))
		sd->info_sex = g_strdup(ptr + 4);
	else if (!strncmp(ptr, "LANGUAGE ", 9))
		sd->info_language = g_strdup(ptr + 9);
	else if (!strncmp(ptr, "COUNTRY ", 8))
		sd->info_country = g_strdup(ptr + 8);
	else if (!strncmp(ptr, "PROVINCE ", 9))
		sd->info_province = g_strdup(ptr + 9);
	else if (!strncmp(ptr, "CITY ", 5))
		sd->info_city = g_strdup(ptr + 5);
	else if (!strncmp(ptr, "HOMEPAGE ", 9))
		sd->info_homepage = g_strdup(ptr + 9);
	else if (!strncmp(ptr, "ABOUT ", 6)) {
		/* Support multiple about lines. */
		if (!sd->info_about)
			sd->info_about = g_strdup(ptr + 6);
		else {
			GString *st = g_string_new(sd->info_about);
			g_string_append_printf(st, "\n%s", ptr + 6);
			g_free(sd->info_about);
			sd->info_about = g_strdup(st->str);
			g_string_free(st, TRUE);
		}
	} else if (!strncmp(ptr, "BIRTHDAY ", 9)) {
		sd->info_birthday = g_strdup(ptr + 9);

		GString *st = g_string_new("Contact Information\n");
		g_string_append_printf(st, "Skype Name: %s\n", user);
		if (sd->info_fullname) {
			if (strlen(sd->info_fullname))
				g_string_append_printf(st, "Full Name: %s\n",
					sd->info_fullname);
			g_free(sd->info_fullname);
			sd->info_fullname = NULL;
		}
		if (sd->info_phonehome) {
			if (strlen(sd->info_phonehome))
				g_string_append_printf(st, "Home Phone: %s\n",
					sd->info_phonehome);
			g_free(sd->info_phonehome);
			sd->info_phonehome = NULL;
		}
		if (sd->info_phoneoffice) {
			if (strlen(sd->info_phoneoffice))
				g_string_append_printf(st, "Office Phone: %s\n",
					sd->info_phoneoffice);
			g_free(sd->info_phoneoffice);
			sd->info_phoneoffice = NULL;
		}
		if (sd->info_phonemobile) {
			if (strlen(sd->info_phonemobile))
				g_string_append_printf(st, "Mobile Phone: %s\n",
					sd->info_phonemobile);
			g_free(sd->info_phonemobile);
			sd->info_phonemobile = NULL;
		}
		g_string_append_printf(st, "Personal Information\n");
		if (sd->info_nrbuddies) {
			if (strlen(sd->info_nrbuddies))
				g_string_append_printf(st,
					"Contacts: %s\n", sd->info_nrbuddies);
			g_free(sd->info_nrbuddies);
			sd->info_nrbuddies = NULL;
		}
		if (sd->info_tz) {
			if (strlen(sd->info_tz)) {
				char ib[256];
				time_t t = time(NULL);
				t += atoi(sd->info_tz)-(60*60*24);
				struct tm *gt = gmtime(&t);
				strftime(ib, 256, "%H:%M:%S", gt);
				g_string_append_printf(st,
					"Local Time: %s\n", ib);
			}
			g_free(sd->info_tz);
			sd->info_tz = NULL;
		}
		if (sd->info_seen) {
			if (strlen(sd->info_seen)) {
				char ib[256];
				time_t it = atoi(sd->info_seen);
				struct tm *tm = localtime(&it);
				strftime(ib, 256, ("%Y. %m. %d. %H:%M"), tm);
				g_string_append_printf(st,
					"Last Seen: %s\n", ib);
			}
			g_free(sd->info_seen);
			sd->info_seen = NULL;
		}
		if (sd->info_birthday) {
			if (strlen(sd->info_birthday) &&
				strcmp(sd->info_birthday, "0")) {
				char ib[256];
				struct tm tm;
				strptime(sd->info_birthday, "%Y%m%d", &tm);
				strftime(ib, 256, "%B %d, %Y", &tm);
				g_string_append_printf(st,
					"Birthday: %s\n", ib);

				strftime(ib, 256, "%Y", &tm);
				int year = atoi(ib);
				time_t t = time(NULL);
				struct tm *lt = localtime(&t);
				g_string_append_printf(st,
					"Age: %d\n", lt->tm_year+1900-year);
			}
			g_free(sd->info_birthday);
			sd->info_birthday = NULL;
		}
		if (sd->info_sex) {
			if (strlen(sd->info_sex)) {
				char *iptr = sd->info_sex;
				while (*iptr++)
					*iptr = tolower(*iptr);
				g_string_append_printf(st,
					"Gender: %s\n", sd->info_sex);
			}
			g_free(sd->info_sex);
			sd->info_sex = NULL;
		}
		if (sd->info_language) {
			if (strlen(sd->info_language)) {
				char *iptr = strchr(sd->info_language, ' ');
				if (iptr)
					iptr++;
				else
					iptr = sd->info_language;
				g_string_append_printf(st,
					"Language: %s\n", iptr);
			}
			g_free(sd->info_language);
			sd->info_language = NULL;
		}
		if (sd->info_country) {
			if (strlen(sd->info_country)) {
				char *iptr = strchr(sd->info_country, ' ');
				if (iptr)
					iptr++;
				else
					iptr = sd->info_country;
				g_string_append_printf(st,
					"Country: %s\n", iptr);
			}
			g_free(sd->info_country);
			sd->info_country = NULL;
		}
		if (sd->info_province) {
			if (strlen(sd->info_province))
				g_string_append_printf(st,
					"Region: %s\n", sd->info_province);
			g_free(sd->info_province);
			sd->info_province = NULL;
		}
		if (sd->info_city) {
			if (strlen(sd->info_city))
				g_string_append_printf(st,
					"City: %s\n", sd->info_city);
			g_free(sd->info_city);
			sd->info_city = NULL;
		}
		if (sd->info_homepage) {
			if (strlen(sd->info_homepage))
				g_string_append_printf(st,
					"Homepage: %s\n", sd->info_homepage);
			g_free(sd->info_homepage);
			sd->info_homepage = NULL;
		}
		if (sd->info_about) {
			if (strlen(sd->info_about))
				g_string_append_printf(st, "%s\n",
					sd->info_about);
			g_free(sd->info_about);
			sd->info_about = NULL;
		}
		imcb_log(ic, "%s", st->str);
		g_string_free(st, TRUE);
	}
}

static void skype_parse_chatmessage_said_emoted(struct im_connection *ic, struct groupchat *gc, char *body)
{
	struct skype_data *sd = ic->proto_data;
	char buf[IRC_LINE_SIZE];
	if (!strcmp(sd->type, "SAID")) {
		if (!sd->is_edit)
			g_snprintf(buf, IRC_LINE_SIZE, "%s", body);
		else {
			g_snprintf(buf, IRC_LINE_SIZE, "%s %s", set_getstr(&ic->acc->set, "edit_prefix"), body);
			sd->is_edit = 0;
		}
	} else
		g_snprintf(buf, IRC_LINE_SIZE, "/me %s", body);
	if (!gc)
		/* Private message */
		imcb_buddy_msg(ic, sd->handle, buf, 0, 0);
	else
		/* Groupchat message */
		imcb_chat_msg(gc, sd->handle, buf, 0, 0);
}

static void skype_parse_chatmessage(struct im_connection *ic, char *line)
{
	struct skype_data *sd = ic->proto_data;
	char *id = strchr(line, ' ');

	if (!++id)
		return;
	char *info = strchr(id, ' ');

	if (!info)
		return;
	*info = '\0';
	info++;
	if (!strcmp(info, "STATUS RECEIVED") || !strncmp(info, "EDITED_TIMESTAMP", 16)) {
		/* New message ID:
		 * (1) Request its from field
		 * (2) Request its body
		 * (3) Request its type
		 * (4) Query chatname
		 */
		skype_printf(ic, "GET CHATMESSAGE %s FROM_HANDLE\n", id);
		if (!strcmp(info, "STATUS RECEIVED"))
			skype_printf(ic, "GET CHATMESSAGE %s BODY\n", id);
		else
			sd->is_edit = 1;
		skype_printf(ic, "GET CHATMESSAGE %s TYPE\n", id);
		skype_printf(ic, "GET CHATMESSAGE %s CHATNAME\n", id);
	} else if (!strncmp(info, "FROM_HANDLE ", 12)) {
		info += 12;
		/* New from field value. Store
		 * it, then we can later use it
		 * when we got the message's
		 * body. */
		g_free(sd->handle);
		sd->handle = g_strdup_printf("%s@skype.com", info);
	} else if (!strncmp(info, "EDITED_BY ", 10)) {
		info += 10;
		/* This is the same as
		 * FROM_HANDLE, except that we
		 * never request these lines
		 * from Skype, we just get
		 * them. */
		g_free(sd->handle);
		sd->handle = g_strdup_printf("%s@skype.com", info);
	} else if (!strncmp(info, "BODY ", 5)) {
		info += 5;
		sd->body = g_list_append(sd->body, g_strdup(info));
	}	else if (!strncmp(info, "TYPE ", 5)) {
		info += 5;
		g_free(sd->type);
		sd->type = g_strdup(info);
	} else if (!strncmp(info, "CHATNAME ", 9)) {
		info += 9;
		if (sd->handle && sd->body && sd->type) {
			struct groupchat *gc = bee_chat_by_title(ic->bee, ic, info);
			int i;
			for (i = 0; i < g_list_length(sd->body); i++) {
				char *body = g_list_nth_data(sd->body, i);
				if (!strcmp(sd->type, "SAID") ||
					!strcmp(sd->type, "EMOTED")) {
					skype_parse_chatmessage_said_emoted(ic, gc, body);
				} else if (!strcmp(sd->type, "SETTOPIC") && gc)
					imcb_chat_topic(gc,
						sd->handle, body, 0);
				else if (!strcmp(sd->type, "LEFT") && gc)
					imcb_chat_remove_buddy(gc,
						sd->handle, NULL);
			}
			g_list_free(sd->body);
			sd->body = NULL;
		}
	}
}

static void skype_parse_call(struct im_connection *ic, char *line)
{
	struct skype_data *sd = ic->proto_data;
	char *id = strchr(line, ' ');
	char buf[IRC_LINE_SIZE];

	if (!++id)
		return;
	char *info = strchr(id, ' ');

	if (!info)
		return;
	*info = '\0';
	info++;
	if (!strncmp(info, "FAILUREREASON ", 14))
		sd->failurereason = atoi(strchr(info, ' '));
	else if (!strcmp(info, "STATUS RINGING")) {
		if (sd->call_id)
			g_free(sd->call_id);
		sd->call_id = g_strdup(id);
		skype_printf(ic, "GET CALL %s PARTNER_HANDLE\n", id);
		sd->call_status = SKYPE_CALL_RINGING;
	} else if (!strcmp(info, "STATUS MISSED")) {
		skype_printf(ic, "GET CALL %s PARTNER_HANDLE\n", id);
		sd->call_status = SKYPE_CALL_MISSED;
	} else if (!strcmp(info, "STATUS CANCELLED")) {
		skype_printf(ic, "GET CALL %s PARTNER_HANDLE\n", id);
		sd->call_status = SKYPE_CALL_CANCELLED;
	} else if (!strcmp(info, "STATUS FINISHED")) {
		skype_printf(ic, "GET CALL %s PARTNER_HANDLE\n", id);
		sd->call_status = SKYPE_CALL_FINISHED;
	} else if (!strcmp(info, "STATUS REFUSED")) {
		skype_printf(ic, "GET CALL %s PARTNER_HANDLE\n", id);
		sd->call_status = SKYPE_CALL_REFUSED;
	} else if (!strcmp(info, "STATUS UNPLACED")) {
		if (sd->call_id)
			g_free(sd->call_id);
		/* Save the ID for later usage (Cancel/Finish). */
		sd->call_id = g_strdup(id);
		sd->call_out = TRUE;
	} else if (!strcmp(info, "STATUS FAILED")) {
		imcb_error(ic, "Call failed: %s",
			skype_call_strerror(sd->failurereason));
		sd->call_id = NULL;
	} else if (!strncmp(info, "DURATION ", 9)) {
		if (sd->call_duration)
			g_free(sd->call_duration);
		sd->call_duration = g_strdup(info+9);
	} else if (!strncmp(info, "PARTNER_HANDLE ", 15)) {
		info += 15;
		if (!sd->call_status)
			return;
		switch (sd->call_status) {
		case SKYPE_CALL_RINGING:
			if (sd->call_out)
				imcb_log(ic, "You are currently ringing the user %s.", info);
			else {
				g_snprintf(buf, IRC_LINE_SIZE,
					"The user %s is currently ringing you.",
					info);
				skype_call_ask(ic, sd->call_id, buf);
			}
			break;
		case SKYPE_CALL_MISSED:
			imcb_log(ic, "You have missed a call from user %s.",
				info);
			break;
		case SKYPE_CALL_CANCELLED:
			imcb_log(ic, "You cancelled the call to the user %s.",
				info);
			sd->call_status = 0;
			sd->call_out = FALSE;
			break;
		case SKYPE_CALL_REFUSED:
			if (sd->call_out)
				imcb_log(ic, "The user %s refused the call.",
					info);
			else
				imcb_log(ic,
					"You refused the call from user %s.",
					info);
			sd->call_out = FALSE;
			break;
		case SKYPE_CALL_FINISHED:
			if (sd->call_duration)
				imcb_log(ic,
					"You finished the call to the user %s "
					"(duration: %s seconds).",
					info, sd->call_duration);
			else
				imcb_log(ic,
					"You finished the call to the user %s.",
					info);
			sd->call_out = FALSE;
			break;
		default:
			/* Don't be noisy, ignore other statuses for now. */
			break;
		}
		sd->call_status = 0;
	}
}

static void skype_parse_filetransfer(struct im_connection *ic, char *line)
{
	struct skype_data *sd = ic->proto_data;
	char *id = strchr(line, ' ');

	if (!++id)
		return;
	char *info = strchr(id, ' ');

	if (!info)
		return;
	*info = '\0';
	info++;
	if (!strcmp(info, "STATUS NEW")) {
		skype_printf(ic, "GET FILETRANSFER %s PARTNER_HANDLE\n",
			id);
		sd->filetransfer_status = SKYPE_FILETRANSFER_NEW;
	} else if (!strcmp(info, "STATUS FAILED")) {
		skype_printf(ic, "GET FILETRANSFER %s PARTNER_HANDLE\n",
			id);
		sd->filetransfer_status = SKYPE_FILETRANSFER_FAILED;
	} else if (!strcmp(info, "STATUS COMPLETED")) {
		skype_printf(ic, "GET FILETRANSFER %s PARTNER_HANDLE\n", id);
		sd->filetransfer_status = SKYPE_FILETRANSFER_COMPLETED;
	} else if (!strcmp(info, "STATUS TRANSFERRING")) {
		skype_printf(ic, "GET FILETRANSFER %s PARTNER_HANDLE\n", id);
		sd->filetransfer_status = SKYPE_FILETRANSFER_TRANSFERRING;
	} else if (!strncmp(info, "FILEPATH ", 9)) {
		info += 9;
		sd->filetransfer_path = g_strdup(info);
	} else if (!strncmp(info, "PARTNER_HANDLE ", 15)) {
		info += 15;
		if (!sd->filetransfer_status)
			return;
		switch (sd->filetransfer_status) {
		case SKYPE_FILETRANSFER_NEW:
			imcb_log(ic, "The user %s offered a new file for you.",
				info);
			break;
		case SKYPE_FILETRANSFER_FAILED:
			imcb_log(ic, "Failed to transfer file from user %s.",
				info);
			break;
		case SKYPE_FILETRANSFER_COMPLETED:
			imcb_log(ic, "File transfer from user %s completed.", info);
			break;
		case SKYPE_FILETRANSFER_TRANSFERRING:
			if (sd->filetransfer_path) {
				imcb_log(ic, "File transfer from user %s started, saving to %s.", info, sd->filetransfer_path);
				g_free(sd->filetransfer_path);
				sd->filetransfer_path = NULL;
			}
			break;
		}
		sd->filetransfer_status = 0;
	}
}

static struct skype_group *skype_group_by_id(struct im_connection *ic, int id)
{
	struct skype_data *sd = ic->proto_data;
	int i;

	for (i = 0; i < g_list_length(sd->groups); i++) {
		struct skype_group *sg = (struct skype_group *)g_list_nth_data(sd->groups, i);

		if (sg->id == id)
			return sg;
	}
	return NULL;
}

static void skype_group_free(struct skype_group *sg, gboolean usersonly)
{
	int i;

	for (i = 0; i < g_list_length(sg->users); i++) {
		char *user = g_list_nth_data(sg->users, i);
		g_free(user);
	}
	sg->users = NULL;
	if (usersonly)
		return;
	g_free(sg->name);
	g_free(sg);
}

/* Update the group of each user in this group */
static void skype_group_users(struct im_connection *ic, struct skype_group *sg)
{
	int i;

	for (i = 0; i < g_list_length(sg->users); i++) {
		char *user = g_list_nth_data(sg->users, i);
		char *buf = g_strdup_printf("%s@skype.com", user);
		imcb_add_buddy(ic, buf, sg->name);
		g_free(buf);
	}
}

static void skype_parse_group(struct im_connection *ic, char *line)
{
	struct skype_data *sd = ic->proto_data;
	char *id = strchr(line, ' ');

	if (!++id)
		return;

	char *info = strchr(id, ' ');

	if (!info)
		return;
	*info = '\0';
	info++;

	if (!strncmp(info, "DISPLAYNAME ", 12)) {
		info += 12;

		/* Name given for a group ID: try to update it or insert a new
		 * one if not found */
		struct skype_group *sg = skype_group_by_id(ic, atoi(id));
		if (sg) {
			g_free(sg->name);
			sg->name = g_strdup(info);
		} else {
			sg = g_new0(struct skype_group, 1);
			sg->id = atoi(id);
			sg->name = g_strdup(info);
			sd->groups = g_list_append(sd->groups, sg);
		}
	} else if (!strncmp(info, "USERS ", 6)) {
		struct skype_group *sg = skype_group_by_id(ic, atoi(id));

		if (sg) {
			char **i;
			char **users = g_strsplit(info + 6, ", ", 0);

			skype_group_free(sg, TRUE);
			i = users;
			while (*i) {
				sg->users = g_list_append(sg->users, g_strdup(*i));
				i++;
			}
			g_strfreev(users);
			skype_group_users(ic, sg);
		} else
			log_message(LOGLVL_ERROR,
				"No skype group with id %s. That's probably a bug.", id);
	} else if (!strncmp(info, "NROFUSERS ", 10)) {
		if (!sd->pending_user) {
			/* Number of users changed in this group, query its type to see
			 * if it's a custom one we should care about. */
			skype_printf(ic, "GET GROUP %s TYPE", id);
			return;
		}

		/* This is a newly created group, we have a single user
		 * to add. */
		struct skype_group *sg = skype_group_by_id(ic, atoi(id));

		if (sg) {
			skype_printf(ic, "ALTER GROUP %d ADDUSER %s", sg->id, sd->pending_user);
			g_free(sd->pending_user);
			sd->pending_user = NULL;
		} else
			log_message(LOGLVL_ERROR,
					"No skype group with id %s. That's probably a bug.", id);
	} else if (!strcmp(info, "TYPE CUSTOM_GROUP"))
		/* This one is interesting, query its users. */
		skype_printf(ic, "GET GROUP %s USERS", id);
}

static void skype_parse_chat(struct im_connection *ic, char *line)
{
	struct skype_data *sd = ic->proto_data;
	char buf[IRC_LINE_SIZE];
	char *id = strchr(line, ' ');

	if (!++id)
		return;
	struct groupchat *gc;
	char *info = strchr(id, ' ');

	if (!info)
		return;
	*info = '\0';
	info++;
	/* Remove fake chat if we created one in skype_chat_with() */
	gc = bee_chat_by_title(ic->bee, ic, "");
	if (gc)
		imcb_chat_free(gc);
	if (!strcmp(info, "STATUS MULTI_SUBSCRIBED")) {
		gc = bee_chat_by_title(ic->bee, ic, id);
		if (!gc) {
			gc = imcb_chat_new(ic, id);
			imcb_chat_name_hint(gc, id);
		}
		skype_printf(ic, "GET CHAT %s ADDER\n", id);
		skype_printf(ic, "GET CHAT %s TOPIC\n", id);
	} else if (!strcmp(info, "STATUS DIALOG") && sd->groupchat_with) {
		gc = imcb_chat_new(ic, id);
		imcb_chat_name_hint(gc, id);
		/* According to the docs this
		 * is necessary. However it
		 * does not seem the situation
		 * and it would open an extra
		 * window on our client, so
		 * just leave it out. */
		/*skype_printf(ic, "OPEN CHAT %s\n", id);*/
		g_snprintf(buf, IRC_LINE_SIZE, "%s@skype.com",
				sd->groupchat_with);
		imcb_chat_add_buddy(gc, buf);
		imcb_chat_add_buddy(gc, sd->username);
		g_free(sd->groupchat_with);
		sd->groupchat_with = NULL;
		skype_printf(ic, "GET CHAT %s ADDER\n", id);
		skype_printf(ic, "GET CHAT %s TOPIC\n", id);
	} else if (!strcmp(info, "STATUS UNSUBSCRIBED")) {
		gc = bee_chat_by_title(ic->bee, ic, id);
		if (gc)
			gc->data = (void *)FALSE;
	} else if (!strncmp(info, "ADDER ", 6)) {
		info += 6;
		g_free(sd->adder);
		sd->adder = g_strdup_printf("%s@skype.com", info);
	} else if (!strncmp(info, "TOPIC ", 6)) {
		info += 6;
		gc = bee_chat_by_title(ic->bee, ic, id);
		if (gc && (sd->adder || sd->topic_wait)) {
			if (sd->topic_wait) {
				sd->adder = g_strdup(sd->username);
				sd->topic_wait = 0;
			}
			imcb_chat_topic(gc, sd->adder, info, 0);
			g_free(sd->adder);
			sd->adder = NULL;
		}
	} else if (!strncmp(info, "MEMBERS ", 8)) {
		info += 8;
		gc = bee_chat_by_title(ic->bee, ic, id);
		/* Hack! We set ->data to TRUE
		 * while we're on the channel
		 * so that we won't rejoin
		 * after a /part. */
		if (!gc || gc->data)
			return;
		char **members = g_strsplit(info, " ", 0);
		int i;
		for (i = 0; members[i]; i++) {
			if (!strcmp(members[i], sd->username))
				continue;
			g_snprintf(buf, IRC_LINE_SIZE, "%s@skype.com",
					members[i]);
			if (!g_list_find_custom(gc->in_room, buf,
				(GCompareFunc)strcmp))
				imcb_chat_add_buddy(gc, buf);
		}
		imcb_chat_add_buddy(gc, sd->username);
		g_strfreev(members);
	}
}

static void skype_parse_password(struct im_connection *ic, char *line)
{
	if (!strncmp(line+9, "OK", 2))
		imcb_connected(ic);
	else {
		imcb_error(ic, "Authentication Failed");
		imc_logout(ic, TRUE);
	}
}

static void skype_parse_profile(struct im_connection *ic, char *line)
{
	imcb_log(ic, "SkypeOut balance value is '%s'.", line+21);
}

static void skype_parse_ping(struct im_connection *ic, char *line)
{
	/* Unused parameter */
	line = line;
	skype_printf(ic, "PONG\n");
}

static void skype_parse_chats(struct im_connection *ic, char *line)
{
	char **i;
	char **chats = g_strsplit(line + 6, ", ", 0);

	i = chats;
	while (*i) {
		skype_printf(ic, "GET CHAT %s STATUS\n", *i);
		skype_printf(ic, "GET CHAT %s ACTIVEMEMBERS\n", *i);
		i++;
	}
	g_strfreev(chats);
}

static void skype_parse_groups(struct im_connection *ic, char *line)
{
	if (!set_getbool(&ic->acc->set, "read_groups"))
		return;

	char **i;
	char **groups = g_strsplit(line + 7, ", ", 0);

	i = groups;
	while (*i) {
		skype_printf(ic, "GET GROUP %s DISPLAYNAME\n", *i);
		skype_printf(ic, "GET GROUP %s USERS\n", *i);
		i++;
	}
	g_strfreev(groups);
}

static void skype_parse_alter_group(struct im_connection *ic, char *line)
{
	char *id = line + strlen("ALTER GROUP");

	if (!++id)
		return;

	char *info = strchr(id, ' ');

	if (!info)
		return;
	*info = '\0';
	info++;

	if (!strncmp(info, "ADDUSER ", 8)) {
		struct skype_group *sg = skype_group_by_id(ic, atoi(id));

		info += 8;
		if (sg) {
			char *buf = g_strdup_printf("%s@skype.com", info);
			sg->users = g_list_append(sg->users, g_strdup(info));
			imcb_add_buddy(ic, buf, sg->name);
			g_free(buf);
		} else
			log_message(LOGLVL_ERROR,
				"No skype group with id %s. That's probably a bug.", id);
	}
}

typedef void (*skype_parser)(struct im_connection *ic, char *line);

static gboolean skype_read_callback(gpointer data, gint fd,
				    b_input_condition cond)
{
	struct im_connection *ic = data;
	struct skype_data *sd = ic->proto_data;
	char buf[IRC_LINE_SIZE];
	int st, i;
	char **lines, **lineptr, *line;
	static struct parse_map {
		char *k;
		skype_parser v;
	} parsers[] = {
		{ "USERS ", skype_parse_users },
		{ "USER ", skype_parse_user },
		{ "CHATMESSAGE ", skype_parse_chatmessage },
		{ "CALL ", skype_parse_call },
		{ "FILETRANSFER ", skype_parse_filetransfer },
		{ "CHAT ", skype_parse_chat },
		{ "GROUP ", skype_parse_group },
		{ "PASSWORD ", skype_parse_password },
		{ "PROFILE PSTN_BALANCE ", skype_parse_profile },
		{ "PING", skype_parse_ping },
		{ "CHATS ", skype_parse_chats },
		{ "GROUPS ", skype_parse_groups },
		{ "ALTER GROUP ", skype_parse_alter_group },
	};

	/* Unused parameters */
	fd = fd;
	cond = cond;

	if (!sd || sd->fd == -1)
		return FALSE;
	/* Read the whole data. */
	st = ssl_read(sd->ssl, buf, sizeof(buf));
	if (st > 0) {
		buf[st] = '\0';
		/* Then split it up to lines. */
		lines = g_strsplit(buf, "\n", 0);
		lineptr = lines;
		while ((line = *lineptr)) {
			if (!strlen(line))
				break;
			if (set_getbool(&ic->acc->set, "skypeconsole_receive"))
				imcb_buddy_msg(ic, "skypeconsole", line, 0, 0);
			for (i = 0; i < ARRAY_SIZE(parsers); i++)
				if (!strncmp(line, parsers[i].k,
					strlen(parsers[i].k))) {
					parsers[i].v(ic, line);
					break;
				}
			lineptr++;
		}
		g_strfreev(lines);
	} else if (st == 0 || (st < 0 && !sockerr_again())) {
		ssl_disconnect(sd->ssl);
		sd->fd = -1;
		sd->ssl = NULL;

		imcb_error(ic, "Error while reading from server");
		imc_logout(ic, TRUE);
		return FALSE;
	}
	return TRUE;
}

gboolean skype_start_stream(struct im_connection *ic)
{
	struct skype_data *sd = ic->proto_data;
	int st;

	if (!sd)
		return FALSE;

	if (sd->bfd <= 0)
		sd->bfd = b_input_add(sd->fd, B_EV_IO_READ,
			skype_read_callback, ic);

	/* Log in */
	skype_printf(ic, "USERNAME %s\n", ic->acc->user);
	skype_printf(ic, "PASSWORD %s\n", ic->acc->pass);

	/* This will download all buddies and groups. */
	st = skype_printf(ic, "SEARCH GROUPS CUSTOM\n");
	skype_printf(ic, "SEARCH FRIENDS\n");

	skype_printf(ic, "SET USERSTATUS ONLINE\n");

	/* Auto join to bookmarked chats if requested.*/
	if (set_getbool(&ic->acc->set, "auto_join"))
		skype_printf(ic, "SEARCH BOOKMARKEDCHATS\n");
	return st;
}

gboolean skype_connected(gpointer data, int returncode, void *source, b_input_condition cond)
{
	struct im_connection *ic = data;
	struct skype_data *sd = ic->proto_data;

	/* Unused parameter */
	cond = cond;

	if (!source) {
		sd->ssl = NULL;
		imcb_error(ic, "Could not connect to server");
		imc_logout(ic, TRUE);
		return FALSE;
	}
	imcb_log(ic, "Connected to server, logging in");

	return skype_start_stream(ic);
}

static void skype_login(account_t *acc)
{
	struct im_connection *ic = imcb_new(acc);
	struct skype_data *sd = g_new0(struct skype_data, 1);

	ic->proto_data = sd;

	imcb_log(ic, "Connecting");
	sd->ssl = ssl_connect(set_getstr(&acc->set, "server"),
		set_getint(&acc->set, "port"), FALSE, skype_connected, ic);
	sd->fd = sd->ssl ? ssl_getfd(sd->ssl) : -1;
	sd->username = g_strdup(acc->user);

	sd->ic = ic;

	if (set_getbool(&acc->set, "skypeconsole"))
		imcb_add_buddy(ic, "skypeconsole", NULL);
}

static void skype_logout(struct im_connection *ic)
{
	struct skype_data *sd = ic->proto_data;
	int i;

	skype_printf(ic, "SET USERSTATUS OFFLINE\n");

	while (ic->groupchats)
		imcb_chat_free(ic->groupchats->data);

	for (i = 0; i < g_list_length(sd->groups); i++) {
		struct skype_group *sg = (struct skype_group *)g_list_nth_data(sd->groups, i);
		skype_group_free(sg, FALSE);
	}

	if (sd->ssl)
		ssl_disconnect(sd->ssl);

	g_free(sd->username);
	g_free(sd->handle);
	g_free(sd);
	ic->proto_data = NULL;
}

static int skype_buddy_msg(struct im_connection *ic, char *who, char *message,
			   int flags)
{
	char *ptr, *nick;
	int st;

	/* Unused parameter */
	flags = flags;

	nick = g_strdup(who);
	ptr = strchr(nick, '@');
	if (ptr)
		*ptr = '\0';

	if (!strncmp(who, "skypeconsole", 12))
		st = skype_printf(ic, "%s\n", message);
	else
		st = skype_printf(ic, "MESSAGE %s %s\n", nick, message);
	g_free(nick);

	return st;
}

const struct skype_away_state *skype_away_state_by_name(char *name)
{
	int i;

	for (i = 0; skype_away_state_list[i].full_name; i++)
		if (g_strcasecmp(skype_away_state_list[i].full_name, name) == 0)
			return skype_away_state_list + i;

	return NULL;
}

static void skype_set_away(struct im_connection *ic, char *state_txt,
			   char *message)
{
	const struct skype_away_state *state;

	/* Unused parameter */
	message = message;

	if (state_txt == NULL)
		state = skype_away_state_by_name("Online");
	else
		state = skype_away_state_by_name(state_txt);
	skype_printf(ic, "SET USERSTATUS %s\n", state->code);
}

static GList *skype_away_states(struct im_connection *ic)
{
	static GList *l;
	int i;

	/* Unused parameter */
	ic = ic;

	if (l == NULL)
		for (i = 0; skype_away_state_list[i].full_name; i++)
			l = g_list_append(l,
				(void *)skype_away_state_list[i].full_name);

	return l;
}

static char *skype_set_display_name(set_t *set, char *value)
{
	account_t *acc = set->data;
	struct im_connection *ic = acc->ic;

	skype_printf(ic, "SET PROFILE FULLNAME %s", value);
	return value;
}

static char *skype_set_mood_text(set_t *set, char *value)
{
	account_t *acc = set->data;
	struct im_connection *ic = acc->ic;

	skype_printf(ic, "SET PROFILE MOOD_TEXT %s", value);
	return value;
}

static char *skype_set_balance(set_t *set, char *value)
{
	account_t *acc = set->data;
	struct im_connection *ic = acc->ic;

	skype_printf(ic, "GET PROFILE PSTN_BALANCE");
	return value;
}

static void skype_call(struct im_connection *ic, char *value)
{
	char *nick = g_strdup(value);
	char *ptr = strchr(nick, '@');

	if (ptr)
		*ptr = '\0';
	skype_printf(ic, "CALL %s", nick);
	g_free(nick);
}

static void skype_hangup(struct im_connection *ic)
{
	struct skype_data *sd = ic->proto_data;

	if (sd->call_id) {
		skype_printf(ic, "SET CALL %s STATUS FINISHED",
				sd->call_id);
		g_free(sd->call_id);
		sd->call_id = 0;
	} else
		imcb_error(ic, "There are no active calls currently.");
}

static char *skype_set_call(set_t *set, char *value)
{
	account_t *acc = set->data;
	struct im_connection *ic = acc->ic;

	if (value)
		skype_call(ic, value);
	else
		skype_hangup(ic);
	return value;
}

static void skype_add_buddy(struct im_connection *ic, char *who, char *group)
{
	struct skype_data *sd = ic->proto_data;
	char *nick, *ptr;

	nick = g_strdup(who);
	ptr = strchr(nick, '@');
	if (ptr)
		*ptr = '\0';

	if (!group) {
		skype_printf(ic, "SET USER %s BUDDYSTATUS 2 Please authorize me\n",
				nick);
		g_free(nick);
	} else {
		struct skype_group *sg = skype_group_by_name(ic, group);

		if (!sg) {
			/* No such group, we need to create it, then have to
			 * add the user once it's created. */
			skype_printf(ic, "CREATE GROUP %s", group);
			sd->pending_user = g_strdup(nick);
		} else {
			skype_printf(ic, "ALTER GROUP %d ADDUSER %s", sg->id, nick);
		}
	}
}

static void skype_remove_buddy(struct im_connection *ic, char *who, char *group)
{
	char *nick, *ptr;

	/* Unused parameter */
	group = group;

	nick = g_strdup(who);
	ptr = strchr(nick, '@');
	if (ptr)
		*ptr = '\0';
	skype_printf(ic, "SET USER %s BUDDYSTATUS 1\n", nick);
	g_free(nick);
}

void skype_chat_msg(struct groupchat *gc, char *message, int flags)
{
	struct im_connection *ic = gc->ic;

	/* Unused parameter */
	flags = flags;

	skype_printf(ic, "CHATMESSAGE %s %s\n", gc->title, message);
}

void skype_chat_leave(struct groupchat *gc)
{
	struct im_connection *ic = gc->ic;
	skype_printf(ic, "ALTER CHAT %s LEAVE\n", gc->title);
	gc->data = (void *)TRUE;
}

void skype_chat_invite(struct groupchat *gc, char *who, char *message)
{
	struct im_connection *ic = gc->ic;
	char *ptr, *nick;

	nick = g_strdup(who);
	ptr = strchr(nick, '@');
	if (ptr)
		*ptr = '\0';
	skype_printf(ic, "ALTER CHAT %s ADDMEMBERS %s\n", gc->title, nick);
	g_free(nick);
}

void skype_chat_topic(struct groupchat *gc, char *message)
{
	struct im_connection *ic = gc->ic;
	struct skype_data *sd = ic->proto_data;
	skype_printf(ic, "ALTER CHAT %s SETTOPIC %s\n",
		gc->title, message);
	sd->topic_wait = 1;
}

struct groupchat *skype_chat_with(struct im_connection *ic, char *who)
{
	struct skype_data *sd = ic->proto_data;
	char *ptr, *nick;
	nick = g_strdup(who);
	ptr = strchr(nick, '@');
	if (ptr)
		*ptr = '\0';
	skype_printf(ic, "CHAT CREATE %s\n", nick);
	sd->groupchat_with = g_strdup(nick);
	g_free(nick);
	/* We create a fake chat for now. We will replace it with a real one in
	 * the real callback. */
	return imcb_chat_new(ic, "");
}

static void skype_get_info(struct im_connection *ic, char *who)
{
	char *ptr, *nick;
	nick = g_strdup(who);
	ptr = strchr(nick, '@');
	if (ptr)
		*ptr = '\0';
	skype_printf(ic, "GET USER %s FULLNAME\n", nick);
	skype_printf(ic, "GET USER %s PHONE_HOME\n", nick);
	skype_printf(ic, "GET USER %s PHONE_OFFICE\n", nick);
	skype_printf(ic, "GET USER %s PHONE_MOBILE\n", nick);
	skype_printf(ic, "GET USER %s NROF_AUTHED_BUDDIES\n", nick);
	skype_printf(ic, "GET USER %s TIMEZONE\n", nick);
	skype_printf(ic, "GET USER %s LASTONLINETIMESTAMP\n", nick);
	skype_printf(ic, "GET USER %s SEX\n", nick);
	skype_printf(ic, "GET USER %s LANGUAGE\n", nick);
	skype_printf(ic, "GET USER %s COUNTRY\n", nick);
	skype_printf(ic, "GET USER %s PROVINCE\n", nick);
	skype_printf(ic, "GET USER %s CITY\n", nick);
	skype_printf(ic, "GET USER %s HOMEPAGE\n", nick);
	skype_printf(ic, "GET USER %s ABOUT\n", nick);
	/*
	 * Hack: we query the bithday property which is always a single line,
	 * so we can send the collected properties to the user when we have
	 * this one.
	 */
	skype_printf(ic, "GET USER %s BIRTHDAY\n", nick);
}

static void skype_set_my_name(struct im_connection *ic, char *info)
{
	skype_set_display_name(set_find(&ic->acc->set, "display_name"), info);
}

static void skype_init(account_t *acc)
{
	set_t *s;

	s = set_add(&acc->set, "server", SKYPE_DEFAULT_SERVER, set_eval_account,
		acc);
	s->flags |= ACC_SET_OFFLINE_ONLY;

	s = set_add(&acc->set, "port", SKYPE_DEFAULT_PORT, set_eval_int, acc);
	s->flags |= ACC_SET_OFFLINE_ONLY;

	s = set_add(&acc->set, "display_name", NULL, skype_set_display_name,
		acc);
	s->flags |= ACC_SET_NOSAVE | ACC_SET_ONLINE_ONLY;

	s = set_add(&acc->set, "mood_text", NULL, skype_set_mood_text, acc);
	s->flags |= ACC_SET_NOSAVE | ACC_SET_ONLINE_ONLY;

	s = set_add(&acc->set, "call", NULL, skype_set_call, acc);
	s->flags |= ACC_SET_NOSAVE | ACC_SET_ONLINE_ONLY;

	s = set_add(&acc->set, "balance", NULL, skype_set_balance, acc);
	s->flags |= ACC_SET_NOSAVE | ACC_SET_ONLINE_ONLY;

	s = set_add(&acc->set, "skypeout_offline", "true", set_eval_bool, acc);

	s = set_add(&acc->set, "skypeconsole", "false", set_eval_bool, acc);
	s->flags |= ACC_SET_OFFLINE_ONLY;

	s = set_add(&acc->set, "skypeconsole_receive", "false", set_eval_bool,
		acc);
	s->flags |= ACC_SET_OFFLINE_ONLY;

	s = set_add(&acc->set, "auto_join", "false", set_eval_bool, acc);
	s->flags |= ACC_SET_OFFLINE_ONLY;

	s = set_add(&acc->set, "test_join", "false", set_eval_bool, acc);
	s->flags |= ACC_SET_OFFLINE_ONLY;

	s = set_add(&acc->set, "show_moods", "false", set_eval_bool, acc);

	s = set_add(&acc->set, "edit_prefix", "EDIT:",
			NULL, acc);

	s = set_add(&acc->set, "read_groups", "false", set_eval_bool, acc);
}

#if BITLBEE_VERSION_CODE > BITLBEE_VER(3, 0, 1)
GList *skype_buddy_action_list(bee_user_t *bu)
{
	static GList *ret;

	/* Unused parameter */
	bu = bu;

	if (ret == NULL) {
		static const struct buddy_action ba[3] = {
			{"CALL", "Initiate a call" },
			{"HANGUP", "Hang up a call" },
		};

		ret = g_list_prepend(ret, (void *) ba + 0);
	}

	return ret;
}

void *skype_buddy_action(struct bee_user *bu, const char *action, char * const args[], void *data)
{
	/* Unused parameters */
	args = args;
	data = data;

	if (!g_strcasecmp(action, "CALL"))
		skype_call(bu->ic, bu->handle);
	else if (!g_strcasecmp(action, "HANGUP"))
		skype_hangup(bu->ic);

	return NULL;
}
#endif

void init_plugin(void)
{
	struct prpl *ret = g_new0(struct prpl, 1);

	ret->name = "skype";
	ret->login = skype_login;
	ret->init = skype_init;
	ret->logout = skype_logout;
	ret->buddy_msg = skype_buddy_msg;
	ret->get_info = skype_get_info;
	ret->set_my_name = skype_set_my_name;
	ret->away_states = skype_away_states;
	ret->set_away = skype_set_away;
	ret->add_buddy = skype_add_buddy;
	ret->remove_buddy = skype_remove_buddy;
	ret->chat_msg = skype_chat_msg;
	ret->chat_leave = skype_chat_leave;
	ret->chat_invite = skype_chat_invite;
	ret->chat_with = skype_chat_with;
	ret->handle_cmp = g_strcasecmp;
	ret->chat_topic = skype_chat_topic;
#if BITLBEE_VERSION_CODE > BITLBEE_VER(3, 0, 1)
	ret->buddy_action_list = skype_buddy_action_list;
	ret->buddy_action = skype_buddy_action;
#endif
	register_protocol(ret);
}
