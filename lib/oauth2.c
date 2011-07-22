/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Simple OAuth client (consumer) implementation.                           *
*                                                                           *
*  Copyright 2010-2011 Wilmer van der Gaast <wilmer@gaast.net>              *
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

#include <glib.h>
#include "oauth2.h"

struct oauth2_service oauth2_service_google =
{
	"https://accounts.google.com/o/oauth2/",
	"783993391592.apps.googleusercontent.com",
	"k5_EV4EQ7jEVCEk3WBwEFfuW",
};

char *oauth2_url( const struct oauth2_service *sp, const char *scope )
{
	return g_strconcat( sp->base_url, "auth"
	                    "?scope=", scope,
	                    "&response_type=code"
	                    "&redirect_uri=urn:ietf:wg:oauth:2.0:oob",
	                    "&client_id=", sp->consumer_key,
	                    NULL );
}
