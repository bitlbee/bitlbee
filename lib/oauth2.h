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

struct oauth2_info;

/* Callback function called twice during the access token request process.
   Return FALSE if something broke and the process must be aborted. */
typedef gboolean (*oauth_cb)( struct oauth2_info * );

struct oauth2_info
{
	const struct oauth_service *sp;
	
	oauth_cb func;
	void *data;
	
	struct http_request *http;
	
//	char *auth_url;
//	char *request_token;
	
//	char *token;
//	char *token_secret;
//	GSList *params;
};

struct oauth2_service
{
	char *base_url;
	char *consumer_key;
	char *consumer_secret;
};

extern struct oauth2_service oauth2_service_google;

/* http://oauth.net/core/1.0a/#auth_step1 (section 6.1) 
   Request an initial anonymous token which can be used to construct an
   authorization URL for the user. This is passed to the callback function
   in a struct oauth2_info. */
char *oauth2_url( const struct oauth2_service *sp, const char *scope );

/* http://oauth.net/core/1.0a/#auth_step3 (section 6.3)
   The user gets a PIN or so which we now exchange for the final access
   token. This is passed to the callback function in the same
   struct oauth2_info. */
gboolean oauth2_access_token( const char *pin, struct oauth2_info *st );

/* Shouldn't normally be required unless the process is aborted by the user. */
void oauth2_info_free( struct oauth2_info *info );
