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

/* Hmm... Linked list? Plleeeeaaase?? ;-) */

typedef struct command_t
{
	char *command;
	int required_parameters;
	int (*execute)(irc_t *, char **args);
} command_t;

int cmd_account( irc_t *irc, char **cmd );
int cmd_help( irc_t *irc, char **args);
int cmd_info( irc_t *irc, char **args);
int cmd_add( irc_t *irc, char **args) ;
int cmd_rename( irc_t *irc, char **args );
int cmd_remove( irc_t *irc, char **args );
int cmd_block( irc_t *irc, char **args );
int cmd_allow( irc_t *irc, char **args );
int cmd_save( irc_t *irc, char **args );
int cmd_set( irc_t *irc, char **args );
int cmd_yesno( irc_t *irc, char **args );
int cmd_identify( irc_t *irc, char **args );
int cmd_register( irc_t *irc, char **args );
int cmd_drop( irc_t *irc, char **args );
int cmd_blist( irc_t *irc, char **cmd );
int cmd_nick( irc_t *irc, char **cmd );
int cmd_qlist( irc_t *irc, char **cmd );
int cmd_import_buddies( irc_t *irc, char **cmd );
int cmd_dump( irc_t *irc, char **cmd );



extern command_t commands[];

#endif
