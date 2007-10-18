  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2004 Wilmer van der Gaast and others                *
  \********************************************************************/

/* Configuration reading code						*/

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

#ifndef __CONF_H
#define __CONF_H

typedef enum runmode { RUNMODE_DAEMON, RUNMODE_FORKDAEMON, RUNMODE_INETD } runmode_t;
typedef enum authmode { AUTHMODE_OPEN, AUTHMODE_CLOSED, AUTHMODE_REGISTERED } authmode_t;

typedef struct conf
{
	char *iface;
	char *port;
	int nofork;
	int verbose;
	runmode_t runmode;
	authmode_t authmode;
	char *auth_pass;
	char *oper_pass;
	char *hostname;
	char *configdir;
	char *plugindir;
	char *pidfile;
	char *motdfile;
	char *primary_storage;
	char **migrate_storage;
	int ping_interval;
	int ping_timeout;
} conf_t;

G_GNUC_MALLOC conf_t *conf_load( int argc, char *argv[] );
void conf_loaddefaults( irc_t *irc );

#endif
