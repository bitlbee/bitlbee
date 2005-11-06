  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2004 Wilmer van der Gaast and others                *
  \********************************************************************/

/* MSN module - Miscellaneous utilities                                 */

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

#include "nogaim.h"
#include "msn.h"
#include <ctype.h>

int msn_write( struct gaim_connection *gc, char *s, int len )
{
	struct msn_data *md = gc->proto_data;
	int st;
	
	st = write( md->fd, s, len );
	if( st != len )
	{
		hide_login_progress_error( gc, "Short write() to main server" );
		signoff( gc );
		return( 0 );
	}
	
	return( 1 );
}

int msn_logged_in( struct gaim_connection *gc )
{
	struct msn_data *md = gc->proto_data;
	char buf[1024];
	
	account_online( gc );
	
	/* account_online() sets an away state if there is any, so only
	   execute this code if we're not away. */
	if( md->away_state == msn_away_state_list )
	{
		g_snprintf( buf, sizeof( buf ), "CHG %d %s %d\r\n", ++md->trId, md->away_state->code, 0 );
		return( msn_write( gc, buf, strlen( buf ) ) );
	}
	
	return( 0 );
}

int msn_buddy_list_add( struct gaim_connection *gc, char *list, char *who, char *realname_ )
{
	struct msn_data *md = gc->proto_data;
	GSList *l, **lp = NULL;
	char buf[1024], *realname;
	
	if( strcmp( list, "AL" ) == 0 )
		lp = &gc->permit;
	else if( strcmp( list, "BL" ) == 0 )
		lp = &gc->deny;
	
	if( lp )
		for( l = *lp; l; l = l->next )
			if( g_strcasecmp( l->data, who ) == 0 )
				return( 1 );
	
	realname = g_new0( char, strlen( realname_ ) * 3 + 1 );
	strcpy( realname, realname_ );
	http_encode( realname );
	
	g_snprintf( buf, sizeof( buf ), "ADD %d %s %s %s\r\n", ++md->trId, list, who, realname );
	if( msn_write( gc, buf, strlen( buf ) ) )
	{
		g_free( realname );
		
		if( lp )
			*lp = g_slist_append( *lp, g_strdup( who ) );
		
		return( 1 );
	}
	
	g_free( realname );
	
	return( 0 );
}

int msn_buddy_list_remove( struct gaim_connection *gc, char *list, char *who )
{
	struct msn_data *md = gc->proto_data;
	GSList *l = NULL, **lp = NULL;
	char buf[1024];
	
	if( strcmp( list, "AL" ) == 0 )
		lp = &gc->permit;
	else if( strcmp( list, "BL" ) == 0 )
		lp = &gc->deny;
	
	if( lp )
	{
		for( l = *lp; l; l = l->next )
			if( g_strcasecmp( l->data, who ) == 0 )
				break;
		
		if( !l )
			return( 1 );
	}
	
	g_snprintf( buf, sizeof( buf ), "REM %d %s %s\r\n", ++md->trId, list, who );
	if( msn_write( gc, buf, strlen( buf ) ) )
	{
		if( lp )
			*lp = g_slist_remove( *lp, l->data );
		
		return( 1 );
	}
	
	return( 0 );
}

struct msn_buddy_ask_data
{
	struct gaim_connection *gc;
	char *handle;
	char *realname;
};

static void msn_buddy_ask_yes( gpointer w, struct msn_buddy_ask_data *bla )
{
	msn_buddy_list_add( bla->gc, "AL", bla->handle, bla->realname );
	
	g_free( bla->handle );
	g_free( bla->realname );
	g_free( bla );
}

static void msn_buddy_ask_no( gpointer w, struct msn_buddy_ask_data *bla )
{
	msn_buddy_list_add( bla->gc, "BL", bla->handle, bla->realname );
	
	g_free( bla->handle );
	g_free( bla->realname );
	g_free( bla );
}

void msn_buddy_ask( struct gaim_connection *gc, char *handle, char *realname )
{
	struct msn_buddy_ask_data *bla = g_new0( struct msn_buddy_ask_data, 1 );
	char buf[1024];
	
	bla->gc = gc;
	bla->handle = g_strdup( handle );
	bla->realname = g_strdup( realname );
	
	g_snprintf( buf, sizeof( buf ),
	            "The user %s (%s) wants to add you to his/her buddy list. Do you want to allow this?",
	            handle, realname );
	do_ask_dialog( gc, buf, bla, msn_buddy_ask_yes, msn_buddy_ask_no );
}

char *msn_findheader( char *text, char *header, int len )
{
	int hlen = strlen( header ), i;
	char *ret;
	
	if( len == 0 )
		len = strlen( text );
	
	i = 0;
	while( ( i + hlen ) < len )
	{
		/* Maybe this is a bit over-commented, but I just hate this part... */
		if( g_strncasecmp( text + i, header, hlen ) == 0 )
		{
			/* Skip to the (probable) end of the header */
			i += hlen;
			
			/* Find the first non-[: \t] character */
			while( i < len && ( text[i] == ':' || text[i] == ' ' || text[i] == '\t' ) ) i ++;
			
			/* Make sure we're still inside the string */
			if( i >= len ) return( NULL );
			
			/* Save the position */
			ret = text + i;
			
			/* Search for the end of this line */
			while( i < len && text[i] != '\r' && text[i] != '\n' ) i ++;
			
			/* Make sure we're still inside the string */
			if( i >= len ) return( NULL );
			
			/* Copy the found data */
			return( g_strndup( ret, text + i - ret ) );
		}
		
		/* This wasn't the header we were looking for, skip to the next line. */
		while( i < len && ( text[i] != '\r' && text[i] != '\n' ) ) i ++;
		while( i < len && ( text[i] == '\r' || text[i] == '\n' ) ) i ++;
		
		/* End of headers? */
		if( strncmp( text + i - 2, "\n\n", 2 ) == 0 ||
		    strncmp( text + i - 4, "\r\n\r\n", 4 ) == 0 ||
		    strncmp( text + i - 2, "\r\r", 2 ) == 0 )
		{
			break;
		}
	}
	
	return( NULL );
}

/* *NOT* thread-safe, but that's not a problem for now... */
char **msn_linesplit( char *line )
{
	static char **ret = NULL;
	static int size = 3;
	int i, n = 0;
	
	if( ret == NULL )
		ret = g_new0( char*, size );
	
	for( i = 0; line[i] && line[i] == ' '; i ++ );
	if( line[i] )
	{
		ret[n++] = line + i;
		for( i ++; line[i]; i ++ )
		{
			if( line[i] == ' ' )
				line[i] = 0;
			else if( line[i] != ' ' && !line[i-1] )
				ret[n++] = line + i;
			
			if( n >= size )
				ret = g_renew( char*, ret, size += 2 );
		}
	}
	ret[n] = NULL;
	
	return( ret );
}

/* This one handles input from a MSN Messenger server. Both the NS and SB servers usually give
   commands, but sometimes they give additional data (payload). This function tries to handle
   this all in a nice way and send all data to the right places. */

/* Return values: -1: Read error, abort connection.
                   0: Command reported error; Abort *immediately*. (The connection does not exist anymore)
                   1: OK */

int msn_handler( struct msn_handler_data *h )
{
	int st;
	
	h->rxq = g_renew( char, h->rxq, h->rxlen + 1024 );
	st = read( h->fd, h->rxq + h->rxlen, 1024 );
	h->rxlen += st;
	
	if( st <= 0 )
		return( -1 );
	
	while( st )
	{
		int i;
		
		if( h->msglen == 0 )
		{
			for( i = 0; i < h->rxlen; i ++ )
			{
				if( h->rxq[i] == '\r' || h->rxq[i] == '\n' )
				{
					char *cmd_text, **cmd;
					int count;
					
					cmd_text = g_strndup( h->rxq, i );
					cmd = msn_linesplit( cmd_text );
					for( count = 0; cmd[count]; count ++ );
					st = h->exec_command( h->data, cmd, count );
					g_free( cmd_text );
					
					/* If the connection broke, don't continue. We don't even exist anymore. */
					if( !st )
						return( 0 );
					
					if( h->msglen )
						h->cmd_text = g_strndup( h->rxq, i );
					
					/* Skip to the next non-emptyline */
					while( i < h->rxlen && ( h->rxq[i] == '\r' || h->rxq[i] == '\n' ) ) i ++;
					
					break;
				}
			}
			
			/* If we reached the end of the buffer, there's still an incomplete command there.
			   Return and wait for more data. */
			if( i == h->rxlen && h->rxq[i-1] != '\r' && h->rxq[i-1] != '\n' )
				break;
		}
		else
		{
			char *msg, **cmd;
			int count;
			
			/* Do we have the complete message already? */
			if( h->msglen > h->rxlen )
				break;
			
			msg = g_strndup( h->rxq, h->msglen );
			cmd = msn_linesplit( h->cmd_text );
			for( count = 0; cmd[count]; count ++ );
			
			st = h->exec_message( h->data, msg, h->msglen, cmd, count );
			g_free( msg );
			g_free( h->cmd_text );
			h->cmd_text = NULL;
			
			if( !st )
				return( 0 );
			
			i = h->msglen;
			h->msglen = 0;
		}
		
		/* More data after this block? */
		if( i < h->rxlen )
		{
			char *tmp;
			
			tmp = g_memdup( h->rxq + i, h->rxlen - i );
			g_free( h->rxq );
			h->rxq = tmp;
			h->rxlen -= i;
			i = 0;
		}
		else
		/* If not, reset the rx queue and get lost. */
		{
			g_free( h->rxq );
			h->rxq = g_new0( char, 1 );
			h->rxlen = 0;
			return( 1 );
		}
	}
	
	return( 1 );
}
