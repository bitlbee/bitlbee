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

/* Callback function called twice during the access token request process.
   Return FALSE if something broke and the process must be aborted. */
typedef gboolean (*oauth_cb)( struct oauth_info * );

typedef enum
{
	OAUTH_INIT,
	OAUTH_REQUEST_TOKEN,
	OAUTH_ACCESS_TOKEN,
} oauth_stage_t;

struct oauth_info
{
	oauth_stage_t stage;
	const struct oauth_service *sp;
	
	oauth_cb func;
	void *data;
	
	struct http_request *http;
	
	char *auth_url;
	char *request_token;
	
	char *token;
	char *token_secret;
	GSList *params;
};

struct oauth_service
{
	char *url_request_token;
	char *url_access_token;
	char *url_authorize;
	
	char *consumer_key;
	char *consumer_secret;
};

/* http://oauth.net/core/1.0a/#auth_step1 (section 6.1) 
   Request an initial anonymous token which can be used to construct an
   authorization URL for the user. This is passed to the callback function
   in a struct oauth_info. */
struct oauth_info *oauth_request_token( const struct oauth_service *sp, oauth_cb func, void *data );

/* http://oauth.net/core/1.0a/#auth_step3 (section 6.3)
   The user gets a PIN or so which we now exchange for the final access
   token. This is passed to the callback function in the same
   struct oauth_info. */
gboolean oauth_access_token( const char *pin, struct oauth_info *st );

/* http://oauth.net/core/1.0a/#anchor12 (section 7)
   Generate an OAuth Authorization: HTTP header. access_token should be
   saved/fetched using the functions above. args can be a string with
   whatever's going to be in the POST body of the request. GET args will
   automatically be grabbed from url. */
char *oauth_http_header( struct oauth_info *oi, const char *method, const char *url, char *args );

/* Shouldn't normally be required unless the process is aborted by the user. */
void oauth_info_free( struct oauth_info *info );

/* Convert to and back from strings, for easier saving. */
char *oauth_to_string( struct oauth_info *oi );
struct oauth_info *oauth_from_string( char *in, const struct oauth_service *sp );

/* For reading misc. data. */
void oauth_params_add( GSList **params, const char *key, const char *value );
void oauth_params_parse( GSList **params, char *in );
void oauth_params_free( GSList **params );
char *oauth_params_string( GSList *params );
void oauth_params_set( GSList **params, const char *key, const char *value );
const char *oauth_params_get( GSList **params, const char *key );
