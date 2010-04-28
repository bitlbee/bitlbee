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

/***************************************************************************\
*                                                                           *
*  Some funtions within this file have been copied from other files within  *
*  BitlBee.                                                                 *
*                                                                           *
****************************************************************************/ 

#include "twitter_http.h"
#include "twitter.h"
#include "bitlbee.h"
#include "url.h"
#include "misc.h"
#include "base64.h"
#include "oauth.h"
#include <ctype.h>
#include <errno.h>


char *twitter_url_append(char *url, char *key, char* value);

/**
 * Do a request.
 * This is actually pretty generic function... Perhaps it should move to the lib/http_client.c
 */
void *twitter_http(char *url_string, http_input_function func, gpointer data, int is_post, char* user, char* pass, char* oauth_token, char** arguments, int arguments_len)
{
	url_t *url = g_new0( url_t, 1 );
	char *tmp;
	char *request;
	void *ret;
	char *userpass = NULL;
	char *userpass_base64;
	char *url_arguments;

	// Fill the url structure.
	if( !url_set( url, url_string ) )
	{
		g_free( url );
		return NULL;
	}

	if( url->proto != PROTO_HTTP && url->proto != PROTO_HTTPS )
	{
		g_free( url );
		return NULL;
	}

	// Concatenate user and pass
	if (user && pass) {
		userpass = g_strdup_printf("%s:%s", user, pass);
		userpass_base64 = base64_encode((unsigned char*)userpass, strlen(userpass));
	} else {
		userpass_base64 = NULL;
	}

	url_arguments = g_malloc(1);
	url_arguments[0] = '\0';

	// Construct the url arguments.
	if (arguments_len != 0)
	{
		int i;
		for (i=0; i<arguments_len; i+=2) 
		{
			tmp = twitter_url_append(url_arguments, arguments[i], arguments[i+1]);
			g_free(url_arguments);
			url_arguments = tmp;
		}
	}

	// Do GET stuff...
	if (!is_post)
	{
		// Find the char-pointer of the end of the string.
		tmp = url->file + strlen(url->file);
		tmp[0] = '?';
		// append the url_arguments to the end of the url->file.
		// TODO GM: Check the length?
		g_stpcpy (tmp+1, url_arguments);
	}


	// Make the request.
	request = g_strdup_printf(  "%s %s HTTP/1.0\r\n"
	                            "Host: %s\r\n"
	                            "User-Agent: BitlBee " BITLBEE_VERSION " " ARCH "/" CPU "\r\n",
	                            is_post ? "POST" : "GET", url->file, url->host );

	// If a pass and user are given we append them to the request.
	if (oauth_token)
	{
		char *full_header;
		
		full_header = oauth_http_header(oauth_token,
		                                is_post ? "POST" : "GET",
		                                url_string, url_arguments);
		
		tmp = g_strdup_printf("%sAuthorization: %s\r\n", request, full_header);
		g_free(request);
		g_free(full_header);
		request = tmp;
	}
	else if (userpass_base64)
	{
		tmp = g_strdup_printf("%sAuthorization: Basic %s\r\n", request, userpass_base64);
		g_free(request);
		request = tmp;
	}

	// Do POST stuff..
	if (is_post)
	{
		// Append the Content-Type and url-encoded arguments.
		tmp = g_strdup_printf("%sContent-Type: application/x-www-form-urlencoded\r\nContent-Length: %zd\r\n\r\n%s", 
								request, strlen(url_arguments), url_arguments);
		g_free(request);
		request = tmp;
	} else {
		// Append an extra \r\n to end the request...
		tmp = g_strdup_printf("%s\r\n", request);
		g_free(request);
		request = tmp;
	}

	ret = http_dorequest( url->host, url->port,	url->proto == PROTO_HTTPS, request, func, data );

	g_free( url );
	g_free( userpass );
	g_free( userpass_base64 );
	g_free( url_arguments );
	g_free( request );
	return ret;
}

char *twitter_url_append(char *url, char *key, char* value)
{
	char *key_encoded = g_strndup(key, 3 * strlen(key));
	http_encode(key_encoded);
	char *value_encoded = g_strndup(value, 3 * strlen(value));
	http_encode(value_encoded);

	char *retval;
	if (strlen(url) != 0)
		retval = g_strdup_printf("%s&%s=%s", url, key_encoded, value_encoded);
	else
		retval = g_strdup_printf("%s=%s", key_encoded, value_encoded);

	g_free(key_encoded);
	g_free(value_encoded);

	return retval;
}
