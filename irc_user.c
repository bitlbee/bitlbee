  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2004 Wilmer van der Gaast and others                *
  \********************************************************************/

/* Stuff to handle, save and search IRC buddies                         */

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

#include "bitlbee.h"
#include "ipc.h"

irc_user_t *irc_user_new( irc_t *irc, const char *nick )
{
	irc_user_t *iu = g_new0( irc_user_t, 1 );
	
	iu->irc = irc;
	iu->nick = g_strdup( nick );
	iu->user = iu->host = iu->fullname = iu->nick;
	
	iu->key = g_strdup( nick );
	nick_lc( iu->key );
	/* Using the hash table for speed and irc->users for easy iteration
	   through the list (since the GLib API doesn't have anything sane
	   for that.) */
	g_hash_table_insert( irc->nick_user_hash, iu->key, iu );
	irc->users = g_slist_insert_sorted( irc->users, iu, irc_user_cmp );
	
	return iu;
}

int irc_user_free( irc_t *irc, irc_user_t *iu )
{
	static struct im_connection *last_ic;
	static char *msg;
	
	if( !iu )
		return 0;
	
	if( iu->bu &&
	    ( iu->bu->ic->flags & OPT_LOGGING_OUT ) &&
	    iu->bu->ic != last_ic )
	{
		char host_prefix[] = "bitlbee.";
		char *s;
		
		/* Irssi recognises netsplits by quitmsgs with two
		   hostnames, where a hostname is a "word" with one
		   of more dots. Mangle no-dot hostnames a bit. */
		if( strchr( irc->root->host, '.' ) )
			*host_prefix = '\0';
		
		last_ic = iu->bu->ic;
		g_free( msg );
		if( !set_getbool( &irc->b->set, "simulate_netsplit" ) )
			msg = g_strdup( "Account off-line" );
		else if( ( s = strchr( iu->bu->ic->acc->user, '@' ) ) )
			msg = g_strdup_printf( "%s%s %s", host_prefix,
			        irc->root->host, s + 1 );
		else
			msg = g_strdup_printf( "%s%s %s.%s",
				host_prefix, irc->root->host,
				iu->bu->ic->acc->prpl->name, irc->root->host );
	}
	else if( !iu->bu || !( iu->bu->ic->flags & OPT_LOGGING_OUT ) )
	{
		g_free( msg );
		msg = g_strdup( "Removed" );
		last_ic = NULL;
	}
	irc_user_quit( iu, msg );
	
	irc->users = g_slist_remove( irc->users, iu );
	g_hash_table_remove( irc->nick_user_hash, iu->key );
	
	g_free( iu->nick );
	if( iu->nick != iu->user ) g_free( iu->user );
	if( iu->nick != iu->host ) g_free( iu->host );
	if( iu->nick != iu->fullname ) g_free( iu->fullname );
	g_free( iu->pastebuf );
	if( iu->pastebuf_timer ) b_event_remove( iu->pastebuf_timer );
	g_free( iu->key );
	g_free( iu );
	
	return 1;
}

irc_user_t *irc_user_by_name( irc_t *irc, const char *nick )
{
	char key[strlen(nick)+1];
	
	strcpy( key, nick );
	if( nick_lc( key ) )
		return g_hash_table_lookup( irc->nick_user_hash, key );
	else
		return NULL;
}

int irc_user_set_nick( irc_user_t *iu, const char *new )
{
	irc_t *irc = iu->irc;
	irc_user_t *new_iu;
	char key[strlen(new)+1];
	GSList *cl;
	
	strcpy( key, new );
	if( iu == NULL || !nick_lc( key ) ||
	    ( ( new_iu = irc_user_by_name( irc, new ) ) && new_iu != iu ) )
		return 0;
	
	for( cl = irc->channels; cl; cl = cl->next )
	{
		irc_channel_t *ic = cl->data;
		
		/* Send a NICK update if we're renaming our user, or someone
		   who's in the same channel like our user. */
		if( iu == irc->user ||
		    ( ( ic->flags & IRC_CHANNEL_JOINED ) &&
		      irc_channel_has_user( ic, iu ) ) )
		{
			irc_send_nick( iu, new );
			break;
		}
	}
	
	irc->users = g_slist_remove( irc->users, iu );
	g_hash_table_remove( irc->nick_user_hash, iu->key );
	
	if( iu->nick == iu->user ) iu->user = NULL;
	if( iu->nick == iu->host ) iu->host = NULL;
	if( iu->nick == iu->fullname ) iu->fullname = NULL;
	g_free( iu->nick );
	iu->nick = g_strdup( new );
	if( iu->user == NULL ) iu->user = g_strdup( iu->nick );
	if( iu->host == NULL ) iu->host = g_strdup( iu->nick );
	if( iu->fullname == NULL ) iu->fullname = g_strdup( iu->nick );
	
	g_free( iu->key );
	iu->key = g_strdup( key );
	g_hash_table_insert( irc->nick_user_hash, iu->key, iu );
	irc->users = g_slist_insert_sorted( irc->users, iu, irc_user_cmp );
	
	if( iu == irc->user )
		ipc_to_master_str( "NICK :%s\r\n", new );
	
	return 1;
}

gint irc_user_cmp( gconstpointer a_, gconstpointer b_ )
{
	const irc_user_t *a = a_, *b = b_;
	
	return strcmp( a->key, b->key );
}

const char *irc_user_get_away( irc_user_t *iu )
{
	irc_t *irc = iu->irc;
	bee_user_t *bu = iu->bu;
	
	if( iu == irc->user )
		return set_getstr( &irc->b->set, "away" );
	else if( bu )
	{
		if( !bu->flags & BEE_USER_ONLINE )
			return "Offline";
		else if( bu->flags & BEE_USER_AWAY )
		{
			if( bu->status_msg )
			{
				static char ret[MAX_STRING];
				g_snprintf( ret, MAX_STRING - 1, "%s (%s)",
				            bu->status ? : "Away", bu->status_msg );
				return ret;
			}
			else
				return bu->status ? : "Away";
		}
	}
	
	return NULL;
}

void irc_user_quit( irc_user_t *iu, const char *msg )
{
	GSList *l;
	gboolean send_quit = FALSE;
	
	if( !iu )
		return;
	
	for( l = iu->irc->channels; l; l = l->next )
	{
		irc_channel_t *ic = l->data;
		send_quit |= irc_channel_del_user( ic, iu, IRC_CDU_SILENT, NULL ) &&
		             ( ic->flags & IRC_CHANNEL_JOINED );
	}
	
	if( send_quit )
		irc_send_quit( iu, msg );
}

/* User-type dependent functions, for root/NickServ: */
static gboolean root_privmsg( irc_user_t *iu, const char *msg )
{
	char cmd[strlen(msg)+1];
	
	strcpy( cmd, msg );
	root_command_string( iu->irc, cmd );
	
	return TRUE;
}

static gboolean root_ctcp( irc_user_t *iu, char * const *ctcp )
{
	if( g_strcasecmp( ctcp[0], "VERSION" ) == 0 )
	{
		irc_send_msg_f( iu, "NOTICE", iu->irc->user->nick, "\001%s %s\001",
		                ctcp[0], PACKAGE " " BITLBEE_VERSION " " ARCH "/" CPU );
	}
	else if( g_strcasecmp( ctcp[0], "PING" ) == 0 )
	{
		irc_send_msg_f( iu, "NOTICE", iu->irc->user->nick, "\001%s %s\001",
		                ctcp[0], ctcp[1] ? : "" );
	}
	
	return TRUE;
}

const struct irc_user_funcs irc_user_root_funcs = {
	root_privmsg,
	root_ctcp,
};

/* Echo to yourself: */
static gboolean self_privmsg( irc_user_t *iu, const char *msg )
{
	irc_send_msg( iu, "PRIVMSG", iu->nick, msg, NULL );
	
	return TRUE;
}

const struct irc_user_funcs irc_user_self_funcs = {
	self_privmsg,
};
