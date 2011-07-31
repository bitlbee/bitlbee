/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Simple OAuth2 client (consumer) implementation.                          *
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

/* Implementation mostly based on my experience with writing the previous OAuth
   module, and from http://code.google.com/apis/accounts/docs/OAuth2.html . */

typedef void (*oauth2_token_callback)( gpointer data, const char *atoken, const char *rtoken );

struct oauth2_service
{
	char *auth_url;
	char *token_url;
	char *redirect_url;
	char *consumer_key;
	char *consumer_secret;
};

/* Currently suitable for authenticating to Google Talk only, and only for
   accounts that have 2-factor authorization enabled. */
extern struct oauth2_service oauth2_service_google;

extern struct oauth2_service oauth2_service_facebook;

#define OAUTH2_AUTH_CODE "authorization_code"
#define OAUTH2_AUTH_REFRESH "refresh_token"

/* Generate a URL the user should open in his/her browser to get an
   authorization code. */
char *oauth2_url( const struct oauth2_service *sp, const char *scope );

/* Exchanges an auth code or refresh token for an access token.
   auth_type is one of the two OAUTH2_AUTH_.. constants above. */
int oauth2_access_token( const struct oauth2_service *sp,
                         const char *auth_type, const char *auth,
                         oauth2_token_callback func, gpointer data );
