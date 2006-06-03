#ifndef __PASSPORT_H__
#define __PASSPORT_H__
/* passport.h
 *
 * Functions to login to Microsoft Passport Service for Messenger
 * Copyright (C) 2004 Wouter Paesen <wouter@blue-gate.be>,
 *                    Wilmer van der Gaast <wilmer@gaast.net>
 *
 * This program is free software; you can redistribute it and/or modify             
 * it under the terms of the GNU General Public License version 2                   
 * as published by the Free Software Foundation                                     
 *                                                                                   
 * This program is distributed in the hope that is will be useful,                  
 * bit WITHOU ANY WARRANTY; without even the implied warranty of                   
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                    
 * GNU General Public License for more details.                                     
 *                                                                                   
 * You should have received a copy of the GNU General Public License                
 * along with this program; if not, write to the Free Software                      
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA          
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif
#include "nogaim.h"

struct passport_reply
{
	void (*func)( struct passport_reply * );
	void *data;
	char *result;
	char *header;
	char *error_string;
};

int passport_get_id( gpointer func, gpointer data, char *username, char *password, char *cookie );

#endif /* __PASSPORT_H__ */
