/********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2005 Wilmer van der Gaast and others                *
  \********************************************************************/

/* Logging services for the bee                               */

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

#ifndef _LOG_H
#define _LOG_H

typedef enum {
	LOGLVL_INFO,
	LOGLVL_WARNING,
	LOGLVL_ERROR,
#ifdef DEBUG
	LOGLVL_DEBUG,
#endif
} loglvl_t;

typedef enum {
	LOGOUTPUT_NULL,
	LOGOUTPUT_IRC,
	LOGOUTPUT_SYSLOG,
	LOGOUTPUT_CONSOLE,
} logoutput_t;

typedef struct log_t {
	void (*error)(int level, const char *logmessage);
	void (*warning)(int level, const char *logmessage);
	void (*informational)(int level, const char *logmessage);
#ifdef DEBUG
	void (*debug)(int level, const char *logmessage);
#endif
} log_t;

void log_init(void);
void log_link(int level, int output);
void log_message(int level, const char *message, ...) G_GNUC_PRINTF(2, 3);
void log_error(const char *functionname);

#endif
