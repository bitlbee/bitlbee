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

#include <glib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <stdio.h>

#include "xmltree.h"

static void xt_start_element( GMarkupParseContext *ctx, const gchar *element_name, const gchar **attr_names, const gchar **attr_values, gpointer data, GError **error )
{
	struct xt_parser *xt = data;
	struct xt_node *node = g_new0( struct xt_node, 1 ), *nt;
	int i;
	
	node->parent = xt->cur;
	node->name = g_strdup( element_name );
	
	/* First count the number of attributes */
	for( i = 0; attr_names[i]; i ++ );
	
	/* Then allocate a NULL-terminated array. */
	node->attr = g_new0( struct xt_attr, i + 1 );
	
	/* And fill it, saving one variable by starting at the end. */
	for( i --; i >= 0; i -- )
	{
		node->attr[i].key = g_strdup( attr_names[i] );
		node->attr[i].value = g_strdup( attr_values[i] );
	}
	
	/* Add it to the linked list of children nodes, if we have a current
	   node yet. */
	if( xt->cur )
	{
		if( xt->cur->children )
		{
			for( nt = xt->cur->children; nt->next; nt = nt->next );
			nt->next = node;
		}
		else
		{
			xt->cur->children = node;
		}
	}
	else if( xt->root )
	{
		/* ERROR situation: A second root-element??? */
	}
	
	/* Now this node will be the new current node. */
	xt->cur = node;
	/* And maybe this is the root? */
	if( xt->root == NULL )
		xt->root = node;
}

static void xt_text( GMarkupParseContext *ctx, const gchar *text, gsize text_len, gpointer data, GError **error )
{
	struct xt_parser *xt = data;
	struct xt_node *node = xt->cur;
	
	if( node == NULL )
		return;
	
	/* FIXME: Does g_renew also OFFICIALLY accept NULL arguments? */
	node->text = g_renew( char, node->text, node->text_len + text_len + 1 );
	memcpy( node->text + node->text_len, text, text_len );
	node->text_len += text_len;
	/* Zero termination is always nice to have. */
	node->text[node->text_len] = 0;
}

static void xt_end_element( GMarkupParseContext *ctx, const gchar *element_name, gpointer data, GError **error )
{
	struct xt_parser *xt = data;
	
	xt->cur->flags |= XT_COMPLETE;
	xt->cur = xt->cur->parent;
}

GMarkupParser xt_parser_funcs =
{
	xt_start_element,
	xt_end_element,
	xt_text,
	NULL,
	NULL
};

struct xt_parser *xt_new( gpointer data )
{
	struct xt_parser *xt = g_new0( struct xt_parser, 1 );
	
	xt->data = data;
	xt_reset( xt );
	
	return xt;
}

/* Reset the parser, flush everything we have so far. For example, we need
   this for XMPP when doing TLS/SASL to restart the stream. */
void xt_reset( struct xt_parser *xt )
{
	if( xt->parser )
		g_markup_parse_context_free( xt->parser );
	
	xt->parser = g_markup_parse_context_new( &xt_parser_funcs, 0, xt, NULL );
	
	if( xt->root )
	{
		xt_free_node( xt->root );
		xt->root = NULL;
		xt->cur = NULL;
	}
}

/* Feed the parser, don't execute any handler. Returns -1 on errors, 0 on
   end-of-stream and 1 otherwise. */
int xt_feed( struct xt_parser *xt, char *text, int text_len )
{
	if( !g_markup_parse_context_parse( xt->parser, text, text_len, &xt->gerr ) )
	{
		return -1;
	}
	
	return !( xt->root && xt->root->flags & XT_COMPLETE );
}

/* Find completed nodes and see if a handler has to be called. Passing
   a node isn't necessary if you want to start at the root, just pass
   NULL. This second argument is needed for recursive calls. FIXME: Retval? */
int xt_handle( struct xt_parser *xt, struct xt_node *node )
{
	struct xt_node *c;
	xt_status st;
	int i;
	
	/* Let's just hope xt->root isn't NULL! */
	if( node == NULL )
		return xt_handle( xt, xt->root );
	
	for( c = node->children; c; c = c->next )
		if( !xt_handle( xt, c ) )
			return 0;
	
	if( node->flags & XT_COMPLETE && !( node->flags & XT_SEEN ) )
	{
		for( i = 0; xt->handlers[i].func; i ++ )
		{
			/* This one is fun! \o/ */
			
						/* If handler.name == NULL it means it should always match. */
			if( ( xt->handlers[i].name == NULL || 
						/* If it's not, compare. There should always be a name. */
			      g_strcasecmp( xt->handlers[i].name, node->name ) == 0 ) &&
						/* If handler.parent == NULL, it's a match. */
			    ( xt->handlers[i].parent == NULL ||
						/* If there's a parent node, see if the name matches. */
			      ( node->parent ? g_strcasecmp( xt->handlers[i].parent, node->parent->name ) == 0 : 
						/* If there's no parent, the handler should mention <root> as a parent. */
			                       g_strcasecmp( xt->handlers[i].parent, "<root>" ) == 0 ) ) )
			{
				st = xt->handlers[i].func( node, xt->data );
				
				if( st == XT_ABORT )
					return 0;
				else if( st != XT_NEXT )
					break;
			}
		}
		
		node->flags |= XT_SEEN;
	}
	
	return 1;
}

/* Garbage collection: Cleans up all nodes that are handled. Useful for
   streams because there's no reason to keep a complete packet history
   in memory. */
void xt_cleanup( struct xt_parser *xt, struct xt_node *node )
{
	struct xt_node *c, *prev;
	
	if( !xt || !xt->root )
		return;
	
	if( node == NULL )
		return xt_cleanup( xt, xt->root );
	
	if( node->flags & XT_SEEN && node == xt->root )
	{
		xt_free_node( xt->root );
		xt->root = xt->cur = NULL;
		/* xt->cur should be NULL already, BTW... */
		
		return;
	}
	
	/* c contains the current node, prev the previous node (or NULL).
	   I admit, this one's pretty horrible. */
	for( c = node->children, prev = NULL; c; prev = c, c = c ? c->next : node->children )
	{
		if( c->flags & XT_SEEN )
		{
			/* Remove the node from the linked list. */
			if( prev )
				prev->next = c->next;
			else
				node->children = c->next;
			
			xt_free_node( c );
			
			/* Since the for loop wants to get c->next, make sure
			   c points at something that exists (and that c->next
			   will actually be the next item we should check). c
			   can be NULL now, if we just removed the first item.
			   That explains the ? thing in for(). */
			c = prev;
		}
		else
		{
			/* This node can't be cleaned up yet, but maybe a
			   subnode can. */
			xt_cleanup( xt, c );
		}
	}
}

static void xt_to_string_real( struct xt_node *node, GString *str )
{
	char *buf;
	struct xt_node *c;
	int i;
	
	g_string_append_printf( str, "<%s", node->name );
	
	for( i = 0; node->attr[i].key; i ++ )
	{
		buf = g_markup_printf_escaped( " %s=\"%s\"", node->attr[i].key, node->attr[i].value );
		g_string_append( str, buf );
		g_free( buf );
	}
	
	if( node->text == NULL && node->children == NULL )
	{
		g_string_append( str, "/>" );
		return;
	}
	
	g_string_append( str, ">" );
	if( node->text_len > 0 )
	{
		buf = g_markup_escape_text( node->text, node->text_len );
		g_string_append( str, buf );
		g_free( buf );
	}
	
	for( c = node->children; c; c = c->next )
		xt_to_string_real( c, str );
	
	g_string_append_printf( str, "</%s>", node->name );
}

char *xt_to_string( struct xt_node *node )
{
	GString *ret;
	char *real;
	
	ret = g_string_new( "" );
	xt_to_string_real( node, ret );
	
	real = ret->str;
	g_string_free( ret, FALSE );
	
	return real;
}

void xt_print( struct xt_node *node )
{
	int i;
	struct xt_node *c;
	
	/* Indentation */
	for( c = node; c->parent; c = c->parent )
		printf( "\t" );
	
	/* Start the tag */
	printf( "<%s", node->name );
	
	/* Print the attributes */
	for( i = 0; node->attr[i].key; i ++ )
		printf( " %s=\"%s\"", node->attr[i].key, g_markup_escape_text( node->attr[i].value, -1 ) );
	
	/* /> in case there's really *nothing* inside this tag, otherwise
	   just >. */
	/* If this tag doesn't have any content at all... */
	if( node->text == NULL && node->children == NULL )
	{
		printf( "/>\n" );
		return;
		/* Then we're finished! */
	}
	
	/* Otherwise... */
	printf( ">" );
	
	/* Only print the text if it contains more than whitespace (TEST). */
	if( node->text_len > 0 )
	{
		for( i = 0; node->text[i] && isspace( node->text[i] ); i ++ );
		if( node->text[i] )
			printf( "%s", g_markup_escape_text( node->text, -1 ) );
	}
	
	if( node->children )
		printf( "\n" );
	
	for( c = node->children; c; c = c->next )
		xt_print( c );
	
	if( node->children )
		for( c = node; c->parent; c = c->parent )
			printf( "\t" );
	
	/* Non-empty tag is now finished. */
	printf( "</%s>\n", node->name );
}

/* Frees a node. This doesn't clean up references to itself from parents! */
void xt_free_node( struct xt_node *node )
{
	int i;
	
	if( !node )
		return;
	
	g_free( node->name );
	g_free( node->text );
	
	for( i = 0; node->attr[i].key; i ++ )
	{
		g_free( node->attr[i].key );
		g_free( node->attr[i].value );
	}
	g_free( node->attr );
	
	while( node->children )
	{
		struct xt_node *next = node->children->next;
		
		xt_free_node( node->children );
		node->children = next;
	}
	
	g_free( node );
}

void xt_free( struct xt_parser *xt )
{
	if( !xt )
		return;
	
	if( xt->root )
		xt_free_node( xt->root );
	
	g_markup_parse_context_free( xt->parser );
	
	g_free( xt );
}

/* To find a node's child with a specific name, pass the node's children
   list, not the node itself! The reason you have to do this by hand: So
   that you can also use this function as a find-next. */
struct xt_node *xt_find_node( struct xt_node *node, char *name )
{
	while( node )
	{
		if( g_strcasecmp( node->name, name ) == 0 )
			break;
		
		node = node->next;
	}
	
	return node;
}

char *xt_find_attr( struct xt_node *node, char *key )
{
	int i;
	
	if( !node )
		return NULL;
	
	for( i = 0; node->attr[i].key; i ++ )
		if( g_strcasecmp( node->attr[i].key, key ) == 0 )
			break;
	
	return node->attr[i].value;
}

struct xt_node *xt_new_node( char *name, char *text, struct xt_node *children )
{
	struct xt_node *node, *c;
	
	node = g_new0( struct xt_node, 1 );
	node->name = g_strdup( name );
	node->children = children;
	node->attr = g_new0( struct xt_attr, 1 );
	
	if( text )
	{
		node->text_len = strlen( text );
		node->text = g_memdup( text, node->text_len );
	}
	
	for( c = children; c; c = c->next )
	{
		if( c->parent != NULL )
		{
			/* ERROR CONDITION: They seem to have a parent already??? */
		}
		
		c->parent = node;
	}
	
	return node;
}

void xt_add_child( struct xt_node *parent, struct xt_node *child )
{
	struct xt_node *node;
	
	/* This function can actually be used to add more than one child, so
	   do handle this properly. */
	for( node = child; node; node = node->next )
	{
		if( node->parent != NULL )
		{
			/* ERROR CONDITION: They seem to have a parent already??? */
		}
		
		node->parent = parent;
	}
	
	if( parent->children == NULL )
	{
		parent->children = child;
	}
	else
	{
		for( node = parent->children; node->next; node = node->next );
		node->next = child;
	}
}

void xt_add_attr( struct xt_node *node, char *key, char *value )
{
	int i;
	
	for( i = 0; node->attr[i].key; i ++ );
	node->attr = g_renew( struct xt_attr, node->attr, i + 2 );
	node->attr[i].key = g_strdup( key );
	node->attr[i].value = g_strdup( value );
	node->attr[i+1].key = NULL;
}
