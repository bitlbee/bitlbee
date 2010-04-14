  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2005 Wilmer van der Gaast and others                *
  \********************************************************************/

/* Logging services for the bee 			*/

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

#define BITLBEE_CORE
#include "bitlbee.h"
#include <syslog.h>

static log_t logoutput;

static void log_null(int level, char *logmessage);
static void log_irc(int level, char *logmessage);
static void log_syslog(int level, char *logmessage);
static void log_console(int level, char *logmessage);

void log_init(void) {
	openlog("bitlbee", LOG_PID, LOG_DAEMON);	

	logoutput.informational = &log_null;
	logoutput.warning = &log_null;
	logoutput.error = &log_null;
#ifdef DEBUG
	logoutput.debug = &log_null;
#endif

	return;
}

void log_link(int level, int output) {
	/* I know it's ugly, but it works and I didn't feel like messing with pointer to function pointers */

	if(level == LOGLVL_INFO) {
		if(output == LOGOUTPUT_NULL)
			logoutput.informational = &log_null;	
		else if(output == LOGOUTPUT_IRC)
			logoutput.informational = &log_irc;	
		else if(output == LOGOUTPUT_SYSLOG)
			logoutput.informational = &log_syslog;	
		else if(output == LOGOUTPUT_CONSOLE) 
			logoutput.informational = &log_console;	
	}
	else if(level == LOGLVL_WARNING) {
		if(output == LOGOUTPUT_NULL)
			logoutput.warning = &log_null;
		else if(output == LOGOUTPUT_IRC)
			logoutput.warning = &log_irc;
		else if(output == LOGOUTPUT_SYSLOG)
			logoutput.warning = &log_syslog;
		else if(output == LOGOUTPUT_CONSOLE)
			logoutput.warning = &log_console;
	}
	else if(level == LOGLVL_ERROR) {
		if(output == LOGOUTPUT_NULL)
			logoutput.error = &log_null;
		else if(output == LOGOUTPUT_IRC)
			logoutput.error = &log_irc;
		else if(output == LOGOUTPUT_SYSLOG)
			logoutput.error = &log_syslog;
		else if(output == LOGOUTPUT_CONSOLE)
			logoutput.error = &log_console;
	}
#ifdef DEBUG
	else if(level == LOGLVL_DEBUG) {
		if(output == LOGOUTPUT_NULL)
			logoutput.debug = &log_null;
		else if(output == LOGOUTPUT_IRC)
			logoutput.debug = &log_irc;
		else if(output == LOGOUTPUT_SYSLOG)
			logoutput.debug = &log_syslog;
		else if(output == LOGOUTPUT_CONSOLE)
			logoutput.debug = &log_console;
	}
#endif
	return;	

}

void log_message(int level, char *message, ... ) {

	va_list ap;
	char *msgstring;

	va_start(ap, message);
	msgstring = g_strdup_vprintf(message, ap);
	va_end(ap);

	if(level == LOGLVL_INFO)
		(*(logoutput.informational))(level, msgstring);
	if(level == LOGLVL_WARNING) 
		(*(logoutput.warning))(level, msgstring);
	if(level == LOGLVL_ERROR)
		(*(logoutput.error))(level, msgstring);
#ifdef DEBUG
	if(level == LOGLVL_DEBUG)
		(*(logoutput.debug))(level, msgstring);
#endif

	g_free(msgstring);
	
	return;
}

void log_error(char *functionname) {
	log_message(LOGLVL_ERROR, "%s: %s", functionname, strerror(errno));
	
	return;
}

static void log_null(int level, char *message) {
	return;
}

static void log_irc(int level, char *message) {
	if(level == LOGLVL_ERROR)
		irc_write_all(1, "ERROR :Error: %s", message);
	if(level == LOGLVL_WARNING)
		irc_write_all(0, "ERROR :Warning: %s", message);
	if(level == LOGLVL_INFO)
		irc_write_all(0, "ERROR :Informational: %s", message);	
#ifdef DEBUG
	if(level == LOGLVL_DEBUG)
		irc_write_all(0, "ERROR :Debug: %s", message);	
#endif	

	return;
}

static void log_syslog(int level, char *message) {
	if(level == LOGLVL_ERROR)
		syslog(LOG_ERR, "%s", message);
	if(level == LOGLVL_WARNING)
		syslog(LOG_WARNING, "%s", message);
	if(level == LOGLVL_INFO)
		syslog(LOG_INFO, "%s", message);
#ifdef DEBUG
	if(level == LOGLVL_DEBUG)
		syslog(LOG_DEBUG, "%s", message);
#endif
	return;
}

static void log_console(int level, char *message) {
	if(level == LOGLVL_ERROR)
		fprintf(stderr, "Error: %s\n", message);
	if(level == LOGLVL_WARNING)
		fprintf(stderr, "Warning: %s\n", message);
	if(level == LOGLVL_INFO)
		fprintf(stdout, "Informational: %s\n", message);
#ifdef DEBUG
	if(level == LOGLVL_DEBUG)
		fprintf(stdout, "Debug: %s\n", message);
#endif
	/* Always log stuff in syslogs too. */
	log_syslog(level, message);
	return;
}
