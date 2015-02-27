/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Simple XML (stream) parse tree handling code (Jabber/XMPP, mainly)       *
*                                                                           *
*  Copyright 2006-2012 Wilmer van der Gaast <wilmer@gaast.net>              *
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
****************************************************************************/

#include <glib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <stdio.h>

#include "xmltree.h"

#define g_strcasecmp g_ascii_strcasecmp
#define g_strncasecmp g_ascii_strncasecmp

static void xt_start_element(GMarkupParseContext *ctx, const gchar *element_name, const gchar **attr_names,
                             const gchar **attr_values, gpointer data, GError **error)
{
	struct xt_parser *xt = data;
	struct xt_node *node = g_new0(struct xt_node, 1), *nt;
	int i;

	node->parent = xt->cur;
	node->name = g_strdup(element_name);

	/* First count the number of attributes */
	for (i = 0; attr_names[i]; i++) {
		;
	}

	/* Then allocate a NULL-terminated array. */
	node->attr = g_new0(struct xt_attr, i + 1);

	/* And fill it, saving one variable by starting at the end. */
	for (i--; i >= 0; i--) {
		node->attr[i].key = g_strdup(attr_names[i]);
		node->attr[i].value = g_strdup(attr_values[i]);
	}

	/* Add it to the linked list of children nodes, if we have a current
	   node yet. */
	if (xt->cur) {
		if (xt->cur->children) {
			for (nt = xt->cur->children; nt->next; nt = nt->next) {
				;
			}
			nt->next = node;
		} else {
			xt->cur->children = node;
		}
	} else if (xt->root) {
		/* ERROR situation: A second root-element??? */
	}

	/* Now this node will be the new current node. */
	xt->cur = node;
	/* And maybe this is the root? */
	if (xt->root == NULL) {
		xt->root = node;
	}
}

static void xt_text(GMarkupParseContext *ctx, const gchar *text, gsize text_len, gpointer data, GError **error)
{
	struct xt_parser *xt = data;
	struct xt_node *node = xt->cur;

	if (node == NULL) {
		return;
	}

	/* FIXME: Does g_renew also OFFICIALLY accept NULL arguments? */
	node->text = g_renew(char, node->text, node->text_len + text_len + 1);
	memcpy(node->text + node->text_len, text, text_len);
	node->text_len += text_len;
	/* Zero termination is always nice to have. */
	node->text[node->text_len] = 0;
}

static void xt_end_element(GMarkupParseContext *ctx, const gchar *element_name, gpointer data, GError **error)
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

struct xt_parser *xt_new(const struct xt_handler_entry *handlers, gpointer data)
{
	struct xt_parser *xt = g_new0(struct xt_parser, 1);

	xt->data = data;
	xt->handlers = handlers;
	xt_reset(xt);

	return xt;
}

/* Reset the parser, flush everything we have so far. For example, we need
   this for XMPP when doing TLS/SASL to restart the stream. */
void xt_reset(struct xt_parser *xt)
{
	if (xt->parser) {
		g_markup_parse_context_free(xt->parser);
	}

	xt->parser = g_markup_parse_context_new(&xt_parser_funcs, 0, xt, NULL);

	if (xt->root) {
		xt_free_node(xt->root);
		xt->root = NULL;
		xt->cur = NULL;
	}
}

/* Feed the parser, don't execute any handler. Returns -1 on errors, 0 on
   end-of-stream and 1 otherwise. */
int xt_feed(struct xt_parser *xt, const char *text, int text_len)
{
	if (!g_markup_parse_context_parse(xt->parser, text, text_len, &xt->gerr)) {
		return -1;
	}

	return !(xt->root && xt->root->flags & XT_COMPLETE);
}

/* Find completed nodes and see if a handler has to be called. Passing
   a node isn't necessary if you want to start at the root, just pass
   NULL. This second argument is needed for recursive calls. */
int xt_handle(struct xt_parser *xt, struct xt_node *node, int depth)
{
	struct xt_node *c;
	xt_status st;
	int i;

	if (xt->root == NULL) {
		return 1;
	}

	if (node == NULL) {
		return xt_handle(xt, xt->root, depth);
	}

	if (depth != 0) {
		for (c = node->children; c; c = c->next) {
			if (!xt_handle(xt, c, depth > 0 ? depth - 1 : depth)) {
				return 0;
			}
		}
	}

	if (node->flags & XT_COMPLETE && !(node->flags & XT_SEEN)) {
		if (xt->handlers) {
			for (i = 0; xt->handlers[i].func; i++) {
				/* This one is fun! \o/ */

				/* If handler.name == NULL it means it should always match. */
				if ((xt->handlers[i].name == NULL ||
				     /* If it's not, compare. There should always be a name. */
				     g_strcasecmp(xt->handlers[i].name, node->name) == 0) &&
				    /* If handler.parent == NULL, it's a match. */
				    (xt->handlers[i].parent == NULL ||
				     /* If there's a parent node, see if the name matches. */
				     (node->parent ? g_strcasecmp(xt->handlers[i].parent, node->parent->name) == 0 :
				      /* If there's no parent, the handler should mention <root> as a parent. */
				      strcmp(xt->handlers[i].parent, "<root>") == 0))) {
					st = xt->handlers[i].func(node, xt->data);

					if (st == XT_ABORT) {
						return 0;
					} else if (st != XT_NEXT) {
						break;
					}
				}
			}
		}

		node->flags |= XT_SEEN;
	}

	return 1;
}

/* Garbage collection: Cleans up all nodes that are handled. Useful for
   streams because there's no reason to keep a complete packet history
   in memory. */
void xt_cleanup(struct xt_parser *xt, struct xt_node *node, int depth)
{
	struct xt_node *c, *prev;

	if (!xt || !xt->root) {
		return;
	}

	if (node == NULL) {
		xt_cleanup(xt, xt->root, depth);
		return;
	}

	if (node->flags & XT_SEEN && node == xt->root) {
		xt_free_node(xt->root);
		xt->root = xt->cur = NULL;
		/* xt->cur should be NULL already, BTW... */

		return;
	}

	/* c contains the current node, prev the previous node (or NULL).
	   I admit, this one's pretty horrible. */
	for (c = node->children, prev = NULL; c; prev = c, c = c ? c->next : node->children) {
		if (c->flags & XT_SEEN) {
			/* Remove the node from the linked list. */
			if (prev) {
				prev->next = c->next;
			} else {
				node->children = c->next;
			}

			xt_free_node(c);

			/* Since the for loop wants to get c->next, make sure
			   c points at something that exists (and that c->next
			   will actually be the next item we should check). c
			   can be NULL now, if we just removed the first item.
			   That explains the ? thing in for(). */
			c = prev;
		} else {
			/* This node can't be cleaned up yet, but maybe a
			   subnode can. */
			if (depth != 0) {
				xt_cleanup(xt, c, depth > 0 ? depth - 1 : depth);
			}
		}
	}
}

struct xt_node *xt_from_string(const char *in, int len)
{
	struct xt_parser *parser;
	struct xt_node *ret = NULL;

	if (len == 0) {
		len = strlen(in);
	}

	parser = xt_new(NULL, NULL);
	xt_feed(parser, in, len);
	if (parser->cur == NULL) {
		ret = parser->root;
		parser->root = NULL;
	}
	xt_free(parser);

	return ret;
}

static void xt_to_string_real(struct xt_node *node, GString *str, int indent)
{
	char *buf;
	struct xt_node *c;
	int i;

	if (indent > 1) {
		g_string_append_len(str, "\n\t\t\t\t\t\t\t\t",
		                    indent < 8 ? indent : 8);
	}

	g_string_append_printf(str, "<%s", node->name);

	for (i = 0; node->attr[i].key; i++) {
		buf = g_markup_printf_escaped(" %s=\"%s\"", node->attr[i].key, node->attr[i].value);
		g_string_append(str, buf);
		g_free(buf);
	}

	if (node->text == NULL && node->children == NULL) {
		g_string_append(str, "/>");
		return;
	}

	g_string_append(str, ">");
	if (node->text_len > 0) {
		buf = g_markup_escape_text(node->text, node->text_len);
		g_string_append(str, buf);
		g_free(buf);
	}

	for (c = node->children; c; c = c->next) {
		xt_to_string_real(c, str, indent ? indent + 1 : 0);
	}

	if (indent > 0 && node->children) {
		g_string_append_len(str, "\n\t\t\t\t\t\t\t\t",
		                    indent < 8 ? indent : 8);
	}

	g_string_append_printf(str, "</%s>", node->name);
}

char *xt_to_string(struct xt_node *node)
{
	GString *ret;

	ret = g_string_new("");
	xt_to_string_real(node, ret, 0);
	return g_string_free(ret, FALSE);
}

/* WITH indentation! */
char *xt_to_string_i(struct xt_node *node)
{
	GString *ret;

	ret = g_string_new("");
	xt_to_string_real(node, ret, 1);
	return g_string_free(ret, FALSE);
}

void xt_print(struct xt_node *node)
{
	char *str = xt_to_string_i(node);

	fprintf(stderr, "%s", str);
	g_free(str);
}

struct xt_node *xt_dup(struct xt_node *node)
{
	struct xt_node *dup = g_new0(struct xt_node, 1);
	struct xt_node *c, *dc = NULL;
	int i;

	/* Let's NOT copy the parent element here BTW! Only do it for children. */

	dup->name = g_strdup(node->name);
	dup->flags = node->flags;
	if (node->text) {
		dup->text = g_memdup(node->text, node->text_len + 1);
		dup->text_len = node->text_len;
	}

	/* Count the number of attributes and allocate the new array. */
	for (i = 0; node->attr[i].key; i++) {
		;
	}
	dup->attr = g_new0(struct xt_attr, i + 1);

	/* Copy them all! */
	for (i--; i >= 0; i--) {
		dup->attr[i].key = g_strdup(node->attr[i].key);
		dup->attr[i].value = g_strdup(node->attr[i].value);
	}

	/* This nice mysterious loop takes care of the children. */
	for (c = node->children; c; c = c->next) {
		if (dc == NULL) {
			dc = dup->children = xt_dup(c);
		} else {
			dc = (dc->next = xt_dup(c));
		}

		dc->parent = dup;
	}

	return dup;
}

/* Frees a node. This doesn't clean up references to itself from parents! */
void xt_free_node(struct xt_node *node)
{
	int i;

	if (!node) {
		return;
	}

	g_free(node->name);
	g_free(node->text);

	for (i = 0; node->attr[i].key; i++) {
		g_free(node->attr[i].key);
		g_free(node->attr[i].value);
	}
	g_free(node->attr);

	while (node->children) {
		struct xt_node *next = node->children->next;

		xt_free_node(node->children);
		node->children = next;
	}

	g_free(node);
}

void xt_free(struct xt_parser *xt)
{
	if (!xt) {
		return;
	}

	if (xt->root) {
		xt_free_node(xt->root);
	}

	g_markup_parse_context_free(xt->parser);

	g_free(xt);
}

/* To find a node's child with a specific name, pass the node's children
   list, not the node itself! The reason you have to do this by hand: So
   that you can also use this function as a find-next. */
struct xt_node *xt_find_node(struct xt_node *node, const char *name)
{
	while (node) {
		char *colon;

		if (g_strcasecmp(node->name, name) == 0 ||
		    ((colon = strchr(node->name, ':')) &&
		     g_strcasecmp(colon + 1, name) == 0)) {
			break;
		}

		node = node->next;
	}

	return node;
}

/* More advanced than the one above, understands something like
   ../foo/bar to find a subnode bar of a node foo which is a child
   of node's parent. Pass the node directly, not its list of children. */
struct xt_node *xt_find_path(struct xt_node *node, const char *name)
{
	while (name && *name && node) {
		char *colon, *slash;
		int n;

		if ((slash = strchr(name, '/'))) {
			n = slash - name;
		} else {
			n = strlen(name);
		}

		if (strncmp(name, "..", n) == 0) {
			node = node->parent;
		} else {
			node = node->children;

			while (node) {
				if (g_strncasecmp(node->name, name, n) == 0 ||
				    ((colon = strchr(node->name, ':')) &&
				     g_strncasecmp(colon + 1, name, n) == 0)) {
					break;
				}

				node = node->next;
			}
		}

		name = slash ? slash + 1 : NULL;
	}

	return node;
}

char *xt_find_attr(struct xt_node *node, const char *key)
{
	int i;
	char *colon;

	if (!node) {
		return NULL;
	}

	for (i = 0; node->attr[i].key; i++) {
		if (g_strcasecmp(node->attr[i].key, key) == 0) {
			break;
		}
	}

	/* This is an awful hack that only takes care of namespace prefixes
	   inside a tag. Since IMHO excessive namespace usage in XMPP is
	   massive overkill anyway (this code exists for almost four years
	   now and never really missed it): Meh. */
	if (!node->attr[i].key && strcmp(key, "xmlns") == 0 &&
	    (colon = strchr(node->name, ':'))) {
		*colon = '\0';
		for (i = 0; node->attr[i].key; i++) {
			if (strncmp(node->attr[i].key, "xmlns:", 6) == 0 &&
			    strcmp(node->attr[i].key + 6, node->name) == 0) {
				break;
			}
		}
		*colon = ':';
	}

	return node->attr[i].value;
}

struct xt_node *xt_find_node_by_attr(struct xt_node *xt, const char *tag, const char *key, const char *value)
{
	struct xt_node *c;
	char *s;

	for (c = xt; (c = xt_find_node(c, tag)); c = c->next) {
		if ((s = xt_find_attr(c, key)) && strcmp(s, value) == 0) {
			return c;
		}
	}
	return NULL;
}


/* Strip a few non-printable characters that aren't allowed in XML streams
   (and upset some XMPP servers for example). */
void xt_strip_text(char *in)
{
	char *out = in;
	static const char nonprint[32] = {
		0, 0, 0, 0, 0, 0, 0, 0, /* 0..7 */
		0, 1, 1, 0, 0, 1, 0, 0, /* 9 (tab), 10 (\n), 13 (\r) */
	};

	if (!in) {
		return;
	}

	while (*in) {
		if ((unsigned int) *in >= ' ' || nonprint[(unsigned int) *in]) {
			*out++ = *in;
		}
		in++;
	}
	*out = *in;
}

struct xt_node *xt_new_node(char *name, const char *text, struct xt_node *children)
{
	struct xt_node *node, *c;

	node = g_new0(struct xt_node, 1);
	node->name = g_strdup(name);
	node->children = children;
	node->attr = g_new0(struct xt_attr, 1);

	if (text) {
		node->text = g_strdup(text);
		xt_strip_text(node->text);
		node->text_len = strlen(node->text);
	}

	for (c = children; c; c = c->next) {
		if (c->parent != NULL) {
			/* ERROR CONDITION: They seem to have a parent already??? */
		}

		c->parent = node;
	}

	return node;
}

void xt_add_child(struct xt_node *parent, struct xt_node *child)
{
	struct xt_node *node;

	/* This function can actually be used to add more than one child, so
	   do handle this properly. */
	for (node = child; node; node = node->next) {
		if (node->parent != NULL) {
			/* ERROR CONDITION: They seem to have a parent already??? */
		}

		node->parent = parent;
	}

	if (parent->children == NULL) {
		parent->children = child;
	} else {
		for (node = parent->children; node->next; node = node->next) {
			;
		}
		node->next = child;
	}
}

/* Same, but at the beginning. */
void xt_insert_child(struct xt_node *parent, struct xt_node *child)
{
	struct xt_node *node, *last = NULL;

	if (child == NULL) {
		return; /* BUG */

	}
	for (node = child; node; node = node->next) {
		if (node->parent != NULL) {
			/* ERROR CONDITION: They seem to have a parent already??? */
		}

		node->parent = parent;
		last = node;
	}

	last->next = parent->children;
	parent->children = child;
}

void xt_add_attr(struct xt_node *node, const char *key, const char *value)
{
	int i;

	/* Now actually it'd be nice if we can also change existing attributes
	   (which actually means this function doesn't have the right name).
	   So let's find out if we have this attribute already... */
	for (i = 0; node->attr[i].key; i++) {
		if (strcmp(node->attr[i].key, key) == 0) {
			break;
		}
	}

	if (node->attr[i].key == NULL) {
		/* If not, allocate space for a new attribute. */
		node->attr = g_renew(struct xt_attr, node->attr, i + 2);
		node->attr[i].key = g_strdup(key);
		node->attr[i + 1].key = NULL;
	} else {
		/* Otherwise, free the old value before setting the new one. */
		g_free(node->attr[i].value);
	}

	node->attr[i].value = g_strdup(value);
}

int xt_remove_attr(struct xt_node *node, const char *key)
{
	int i, last;

	for (i = 0; node->attr[i].key; i++) {
		if (strcmp(node->attr[i].key, key) == 0) {
			break;
		}
	}

	/* If we didn't find the attribute... */
	if (node->attr[i].key == NULL) {
		return 0;
	}

	g_free(node->attr[i].key);
	g_free(node->attr[i].value);

	/* If it's the last, this is easy: */
	if (node->attr[i + 1].key == NULL) {
		node->attr[i].key = node->attr[i].value = NULL;
	} else { /* It's also pretty easy, actually. */
		/* Find the last item. */
		for (last = i + 1; node->attr[last + 1].key; last++) {
			;
		}

		node->attr[i] = node->attr[last];
		node->attr[last].key = NULL;
		node->attr[last].value = NULL;
	}

	/* Let's not bother with reallocating memory here. It takes time and
	   most packets don't stay in memory for long anyway. */

	return 1;
}
