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
#include "http_client.h"
#include "oauth2.h"
#include "oauth.h"
#include "url.h"

struct oauth2_service oauth2_service_google =
{
	"https://accounts.google.com/o/oauth2/auth",
	"https://accounts.google.com/o/oauth2/token",
	"urn:ietf:wg:oauth:2.0:oob",
	"783993391592.apps.googleusercontent.com",
	"k5_EV4EQ7jEVCEk3WBwEFfuW",
};
struct oauth2_service oauth2_service_facebook =
{
	"https://www.facebook.com/dialog/oauth",
	"https://graph.facebook.com/oauth/access_token",
	"http://www.bitlbee.org/main.php/fb.html",
	"126828914005625",
	"4b100f0f244d620bf3f15f8b217d4c32",
};

char *oauth2_url( const struct oauth2_service *sp, const char *scope )
{
	return g_strconcat( sp->auth_url,
	                    "?scope=", scope,
	                    "&response_type=code"
	                    "&redirect_uri=", sp->redirect_url, 
	                    "&client_id=", sp->consumer_key,
	                    NULL );
}

struct oauth2_access_token_data
{
	oauth2_token_callback func;
	gpointer data;
};

static char *oauth2_json_dumb_get( const char *json, const char *key );
static void oauth2_access_token_done( struct http_request *req );

int oauth2_access_token( const struct oauth2_service *sp,
                         const char *auth_type, const char *auth,
                         oauth2_token_callback func, gpointer data )
{
	GSList *args = NULL;
	char *args_s, *s;
	url_t url_p;
	struct http_request *req;
	struct oauth2_access_token_data *cb_data;
	
	if( !url_set( &url_p, sp->token_url ) )
		return 0;
	
	oauth_params_add( &args, "client_id", sp->consumer_key );
	oauth_params_add( &args, "client_secret", sp->consumer_secret );
	oauth_params_add( &args, "grant_type", auth_type );
	if( strcmp( auth_type, OAUTH2_AUTH_CODE ) == 0 )
	{
		oauth_params_add( &args, "redirect_uri", sp->redirect_url );
		oauth_params_add( &args, "code", auth );
	}
	else
	{
		oauth_params_add( &args, "refresh_token", auth );
	}
	args_s = oauth_params_string( args );
	oauth_params_free( &args );
	
	s = g_strdup_printf( "POST %s HTTP/1.0\r\n"
	                     "Host: %s\r\n"
	                     "Content-Type: application/x-www-form-urlencoded\r\n"
	                     "Content-Length: %zd\r\n"
	                     "Connection: close\r\n"
	                     "\r\n"
	                     "%s", url_p.file, url_p.host, strlen( args_s ), args_s );
	g_free( args_s );
	
	cb_data = g_new0( struct oauth2_access_token_data, 1 );
	cb_data->func = func;
	cb_data->data = data;
	
	req = http_dorequest( url_p.host, url_p.port, url_p.proto == PROTO_HTTPS,
	                      s, oauth2_access_token_done, cb_data );
	
	g_free( s );
	
	if( req == NULL )
		g_free( cb_data );
	
	return req != NULL;
}

static void oauth2_access_token_done( struct http_request *req )
{
	struct oauth2_access_token_data *cb_data = req->data;
	char *atoken = NULL, *rtoken = NULL;
	
	if( req->status_code == 200 )
	{
		atoken = oauth2_json_dumb_get( req->reply_body, "access_token" );
		rtoken = oauth2_json_dumb_get( req->reply_body, "refresh_token" );
	}
	cb_data->func( cb_data->data, atoken, rtoken );
	g_free( atoken );
	g_free( rtoken );
	g_free( cb_data );
}

/* Super dumb. I absolutely refuse to use/add a complete json parser library
   (adding a new dependency to BitlBee for the first time in.. 6 years?) just
   to parse 100 bytes of data. So I have to do my own parsing because OAuth2
   dropped support for XML. (GRRR!) This is very dumb and for example won't
   work for integer values, nor will it strip/handle backslashes. */
static char *oauth2_json_dumb_get( const char *json, const char *key )
{
	int is_key; /* 1 == reading key, 0 == reading value */
	int found_key = 0;
		
	while( json && *json )
	{
		/* Grab strings and see if they're what we're looking for. */
		if( *json == '"' || *json == '\'' )
		{
			char q = *json;
			const char *str_start;
			json ++;
			str_start = json;
			
			while( *json )
			{
				/* \' and \" are not string terminators. */
				if( *json == '\\' && json[1] == q )
					json ++;
				/* But without a \ it is. */
				else if( *json == q )
					break;
				json ++;
			}
			if( *json == '\0' )
				return NULL;
			
			if( is_key && strncmp( str_start, key, strlen( key ) ) == 0 )
			{
				found_key = 1;
			}
			else if( !is_key && found_key )
			{
				char *ret = g_memdup( str_start, json - str_start + 1 );
				ret[json-str_start] = '\0';
				return ret;
			}
			
		}
		else if( *json == '{' || *json == ',' )
		{
			found_key = 0;
			is_key = 1;
		}
		else if( *json == ':' )
			is_key = 0;
		
		json ++;
	}
	
	return NULL;
}
