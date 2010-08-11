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
#include "sha1.h"
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
	char *soap_action = NULL;
	url_t url;
	
	soap_req->build_request( soap_req );
	
	if( soap_req->action )
		soap_action = g_strdup_printf( "SOAPAction: \"%s\"\r\n", soap_req->action );
	
	url_set( &url, soap_req->url );
	http_req = g_strdup_printf( SOAP_HTTP_REQUEST, url.file, url.host,
		soap_action ? soap_action : "",
		strlen( soap_req->payload ), soap_req->payload );
	
	soap_req->http_req = http_dorequest( url.host, url.port, url.proto == PROTO_HTTPS,
		http_req, msn_soap_handle_response, soap_req );
	
	g_free( http_req );
	g_free( soap_action );
	
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
	
	g_free( soap_req->url );
	g_free( soap_req->action );
	g_free( soap_req->payload );
	soap_req->url = soap_req->action = soap_req->payload = NULL;
	
	if( st == MSN_SOAP_RETRY && --soap_req->ttl )
		msn_soap_send_request( soap_req );
	else
	{
		soap_req->free_data( soap_req );
		g_free( soap_req );
	}
}


/* passport_sso: Authentication MSNP15+ */

struct msn_soap_passport_sso_data
{
	char *policy;
	char *nonce;
	char *secret;
};

static int msn_soap_passport_sso_build_request( struct msn_soap_req_data *soap_req )
{
	struct msn_soap_passport_sso_data *sd = soap_req->data;
	struct im_connection *ic = soap_req->ic;
	
	soap_req->url = g_strdup( SOAP_PASSPORT_SSO_URL );
	soap_req->payload = g_markup_printf_escaped( SOAP_PASSPORT_SSO_PAYLOAD,
		ic->acc->user, ic->acc->pass, sd->policy );
	
	return MSN_SOAP_OK;
}

static xt_status msn_soap_passport_sso_token( struct xt_node *node, gpointer data )
{
	struct msn_soap_req_data *soap_req = data;
	struct msn_soap_passport_sso_data *sd = soap_req->data;
	struct msn_data *md = soap_req->ic->proto_data;
	struct xt_node *p;
	char *id;
	
	if( ( id = xt_find_attr( node, "Id" ) ) == NULL )
		return XT_HANDLED;
	id += strlen( id ) - 1;
	if( *id == '1' &&
	    ( p = node->parent ) && ( p = p->parent ) &&
	    ( p = xt_find_node( p->children, "wst:RequestedProofToken" ) ) &&
	    ( p = xt_find_node( p->children, "wst:BinarySecret" ) ) &&
	    p->text )
	    	sd->secret = g_strdup( p->text );
	
	if( *id == '1' )
		md->tokens[0] = g_strdup( node->text );
	else if( *id == '2' )
		md->tokens[1] = g_strdup( node->text );
	
	return XT_HANDLED;
}

static const struct xt_handler_entry msn_soap_passport_sso_parser[] = {
	{ "wsse:BinarySecurityToken", "wst:RequestedSecurityToken", msn_soap_passport_sso_token },
	{ NULL, NULL, NULL }
};

static char *msn_key_fuckery( char *key, int key_len, char *type )
{
	unsigned char hash1[20+strlen(type)+1];
	unsigned char hash2[20];
	char *ret;
	
	sha1_hmac( key, key_len, type, 0, hash1 );
	strcpy( (char*) hash1 + 20, type );
	sha1_hmac( key, key_len, (char*) hash1, sizeof( hash1 ) - 1, hash2 );
	
	/* This is okay as hash1 is read completely before it's overwritten. */
	sha1_hmac( key, key_len, (char*) hash1, 20, hash1 );
	sha1_hmac( key, key_len, (char*) hash1, sizeof( hash1 ) - 1, hash1 );
	
	ret = g_malloc( 24 );
	memcpy( ret, hash2, 20 );
	memcpy( ret + 20, hash1, 4 );
	return ret;
}

static int msn_soap_passport_sso_handle_response( struct msn_soap_req_data *soap_req )
{
	struct msn_soap_passport_sso_data *sd = soap_req->data;
	struct im_connection *ic = soap_req->ic;
	char *key1, *key2, *key3, *blurb64;
	int key1_len;
	unsigned char *padnonce, *des3res;
	struct
	{
		unsigned int uStructHeaderSize; // 28. Does not count data
		unsigned int uCryptMode; // CRYPT_MODE_CBC (1)
		unsigned int uCipherType; // TripleDES (0x6603)
		unsigned int uHashType; // SHA1 (0x8004)
		unsigned int uIVLen;    // 8
		unsigned int uHashLen;  // 20
		unsigned int uCipherLen; // 72
		unsigned char iv[8];
		unsigned char hash[20];
		unsigned char cipherbytes[72];
	} blurb = {
		GUINT32_TO_LE( 28 ),
		GUINT32_TO_LE( 1 ),
		GUINT32_TO_LE( 0x6603 ),
		GUINT32_TO_LE( 0x8004 ),
		GUINT32_TO_LE( 8 ),
		GUINT32_TO_LE( 20 ),
		GUINT32_TO_LE( 72 ),
	};

	key1_len = base64_decode( sd->secret, (unsigned char**) &key1 );
	
	key2 = msn_key_fuckery( key1, key1_len, "WS-SecureConversationSESSION KEY HASH" );
	key3 = msn_key_fuckery( key1, key1_len, "WS-SecureConversationSESSION KEY ENCRYPTION" );
	
	sha1_hmac( key2, 24, sd->nonce, 0, blurb.hash );
	padnonce = g_malloc( strlen( sd->nonce ) + 8 );
	strcpy( (char*) padnonce, sd->nonce );
	memset( padnonce + strlen( sd->nonce ), 8, 8 );
	
	random_bytes( blurb.iv, 8 );
	
	ssl_des3_encrypt( (unsigned char*) key3, 24, padnonce, strlen( sd->nonce ) + 8, blurb.iv, &des3res );
	memcpy( blurb.cipherbytes, des3res, 72 );
	
	blurb64 = base64_encode( (unsigned char*) &blurb, sizeof( blurb ) );
	msn_auth_got_passport_token( ic, blurb64 );
	
	g_free( padnonce );
	g_free( blurb64 );
	g_free( des3res );
	g_free( key1 );
	g_free( key2 );
	g_free( key3 );
	
	return MSN_SOAP_OK;
}

static int msn_soap_passport_sso_free_data( struct msn_soap_req_data *soap_req )
{
	struct msn_soap_passport_sso_data *sd = soap_req->data;
	
	g_free( sd->policy );
	g_free( sd->nonce );
	g_free( sd->secret );
	
	return MSN_SOAP_OK;
}

int msn_soap_passport_sso_request( struct im_connection *ic, const char *policy, const char *nonce )
{
	struct msn_soap_passport_sso_data *sd = g_new0( struct msn_soap_passport_sso_data, 1 );
	
	sd->policy = g_strdup( policy );
	sd->nonce = g_strdup( nonce );
	
	return msn_soap_start( ic, sd, msn_soap_passport_sso_build_request,
	                               msn_soap_passport_sso_parser,
	                               msn_soap_passport_sso_handle_response,
	                               msn_soap_passport_sso_free_data );
}


/* oim_send: Sending offline messages */

struct msn_soap_oim_send_data
{
	char *to;
	char *msg;
	int number;
	int need_retry;
};

static int msn_soap_oim_build_request( struct msn_soap_req_data *soap_req )
{
	struct msn_soap_oim_send_data *oim = soap_req->data;
	struct im_connection *ic = soap_req->ic;
	struct msn_data *md = ic->proto_data;
	char *display_name_b64;
	
	display_name_b64 = tobase64( ic->displayname );
	
	soap_req->url = g_strdup( SOAP_OIM_SEND_URL );
	soap_req->action = g_strdup( SOAP_OIM_SEND_ACTION );
	soap_req->payload = g_markup_printf_escaped( SOAP_OIM_SEND_PAYLOAD,
		ic->acc->user, display_name_b64, oim->to, "bla", //md->passport_token,
		MSNP11_PROD_ID, md->lock_key ? md->lock_key : "",
		oim->number, oim->number, oim->msg );
	
	g_free( display_name_b64 );
	
	return MSN_SOAP_OK;
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
	
	if( soap_req->http_req->status_code == 500 && oim->need_retry && soap_req->ttl > 0 )
	{
		oim->need_retry = 0;
		return MSN_SOAP_RETRY;
	}
	else if( soap_req->http_req->status_code == 200 )
	{
		imcb_log( soap_req->ic, "Offline message successfully delivered to %s", oim->to );
		return MSN_SOAP_OK;
	}
	else
	{
		imcb_log( soap_req->ic, "Failed to deliver offline message to %s:\n%s", oim->to, oim->msg );
		return MSN_SOAP_ABORT;
	}
}

static int msn_soap_oim_free_data( struct msn_soap_req_data *soap_req )
{
	struct msn_soap_oim_send_data *oim = soap_req->data;
	
	g_free( oim->to );
	g_free( oim->msg );
	g_free( oim );
	
	return MSN_SOAP_OK;
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

int msn_soap_oim_send_queue( struct im_connection *ic, GSList **msgq )
{
	GSList *l;
	char *n = NULL;
	
	for( l = *msgq; l; l = l->next )
	{
		struct msn_message *m = l->data;
		
		if( n == NULL )
			n = m->who;
		if( strcmp( n, m->who ) == 0 )
			msn_soap_oim_send( ic, m->who, m->text );
	}
	
	while( *msgq != NULL )
	{
		struct msn_message *m = (*msgq)->data;
		
		g_free( m->who );
		g_free( m->text );
		g_free( m );
		
		*msgq = g_slist_remove( *msgq, m );
	}
	
	return 1;
}


/* memlist: Fetching the membership list (NOT address book) */

static int msn_soap_memlist_build_request( struct msn_soap_req_data *soap_req )
{
	struct msn_data *md = soap_req->ic->proto_data;
	
	soap_req->url = g_strdup( SOAP_MEMLIST_URL );
	soap_req->action = g_strdup( SOAP_MEMLIST_ACTION );
	soap_req->payload = g_markup_printf_escaped( SOAP_MEMLIST_PAYLOAD, md->tokens[1] );
	
	return 1;
}

static xt_status msn_soap_memlist_member( struct xt_node *node, gpointer data )
{
	bee_user_t *bu;
	struct msn_buddy_data *bd;
	struct xt_node *p;
	char *role = NULL, *handle = NULL;
	struct msn_soap_req_data *soap_req = data;
	struct im_connection *ic = soap_req->ic;
	
	if( ( p = node->parent ) && ( p = p->parent ) &&
	    ( p = xt_find_node( p->children, "MemberRole" ) ) )
		role = p->text;
	
	if( ( p = xt_find_node( node->children, "PassportName" ) ) )
		handle = p->text;
	
	if( !role || !handle || 
	    !( ( bu = bee_user_by_handle( ic->bee, ic, handle ) ) ||
	       ( bu = bee_user_new( ic->bee, ic, handle, 0 ) ) ) )
		return XT_HANDLED;
	
	bd = bu->data;
	if( strcmp( role, "Allow" ) == 0 )
		bd->flags |= MSN_BUDDY_AL;
	else if( strcmp( role, "Block" ) == 0 )
		bd->flags |= MSN_BUDDY_BL;
	else if( strcmp( role, "Reverse" ) == 0 )
		bd->flags |= MSN_BUDDY_RL;
	else if( strcmp( role, "Pending" ) == 0 )
		bd->flags |= MSN_BUDDY_PL;
	
	return XT_HANDLED;
}

static const struct xt_handler_entry msn_soap_memlist_parser[] = {
	{ "Member", "Members", msn_soap_memlist_member },
	{ NULL,               NULL,     NULL                        }
};

static int msn_soap_memlist_handle_response( struct msn_soap_req_data *soap_req )
{
	msn_soap_addressbook_request( soap_req->ic );
	
	return MSN_SOAP_OK;
}

static int msn_soap_memlist_free_data( struct msn_soap_req_data *soap_req )
{
	return 0;
}

int msn_soap_memlist_request( struct im_connection *ic )
{
	return msn_soap_start( ic, NULL, msn_soap_memlist_build_request,
	                                 msn_soap_memlist_parser,
	                                 msn_soap_memlist_handle_response,
	                                 msn_soap_memlist_free_data );
}


/* addressbook: Fetching the membership list (NOT address book) */

static int msn_soap_addressbook_build_request( struct msn_soap_req_data *soap_req )
{
	struct msn_data *md = soap_req->ic->proto_data;
	
	soap_req->url = g_strdup( SOAP_ADDRESSBOOK_URL );
	soap_req->action = g_strdup( SOAP_ADDRESSBOOK_ACTION );
	soap_req->payload = g_markup_printf_escaped( SOAP_ADDRESSBOOK_PAYLOAD, md->tokens[1] );
	
	return 1;
}

static xt_status msn_soap_addressbook_group( struct xt_node *node, gpointer data )
{
	struct xt_node *p;
	char *id = NULL, *name = NULL;
	struct msn_soap_req_data *soap_req = data;
	struct im_connection *ic = soap_req->ic;
	
	if( ( p = node->parent ) &&
	    ( p = xt_find_node( p->children, "groupId" ) ) )
		id = p->text;
	
	if( ( p = xt_find_node( node->children, "name" ) ) )
		name = p->text;
	
	printf( "%s %s\n", id, name );
	
	return XT_HANDLED;
}

static xt_status msn_soap_addressbook_contact( struct xt_node *node, gpointer data )
{
	bee_user_t *bu;
	struct msn_buddy_data *bd;
	struct xt_node *p;
	char *id = NULL, *type = NULL, *handle = NULL, *display_name = NULL;
	struct msn_soap_req_data *soap_req = data;
	struct im_connection *ic = soap_req->ic;
	
	if( ( p = node->parent ) &&
	    ( p = xt_find_node( p->children, "contactId" ) ) )
		id = p->text;
	if( ( p = xt_find_node( node->children, "contactType" ) ) )
		type = p->text;
	if( ( p = xt_find_node( node->children, "passportName" ) ) )
		handle = p->text;
	if( ( p = xt_find_node( node->children, "displayName" ) ) )
		display_name = p->text;
	
	if( type && g_strcasecmp( type, "me" ) == 0 )
	{
		set_t *set = set_find( &ic->acc->set, "display_name" );
		g_free( set->value );
		set->value = g_strdup( display_name );
		
		return XT_HANDLED;
	}
	
	if( !( bu = bee_user_by_handle( ic->bee, ic, handle ) ) &&
	    !( bu = bee_user_new( ic->bee, ic, handle, 0 ) ) )
		return XT_HANDLED;
	
	bd = bu->data;
	bd->flags |= MSN_BUDDY_FL;
	g_free( bd->cid );
	bd->cid = g_strdup( id );
	
	imcb_rename_buddy( ic, handle, display_name );
	
	printf( "%s %s %s %s\n", id, type, handle, display_name );
	
	return XT_HANDLED;
}

static const struct xt_handler_entry msn_soap_addressbook_parser[] = {
	{ "contactInfo", "Contact", msn_soap_addressbook_contact },
	{ "groupInfo", "Group", msn_soap_addressbook_group },
	{ NULL,               NULL,     NULL                        }
};

static int msn_soap_addressbook_handle_response( struct msn_soap_req_data *soap_req )
{
	return MSN_SOAP_OK;
}

static int msn_soap_addressbook_free_data( struct msn_soap_req_data *soap_req )
{
	return 0;
}

int msn_soap_addressbook_request( struct im_connection *ic )
{
	return msn_soap_start( ic, NULL, msn_soap_addressbook_build_request,
	                                 msn_soap_addressbook_parser,
	                                 msn_soap_addressbook_handle_response,
	                                 msn_soap_addressbook_free_data );
}
