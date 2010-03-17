/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Utility functions for file transfer                                      *
*                                                                           *
*  Copyright 2008 Uli Meis <a.sporto+bee@gmail.com>                         *
*                                                                           *
*  This program is free software; you can redistribute it and/or modify     *
*  it under the terms of the GNU General Public License as published by     *
*  the Free Software Foundation; either version 2 of the License, or        *
*  (at your option) any later version.                                      *
*                                                                           *
*  This program is distributed in the hope that it will be useful,          *
*  but WITHOUT ANY WARRANTY; without even the implied warranty of           *
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
*  GNU General Public License for more details.                             *
*                                                                           *
*  You should have received a copy of the GNU General Public License along  *
*  with this program; if not, write to the Free Software Foundation, Inc.,  *
*  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.              *
*                                                                           *
\***************************************************************************/

#ifndef AI_NUMERICSERV
#define AI_NUMERICSERV 0x0400   /* Don't use name resolution.  */
#endif

/* Some ifdefs for ulibc and apparently also BSD (Thanks to Whoopie) */
#ifndef HOST_NAME_MAX
#include <sys/param.h>
#ifdef MAXHOSTNAMELEN
#define HOST_NAME_MAX MAXHOSTNAMELEN
#else
#define HOST_NAME_MAX 255
#endif
#endif

/* This function should be used with care. host should be AT LEAST a
   char[HOST_NAME_MAX+1] and port AT LEAST a char[6]. */
int ft_listen( struct sockaddr_storage *saddr_ptr, char *host, char *port, int for_bitlbee_client, char **errptr );
