/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Simple XML (stream) parse tree handling code (Jabber/XMPP, mainly)       *
*                                                                           *
*  Copyright 2006 Wilmer van der Gaast <wilmer@gaast.net>                   *
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

typedef enum
{
	XT_COMPLETE	= 1,	/* </tag> reached */
	XT_SEEN		= 2,	/* Handler called (or not defined) */
} xt_flags;

typedef enum
{
	XT_ABORT,		/* Abort, don't handle the rest anymore */
	XT_HANDLED,		/* Handled this tag properly, go to the next one */
	XT_NEXT			/* Try if there's another matching handler */
} xt_status;

struct xt_attr
{
	char *key, *value;
};

struct xt_node
{
	struct xt_node *parent;
	struct xt_node *children;
	
	char *name;
	struct xt_attr *attr;
	char *text;
	int text_len;
	
	struct xt_node *next;
	xt_flags flags;
};

typedef xt_status (*xt_handler_func) ( struct xt_node *node, gpointer data );

struct xt_handler_entry
{
	char *name, *parent;
	xt_handler_func func;
};

struct xt_parser
{
	GMarkupParseContext *parser;
	struct xt_node *root;
	struct xt_node *cur;
	
	struct xt_handler_entry *handlers;
	gpointer data;
	
	GError *gerr;
};

struct xt_parser *xt_new( gpointer data );
void xt_reset( struct xt_parser *xt );
int xt_feed( struct xt_parser *xt, char *text, int text_len );
int xt_handle( struct xt_parser *xt, struct xt_node *node );
void xt_cleanup( struct xt_parser *xt, struct xt_node *node );
char *xt_to_string( struct xt_node *node );
void xt_print( struct xt_node *node );
void xt_free_node( struct xt_node *node );
void xt_free( struct xt_parser *xt );
struct xt_node *xt_find_node( struct xt_node *node, char *name );
char *xt_find_attr( struct xt_node *node, char *key );

struct xt_node *xt_new_node( char *name, char *text, struct xt_node *children );
void xt_add_child( struct xt_node *parent, struct xt_node *child );
void xt_add_attr( struct xt_node *node, char *key, char *value );
