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

int msn_write( struct im_connection *ic, char *s, int len )
{
	struct msn_data *md = ic->proto_data;
	int st;
	
	st = write( md->fd, s, len );
	if( st != len )
	{
		imcb_error( ic, "Short write() to main server" );
		imc_logout( ic, TRUE );
		return 0;
	}
	
	return 1;
}

int msn_logged_in( struct im_connection *ic )
{
	imcb_connected( ic );
	
	return( 0 );
}

int msn_buddy_list_add( struct im_connection *ic, const char *list, const char *who, const char *realname_, const char *group )
{
	struct msn_data *md = ic->proto_data;
	char buf[1024], *realname, groupid[8];
	
	*groupid = '\0';
	if( group )
	{
		int i;
		for( i = 0; i < md->groupcount; i ++ )
			if( g_strcasecmp( md->grouplist[i], group ) == 0 )
			{
				g_snprintf( groupid, sizeof( groupid ), " %d", i );
				break;
			}
		
		if( *groupid == '\0' )
		{
			/* Have to create this group, it doesn't exist yet. */
			struct msn_groupadd *ga;
			GSList *l;
			
			for( l = md->grpq; l; l = l->next )
			{
				ga = l->data;
				if( g_strcasecmp( ga->group, group ) == 0 )
					break;
			}
			
			ga = g_new0( struct msn_groupadd, 1 );
			ga->who = g_strdup( who );
			ga->group = g_strdup( group );
			md->grpq = g_slist_prepend( md->grpq, ga );
			
			if( l == NULL )
			{
				char *groupname = msn_http_encode( group );
				g_snprintf( buf, sizeof( buf ), "ADG %d %s %d\r\n", ++md->trId, groupname, 0 );
				g_free( groupname );
				return msn_write( ic, buf, strlen( buf ) );
			}
			else
			{
				/* This can happen if the user's doing lots of adds to a
				   new group at once; we're still waiting for the server
				   to confirm group creation. */
				return 1;
			}
		}
	}
	
	realname = msn_http_encode( realname_ );
	g_snprintf( buf, sizeof( buf ), "ADD %d %s %s %s%s\r\n", ++md->trId, list, who, realname, groupid );
	g_free( realname );
	
	return msn_write( ic, buf, strlen( buf ) );
}

int msn_buddy_list_remove( struct im_connection *ic, char *list, const char *who, const char *group )
{
	struct msn_data *md = ic->proto_data;
	char buf[1024], groupid[8];
	
	*groupid = '\0';
	if( group )
	{
		int i;
		for( i = 0; i < md->groupcount; i ++ )
			if( g_strcasecmp( md->grouplist[i], group ) == 0 )
			{
				g_snprintf( groupid, sizeof( groupid ), " %d", i );
				break;
			}
	}
	
	g_snprintf( buf, sizeof( buf ), "REM %d %s %s%s\r\n", ++md->trId, list, who, groupid );
	if( msn_write( ic, buf, strlen( buf ) ) )
		return( 1 );
	
	return( 0 );
}

struct msn_buddy_ask_data
{
	struct im_connection *ic;
	char *handle;
	char *realname;
};

static void msn_buddy_ask_yes( void *data )
{
	struct msn_buddy_ask_data *bla = data;
	
	msn_buddy_list_add( bla->ic, "AL", bla->handle, bla->realname, NULL );
	
	imcb_ask_add( bla->ic, bla->handle, NULL );
	
	g_free( bla->handle );
	g_free( bla->realname );
	g_free( bla );
}

static void msn_buddy_ask_no( void *data )
{
	struct msn_buddy_ask_data *bla = data;
	
	msn_buddy_list_add( bla->ic, "BL", bla->handle, bla->realname, NULL );
	
	g_free( bla->handle );
	g_free( bla->realname );
	g_free( bla );
}

void msn_buddy_ask( struct im_connection *ic, char *handle, char *realname )
{
	struct msn_buddy_ask_data *bla = g_new0( struct msn_buddy_ask_data, 1 );
	char buf[1024];
	
	bla->ic = ic;
	bla->handle = g_strdup( handle );
	bla->realname = g_strdup( realname );
	
	g_snprintf( buf, sizeof( buf ),
	            "The user %s (%s) wants to add you to his/her buddy list.",
	            handle, realname );
	imcb_ask( ic, buf, bla, msn_buddy_ask_yes, msn_buddy_ask_no );
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
		if( ( i >= 4 && strncmp( text + i - 4, "\r\n\r\n", 4 ) == 0 ) ||
		    ( i >= 2 && ( strncmp( text + i - 2, "\n\n", 2 ) == 0 ||   
		                  strncmp( text + i - 2, "\r\r", 2 ) == 0 ) ) )
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

/* The difference between this function and the normal http_encode() function
   is that this one escapes every 7-bit ASCII character because this is said
   to avoid some lame server-side checks when setting a real-name. Also,
   non-ASCII characters are not escaped because MSN servers don't seem to
   appreciate that! */
char *msn_http_encode( const char *input )
{
	char *ret, *s;
	int i;
	
	ret = s = g_new0( char, strlen( input ) * 3 + 1 );
	for( i = 0; input[i]; i ++ )
		if( input[i] & 128 )
		{
			*s = input[i];
			s ++;
		}
		else
		{
			g_snprintf( s, 4, "%%%02X", input[i] );
			s += 3;
		}
	
	return ret;
}

void msn_msgq_purge( struct im_connection *ic, GSList **list )
{
	struct msn_message *m;
	GString *ret;
	GSList *l;
	int n = 0;
	
	l = *list;
	if( l == NULL )
		return;
	
	m = l->data;
	ret = g_string_sized_new( 1024 );
	g_string_printf( ret, "Warning: Cleaning up MSN (switchboard) connection with unsent "
	                      "messages to %s:", m->who ? m->who : "unknown recipient" );
	
	while( l )
	{
		m = l->data;
		
		if( strncmp( m->text, "\r\r\r", 3 ) != 0 )
		{
			g_string_append_printf( ret, "\n%s", m->text );
			n ++;
		}
		
		g_free( m->who );
		g_free( m->text );
		g_free( m );
		
		l = l->next;
	}
	g_slist_free( *list );
	*list = NULL;
	
	if( n > 0 )
		imcb_log( ic, "%s", ret->str );
	g_string_free( ret, TRUE );
}

gboolean msn_set_display_name( struct im_connection *ic, const char *rawname )
{
	char *fn = msn_http_encode( rawname );
	struct msn_data *md = ic->proto_data;
	char buf[1024];
	
	g_snprintf( buf, sizeof( buf ), "REA %d %s %s\r\n", ++md->trId, ic->acc->user, fn );
	g_free( fn );
	
	return msn_write( ic, buf, strlen( buf ) ) != 0;
}
