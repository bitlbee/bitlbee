  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2004 Wilmer van der Gaast and others                *
  \********************************************************************/

/* User manager (root) commands                                         */

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

#ifndef _COMMANDS_H
#define _COMMANDS_H

#include "bitlbee.h"

typedef struct command
{
	char *command;
	int required_parameters;
	void (*execute)(irc_t *, char **args);
	int flags;
} command_t;

extern command_t root_commands[];

#define IRC_CMD_PRE_LOGIN	1
#define IRC_CMD_LOGGED_IN	2
#define IRC_CMD_OPER_ONLY	4
#define IRC_CMD_TO_MASTER	8

#define IPC_CMD_TO_CHILDREN	1

#endif
