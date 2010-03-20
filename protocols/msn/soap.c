/** soap.c
 *
 * SOAP-related functions. Some manager at Microsoft apparently thought
 * MSNP wasn't XMLy enough so someone stepped up and changed that. This
 * is the result.
 *
 * Copyright (C) 2010 Wilmer van der Gaast <wilmer@gaast.net>
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA          
 *
 */

#include "http_client.h"
#include "soap.h"
#include "msn.h"
#include "bitlbee.h"
#include "url.h"
#include "misc.h"
#include "base64.h"
#include "xmltree.h"
#include <ctype.h>
#include <errno.h>

typedef enum
{
	MSN_SOAP_OK,
	MSN_SOAP_RETRY,
	MSN_SOAP_ABORT,
} msn_soap_result_t;

struct msn_soap_req_data;

typedef int (*msn_soap_func) ( struct msn_soap_req_data * );

struct msn_soap_req_data
{
	void *data;
	struct im_connection *ic;
	int ttl;
	
	char *url, *action, *payload;
	struct http_request *http_req;
	
	const struct xt_handler_entry *xml_parser;
	msn_soap_func build_request, handle_response, free_data;
};

struct msn_soap_oim_send_data
{
	char *to;
	char *msg;
	int number;
	int need_retry;
};

static int msn_soap_send_request( struct msn_soap_req_data *req );

static int msn_soap_start( struct im_connection *ic,
                    void *data,
                    msn_soap_func build_request,
                    const struct xt_handler_entry *xml_parser,
                    msn_soap_func handle_response,
                    msn_soap_func free_data )
{
	struct msn_soap_req_data *req = g_new0( struct msn_soap_req_data, 1 );
	
	req->ic = ic;
	req->data = data;
	req->xml_parser = xml_parser;
	req->build_request = build_request;
	req->handle_response = handle_response;
	req->free_data = free_data;
	req->ttl = 3;
	
	return msn_soap_send_request( req );
}

static void msn_soap_handle_response( struct http_request *http_req );

static int msn_soap_send_request( struct msn_soap_req_data *soap_req )
{
	char *http_req;
	url_t url;
	
	soap_req->build_request( soap_req );
	
	url_set( &url, soap_req->url );
	http_req = g_strdup_printf( SOAP_HTTP_REQUEST, url.file, url.host,
		soap_req->action, strlen( soap_req->payload ), soap_req->payload );
	
	soap_req->http_req = http_dorequest( url.host, url.port, url.proto == PROTO_HTTPS,
		http_req, msn_soap_handle_response, soap_req );
	
	return soap_req->http_req != NULL;
}

static void msn_soap_handle_response( struct http_request *http_req )
{
	struct msn_soap_req_data *soap_req = http_req->data;
	int st;
	
	if( http_req->body_size > 0 )
	{
		struct xt_parser *parser;
		
		parser = xt_new( soap_req->xml_parser, soap_req );
		xt_feed( parser, http_req->reply_body, http_req->body_size );
		xt_handle( parser, NULL, -1 );
		xt_free( parser );
	}
	
	st = soap_req->handle_response( soap_req );
	
	if( st == MSN_SOAP_RETRY && --soap_req->ttl )
		msn_soap_send_request( soap_req );
	else
	{
		soap_req->free_data( soap_req );
		g_free( soap_req->url );
		g_free( soap_req->action );
		g_free( soap_req->payload );
		g_free( soap_req );
	}
}

static int msn_soap_oim_build_request( struct msn_soap_req_data *soap_req )
{
	struct msn_soap_oim_send_data *oim = soap_req->data;
	struct im_connection *ic = soap_req->ic;
	struct msn_data *md = ic->proto_data;
	char *display_name_b64;
	
	display_name_b64 = tobase64( ic->displayname );
	
	soap_req->url = g_strdup( SOAP_OIM_SEND_URL );
	soap_req->action = g_strdup( SOAP_OIM_ACTION_URL );
	soap_req->payload = g_markup_printf_escaped( SOAP_OIM_SEND_PAYLOAD,
		ic->acc->user, display_name_b64, oim->to, md->passport_token,
		MSNP11_PROD_ID, md->lock_key ? : "", oim->number, oim->number, oim->msg );
	
	g_free( display_name_b64 );
	
	return 1;
}

static xt_status msn_soap_oim_send_challenge( struct xt_node *node, gpointer data )
{
	struct msn_soap_req_data *soap_req = data;
	struct msn_soap_oim_send_data *oim = soap_req->data;
	struct im_connection *ic = soap_req->ic;
	struct msn_data *md = ic->proto_data;
	
	g_free( md->lock_key );
	md->lock_key = msn_p11_challenge( node->text );
	
	oim->need_retry = 1;
	
	return XT_HANDLED;
}

static const struct xt_handler_entry msn_soap_oim_send_parser[] = {
	{ "LockKeyChallenge", "detail", msn_soap_oim_send_challenge },
	{ NULL,               NULL,     NULL                        }
};

static int msn_soap_oim_handle_response( struct msn_soap_req_data *soap_req )
{
	struct msn_soap_oim_send_data *oim = soap_req->data;
	
	if( soap_req->http_req->status_code == 500 && oim->need_retry )
	{
		oim->need_retry = 0;
		return MSN_SOAP_RETRY;
	}
	else if( soap_req->http_req->status_code == 200 )
		return MSN_SOAP_OK;
	else
		return MSN_SOAP_ABORT;
}

static int msn_soap_oim_free_data( struct msn_soap_req_data *soap_req )
{
	struct msn_soap_oim_send_data *oim = soap_req->data;
	
	g_free( oim->to );
	g_free( oim->msg );
	g_free( oim );
	
	return 0;
}

int msn_soap_oim_send( struct im_connection *ic, const char *to, const char *msg )
{
	struct msn_soap_oim_send_data *data;
	
	data = g_new0( struct msn_soap_oim_send_data, 1 );
	data->to = g_strdup( to );
	data->msg = tobase64( msg );
	data->number = 1;
	
	return msn_soap_start( ic, data, msn_soap_oim_build_request,
	                                 msn_soap_oim_send_parser,
	                                 msn_soap_oim_handle_response,
	                                 msn_soap_oim_free_data );
}
