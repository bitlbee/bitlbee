/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Simple module to facilitate twitter functionality.                       *
*                                                                           *
*  Copyright 2009 Geert Mulders <g.c.w.m.mulders@gmail.com>                 *
*                                                                           *
*  This library is free software; you can redistribute it and/or            *
*  modify it under the terms of the GNU Lesser General Public               *
*  License as published by the Free Software Foundation, version            *
*  2.1.                                                                     *
*                                                                           *
*  This library is distributed in the hope that it will be useful,          *
*  but WITHOUT ANY WARRANTY; without even the implied warranty of           *
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU        *
*  Lesser General Public License for more details.                          *
*                                                                           *
*  You should have received a copy of the GNU Lesser General Public License *
*  along with this library; if not, write to the Free Software Foundation,  *
*  Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA           *
*                                                                           *
****************************************************************************/

#include "nogaim.h"

#ifndef _TWITTER_H
#define _TWITTER_H

#ifdef DEBUG_TWITTER
#define debug( text... ) imcb_log( ic, text );
#else
#define debug( text... )
#endif

struct twitter_data
{
	char* user;
	char* pass;
	struct oauth_info *oauth_info;
	guint64 home_timeline_id;
	gint main_loop_id;
	struct groupchat *home_timeline_gc;
	gint http_fails;
};

/**
 * This has the same function as the msn_connections GSList. We use this to 
 * make sure the connection is still alive in callbacks before we do anything
 * else.
 */
GSList *twitter_connections;

#endif //_TWITTER_H
