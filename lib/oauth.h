/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Simple OAuth client (consumer) implementation.                           *
*                                                                           *
*  Copyright 2010 Wilmer van der Gaast <wilmer@gaast.net>                   *
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

/* http://oauth.net/core/1.0a/ */

struct oauth_info;
typedef void (*oauth_cb)( struct oauth_info * );

struct oauth_info
{
	oauth_cb func;
	void *data;
	
	struct http_request *http;
	
	char *auth_params;
	char *token;
	
	char *access_token;
};

/* http://oauth.net/core/1.0a/#auth_step1 (section 6.1) 
   Request an initial anonymous token which can be used to construct an
   authorization URL for the user. This is passed to the callback function
   in a struct oauth_info. */
void *oauth_request_token( const char *url, oauth_cb func, void *data );

/* http://oauth.net/core/1.0a/#auth_step3 (section 6.3)
   The user gets a PIN or so which we now exchange for the final access
   token. This is passed to the callback function in the same
   struct oauth_info. */
void *oauth_access_token( const char *url, const char *pin, struct oauth_info *st );

/* http://oauth.net/core/1.0a/#anchor12 (section 7)
   Generate an OAuth Authorization: HTTP header. access_token should be
   saved/fetched using the functions above. args can be a string with
   whatever's going to be in the POST body of the request. GET args will
   automatically be grabbed from url. */
char *oauth_http_header( char *access_token, const char *method, const char *url, char *args );
