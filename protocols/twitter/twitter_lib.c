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

#include "twitter_http.h"
#include "twitter.h"
#include "bitlbee.h"
#include "url.h"
#include "misc.h"
#include "base64.h"
#include "xmltree.h"
#include "twitter_lib.h"
#include <ctype.h>
#include <errno.h>

#define TXL_STATUS 1
#define TXL_ID 1

struct twitter_xml_list {
	int next_cursor;
	GSList *list;
	gpointer data;
};

struct twitter_xml_user {
	char *name;
	char *screen_name;
};

struct twitter_xml_status {
	char *created_at;
	char *text;
	struct twitter_xml_user *user;
	guint64 id;
};

void txl_free(struct twitter_xml_list *txl, int type);
void txs_free(struct twitter_xml_status *txs);
void txu_free(struct twitter_xml_user *txu);

static void twitter_http_get_friends_ids(struct http_request *req);

/**
 * Get the friends ids.
 */
void twitter_get_friends_ids(struct im_connection *ic, int next_cursor)
{
	struct twitter_data *td = ic->proto_data;

	// Primitive, but hey! It works...	
	char* args[2];
	args[0] = "cursor";
	args[1] = g_strdup_printf ("%d", next_cursor);
	twitter_http(TWITTER_FRIENDS_IDS_URL, twitter_http_get_friends_ids, ic, 0, td->user, td->pass, args, 2);

	g_free(args[1]);
}

/**
 * Function to help fill a list.
 */
static xt_status twitter_xt_next_cursor( struct xt_node *node, struct twitter_xml_list *txl )
{
	// Do something with the cursor.
	txl->next_cursor = atoi(node->text);

	return XT_HANDLED;
}

/**
 * Fill a list of ids.
 */
static xt_status twitter_xt_get_friends_id_list( struct xt_node *node, struct twitter_xml_list *txl )
{
	struct xt_node *child;

	// The root <statuses> node should hold the list of statuses <status>
	// Walk over the nodes children.
	for( child = node->children ; child ; child = child->next )
	{
		if ( g_strcasecmp( "id", child->name ) == 0)
		{
			// Add the item to the list.
			txl->list = g_slist_append (txl->list, g_memdup( node->text, node->text_len + 1 ));
		}
		else if ( g_strcasecmp( "next_cursor", child->name ) == 0)
		{
			twitter_xt_next_cursor(child, txl);
		}
	}

	return XT_HANDLED;
}

/**
 * Callback for getting the friends ids.
 */
static void twitter_http_get_friends_ids(struct http_request *req)
{
	struct im_connection *ic;
	struct xt_parser *parser;
	struct twitter_xml_list *txl;

	ic = req->data;

	// Check if the HTTP request went well.
	if (req->status_code != 200) {
		// It didn't go well, output the error and return.
		imcb_error(ic, "Could not retrieve friends. HTTP STATUS: %d", req->status_code);
		return;
	}

	txl = g_new0(struct twitter_xml_list, 1);
	txl->list = NULL;

	// Parse the data.
	parser = xt_new( NULL, txl );
	xt_feed( parser, req->reply_body, req->body_size );
	twitter_xt_get_friends_id_list(parser->root, txl);
	xt_free( parser );

	if (txl->next_cursor)
		twitter_get_friends_ids(ic, txl->next_cursor);

	txl_free(txl, TXL_ID);
	g_free(txl);
}

/**
 * Function to fill a twitter_xml_user struct.
 * It sets:
 *  - the name and
 *  - the screen_name.
 */
static xt_status twitter_xt_get_user( struct xt_node *node, struct twitter_xml_user *txu )
{
	struct xt_node *child;

	// Walk over the nodes children.
	for( child = node->children ; child ; child = child->next )
	{
		if ( g_strcasecmp( "name", child->name ) == 0)
		{
			txu->name = g_memdup( child->text, child->text_len + 1 );
		}
		else if (g_strcasecmp( "screen_name", child->name ) == 0)
		{
			txu->screen_name = g_memdup( child->text, child->text_len + 1 );
		}
	}
	return XT_HANDLED;
}

/**
 * Function to fill a twitter_xml_status struct.
 * It sets:
 *  - the status text and
 *  - the created_at timestamp and
 *  - the status id and
 *  - the user in a twitter_xml_user struct.
 */
static xt_status twitter_xt_get_status( struct xt_node *node, struct twitter_xml_status *txs )
{
	struct xt_node *child;

	// Walk over the nodes children.
	for( child = node->children ; child ; child = child->next )
	{
		if ( g_strcasecmp( "text", child->name ) == 0)
		{
			txs->text = g_memdup( child->text, child->text_len + 1 );
		}
		else if (g_strcasecmp( "created_at", child->name ) == 0)
		{
			txs->created_at = g_memdup( child->text, child->text_len + 1 );
		}
		else if (g_strcasecmp( "user", child->name ) == 0)
		{
			txs->user = g_new0(struct twitter_xml_user, 1);
			twitter_xt_get_user( child, txs->user );
		}
		else if (g_strcasecmp( "id", child->name ) == 0)
		{
			txs->id = g_ascii_strtoull (child->text, NULL, 10);
		}
	}
	return XT_HANDLED;
}

/**
 * Function to fill a twitter_xml_list struct.
 * It sets:
 *  - all <status>es within the <status> element and
 *  - the next_cursor.
 */
static xt_status twitter_xt_get_status_list( struct xt_node *node, struct twitter_xml_list *txl )
{
	struct twitter_xml_status *txs;
	struct xt_node *child;

	// The root <statuses> node should hold the list of statuses <status>
	// Walk over the nodes children.
	for( child = node->children ; child ; child = child->next )
	{
		if ( g_strcasecmp( "status", child->name ) == 0)
		{
			txs = g_new0(struct twitter_xml_status, 1);
			twitter_xt_get_status(child, txs);
			// Put the item in the front of the list.
			txl->list = g_slist_prepend (txl->list, txs);
		}
		else if ( g_strcasecmp( "next_cursor", child->name ) == 0)
		{
			twitter_xt_next_cursor(child, txl);
		}
	}

	return XT_HANDLED;
}

static void twitter_http_get_home_timeline(struct http_request *req);

/**
 * Get the timeline.
 */
void twitter_get_home_timeline(struct im_connection *ic, int next_cursor)
{
	struct twitter_data *td = ic->proto_data;

	char* args[4];
	args[0] = "cursor";
	args[1] = g_strdup_printf ("%d", next_cursor);
	if (td->home_timeline_id) {
		args[2] = "since_id";
		args[3] = g_strdup_printf ("%llu", td->home_timeline_id);
	}

	twitter_http(TWITTER_HOME_TIMELINE_URL, twitter_http_get_home_timeline, ic, 0, td->user, td->pass, args, td->home_timeline_id ? 4 : 2);

	g_free(args[1]);
	if (td->home_timeline_id) {
		g_free(args[3]);
	}
}

/**
 * Callback for getting the home timeline.
 */
static void twitter_http_get_home_timeline(struct http_request *req)
{
	struct im_connection *ic = req->data;;
	struct xt_parser *parser;
	struct twitter_xml_list *txl;
	struct twitter_data *td = ic->proto_data;
	struct groupchat *gc;

	// Check if the HTTP request went well.
	if (req->status_code != 200) {
		// It didn't go well, output the error and return.
		imcb_error(ic, "Could not retrieve home/timeline. HTTP STATUS: %d", req->status_code);
		return;
	}

	txl = g_new0(struct twitter_xml_list, 1);
	txl->list = NULL;
	
	// Parse the data.
	parser = xt_new( NULL, txl );
	xt_feed( parser, req->reply_body, req->body_size );
	// The root <statuses> node should hold the list of statuses <status>
	twitter_xt_get_status_list(parser->root, txl);
	xt_free( parser );
	
	GSList *l;
	struct twitter_xml_status *status;

	// Create a new groupchat if it does not exsist.
	if (!td->home_timeline_gc)
	{
		td->home_timeline_gc = gc = imcb_chat_new( ic, "home/timeline" );
		// Add the current user to the chat...
		imcb_chat_add_buddy( gc, ic->acc->user );
	}
	else
	{
		gc = td->home_timeline_gc;
	}

	for ( l = txl->list; l ; l = g_slist_next(l) )
	{
		status = l->data;
		// TODO Put the next part in a new function....

		// Ugly hack, to show current user in chat...
		if ( g_strcasecmp(status->user->screen_name, ic->acc->user) == 0)
		{
			char *tmp = g_strdup_printf ("_%s_", status->user->screen_name);
			g_free(status->user->screen_name);
			status->user->screen_name = tmp;
		}

		// Check if the buddy is allready in the buddy list.
		if (!user_findhandle( ic, status->user->screen_name ))
		{
			// The buddy is not in the list, add the buddy...
			imcb_add_buddy( ic, status->user->screen_name, NULL );
			imcb_buddy_status( ic, status->user->screen_name, OPT_LOGGED_IN, NULL, NULL );
		}

		// Say it!
		imcb_chat_msg (gc, status->user->screen_name, status->text, 0, 0 );
		// Update the home_timeline_id to hold the highest id, so that by the next request
		// we won't pick up the updates allready in the list.
		td->home_timeline_id = td->home_timeline_id < status->id ? status->id : td->home_timeline_id;
	}

	// Free the structure.	
	txl_free(txl, TXL_STATUS);
	g_free(txl);
}

/**
 * Free a twitter_xml_list struct.
 * type is the type of list the struct holds.
 */
void txl_free(struct twitter_xml_list *txl, int type)
{
	GSList *l;
	for ( l = txl->list; l ; l = g_slist_next(l) )
		if (type == TXL_STATUS)
			txs_free((struct twitter_xml_status *)l->data);
		else if (type == TXL_ID)
			g_free(l->data);
	g_slist_free(txl->list);
}

/**
 * Frees a twitter_xml_status struct.
 */
void txs_free(struct twitter_xml_status *txs)
{
	g_free(txs->created_at);
	g_free(txs->text);
	txu_free(txs->user);
}

/**
 * Frees a twitter_xml_user struct.
 */
void txu_free(struct twitter_xml_user *txu)
{
	g_free(txu->name);
	g_free(txu->screen_name);
}

/**
 * Callback after sending a new update to twitter.
 */
static void twitter_http_post_status(struct http_request *req)
{
	struct im_connection *ic = req->data;

	// Check if the HTTP request went well.
	if (req->status_code != 200) {
		// It didn't go well, output the error and return.
		imcb_error(ic, "Could not post tweed... HTTP STATUS: %d", req->status_code);
		imcb_error(ic, req->reply_body);
		return;
	}
}

/**
 * Function to POST a new status to twitter.
 */ 
void twitter_post_status(struct im_connection *ic, char* msg)
{
	struct twitter_data *td = ic->proto_data;

	char* args[2];
	args[0] = "status";
	args[1] = msg;
	twitter_http(TWITTER_STATUS_UPDATE_URL, twitter_http_post_status, ic, 1, td->user, td->pass, args, 2);
	g_free(args[1]);
}


