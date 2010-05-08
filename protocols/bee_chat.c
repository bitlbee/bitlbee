  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2010 Wilmer van der Gaast and others                *
  \********************************************************************/

/* Stuff to handle rooms                                                */

/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License with
  the Debian GNU/Linux distribution in /usr/share/common-licenses/GPL;
  if not, write to the Free Software Foundation, Inc., 59 Temple Place,
  Suite 330, Boston, MA  02111-1307  USA
*/

#define BITLBEE_CORE
#include "bitlbee.h"

struct groupchat *imcb_chat_new( struct im_connection *ic, const char *handle )
{
	struct groupchat *c = g_new0( struct groupchat, 1 );
	bee_t *bee = ic->bee;
	
	/* This one just creates the conversation structure, user won't see
	   anything yet until s/he is joined to the conversation. (This
	   allows you to add other already present participants first.) */
	
	ic->groupchats = g_slist_prepend( ic->groupchats, c );
	c->ic = ic;
	c->title = g_strdup( handle );
	c->topic = g_strdup_printf( "BitlBee groupchat: \"%s\". Please keep in mind that root-commands won't work here. Have fun!", c->title );
	
	if( set_getbool( &ic->bee->set, "debug" ) )
		imcb_log( ic, "Creating new conversation: (id=%p,handle=%s)", c, handle );
	
	if( bee->ui->chat_new )
		bee->ui->chat_new( bee, c );
	
	return c;
}

void imcb_chat_name_hint( struct groupchat *c, const char *name )
{
#if 0
	if( !c->joined )
	{
		struct im_connection *ic = c->ic;
		char stripped[MAX_NICK_LENGTH+1], *full_name;
		
		strncpy( stripped, name, MAX_NICK_LENGTH );
		stripped[MAX_NICK_LENGTH] = '\0';
		nick_strip( stripped );
		if( set_getbool( &ic->irc->set, "lcnicks" ) )
			nick_lc( stripped );
		
		full_name = g_strdup_printf( "&%s", stripped );
		
		if( stripped[0] &&
		    nick_cmp( stripped, ic->irc->channel + 1 ) != 0 &&
		    irc_chat_by_channel( ic->irc, full_name ) == NULL )
		{
			g_free( c->channel );
			c->channel = full_name;
		}
		else
		{
			g_free( full_name );
		}
	}
#endif
}

void imcb_chat_free( struct groupchat *c )
{
	struct im_connection *ic = c->ic;
	bee_t *bee = ic->bee;
	GList *ir;
	
	if( bee->ui->chat_free )
		bee->ui->chat_free( bee, c );
	
	if( set_getbool( &ic->bee->set, "debug" ) )
		imcb_log( ic, "You were removed from conversation %p", c );
	
	ic->groupchats = g_slist_remove( ic->groupchats, c );
	
	for( ir = c->in_room; ir; ir = ir->next )
		g_free( ir->data );
	g_list_free( c->in_room );
	g_free( c->title );
	g_free( c->topic );
	g_free( c );
}

void imcb_chat_msg( struct groupchat *c, const char *who, char *msg, uint32_t flags, time_t sent_at )
{
	struct im_connection *ic = c->ic;
	bee_t *bee = ic->bee;
	bee_user_t *bu;
	char *s;
	
	/* Gaim sends own messages through this too. IRC doesn't want this, so kill them */
	if( g_strcasecmp( who, ic->acc->user ) == 0 )
		return;
	
	bu = bee_user_by_handle( bee, ic, who );
	
	s = set_getstr( &ic->bee->set, "strip_html" );
	if( ( g_strcasecmp( s, "always" ) == 0 ) ||
	    ( ( ic->flags & OPT_DOES_HTML ) && s ) )
		strip_html( msg );
	
	if( bu && bee->ui->chat_msg )
		bee->ui->chat_msg( bee, c, bu, msg, sent_at );
	else
		imcb_chat_log( c, "Message from unknown participant %s: %s", who, msg );
}

void imcb_chat_log( struct groupchat *c, char *format, ... )
{
	struct im_connection *ic = c->ic;
	bee_t *bee = ic->bee;
	va_list params;
	char *text;
	
	if( !bee->ui->chat_log )
		return;
	
	va_start( params, format );
	text = g_strdup_vprintf( format, params );
	va_end( params );
	
	bee->ui->chat_log( bee, c, text );
	g_free( text );
}

void imcb_chat_topic( struct groupchat *c, char *who, char *topic, time_t set_at )
{
#if 0
	struct im_connection *ic = c->ic;
	user_t *u = NULL;
	
	if( who == NULL)
		u = user_find( ic->irc, ic->irc->mynick );
	else if( g_strcasecmp( who, ic->acc->user ) == 0 )
		u = user_find( ic->irc, ic->irc->nick );
	else
		u = user_findhandle( ic, who );
	
	if( ( g_strcasecmp( set_getstr( &ic->bee->set, "strip_html" ), "always" ) == 0 ) ||
	    ( ( ic->flags & OPT_DOES_HTML ) && set_getbool( &ic->bee->set, "strip_html" ) ) )
		strip_html( topic );
	
	g_free( c->topic );
	c->topic = g_strdup( topic );
	
	if( c->joined && u )
		irc_write( ic->irc, ":%s!%s@%s TOPIC %s :%s", u->nick, u->user, u->host, c->channel, topic );
#endif
}

void imcb_chat_add_buddy( struct groupchat *c, const char *handle )
{
	struct im_connection *ic = c->ic;
	bee_t *bee = ic->bee;
	bee_user_t *bu = bee_user_by_handle( bee, ic, handle );
	gboolean me;
	
	if( set_getbool( &c->ic->bee->set, "debug" ) )
		imcb_log( c->ic, "User %s added to conversation %p", handle, c );
	
	me = ic->acc->prpl->handle_cmp( handle, ic->acc->user ) == 0;
	
	/* Most protocols allow people to join, even when they're not in
	   your contact list. Try to handle that here */
	if( !me && !bu )
		bu = bee_user_new( bee, ic, handle );
	
	/* Add the handle to the room userlist */
	/* TODO: Use bu instead of a string */
	c->in_room = g_list_append( c->in_room, g_strdup( handle ) );
	
	if( bee->ui->chat_add_user )
		bee->ui->chat_add_user( bee, c, me ? bee->user : bu );
	
	if( me )
		c->joined = 1;
}

void imcb_chat_remove_buddy( struct groupchat *c, const char *handle, const char *reason )
{
	struct im_connection *ic = c->ic;
	bee_t *bee = ic->bee;
	bee_user_t *bu = NULL;
	
	if( set_getbool( &bee->set, "debug" ) )
		imcb_log( ic, "User %s removed from conversation %p (%s)", handle, c, reason ? reason : "" );
	
	/* It might be yourself! */
	if( g_strcasecmp( handle, ic->acc->user ) == 0 )
	{
		if( c->joined == 0 )
			return;
		
		bu = bee->user;
		c->joined = 0;
	}
	else
	{
		bu = bee_user_by_handle( bee, ic, handle );
	}
	
	if( bee->ui->chat_remove_user )
		bee->ui->chat_remove_user( bee, c, bu );
}

#if 0
static int remove_chat_buddy_silent( struct groupchat *b, const char *handle )
{
	GList *i;
	
	/* Find the handle in the room userlist and shoot it */
	i = b->in_room;
	while( i )
	{
		if( g_strcasecmp( handle, i->data ) == 0 )
		{
			g_free( i->data );
			b->in_room = g_list_remove( b->in_room, i->data );
			return( 1 );
		}
		
		i = i->next;
	}
	
	return 0;
}
#endif
