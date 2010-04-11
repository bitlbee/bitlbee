  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2010 Wilmer van der Gaast and others                *
  \********************************************************************/

/* The IRC-based UI - Sending responses to commands/etc.                */

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

void irc_send_num( irc_t *irc, int code, char *format, ... )
{
	char text[IRC_MAX_LINE];
	va_list params;
	
	va_start( params, format );
	g_vsnprintf( text, IRC_MAX_LINE, format, params );
	va_end( params );
	
	irc_write( irc, ":%s %03d %s %s", irc->root->host, code, irc->user->nick ? : "*", text );
}

void irc_send_login( irc_t *irc )
{
	irc_send_num( irc,   1, ":Welcome to the BitlBee gateway, %s", irc->user->nick );
	irc_send_num( irc,   2, ":Host %s is running BitlBee " BITLBEE_VERSION " " ARCH "/" CPU ".", irc->root->host );
	irc_send_num( irc,   3, ":%s", IRCD_INFO );
	irc_send_num( irc,   4, "%s %s %s %s", irc->root->host, BITLBEE_VERSION, UMODES UMODES_PRIV, CMODES );
	irc_send_num( irc,   5, "PREFIX=(ov)@+ CHANTYPES=%s CHANMODES=,,,%s NICKLEN=%d NETWORK=BitlBee "
	                        "CASEMAPPING=rfc1459 MAXTARGETS=1 WATCH=128 :are supported by this server",
	                        CTYPES, CMODES, MAX_NICK_LENGTH - 1 );
	irc_send_motd( irc );
}

void irc_send_motd( irc_t *irc )
{
	int fd;
	
	fd = open( global.conf->motdfile, O_RDONLY );
	if( fd == -1 )
	{
		irc_send_num( irc, 422, ":We don't need MOTDs." );
	}
	else
	{
		char linebuf[80];	/* Max. line length for MOTD's is 79 chars. It's what most IRC networks seem to do. */
		char *add, max;
		int len;
		
		linebuf[79] = len = 0;
		max = sizeof( linebuf ) - 1;
		
		irc_send_num( irc, 375, ":- %s Message Of The Day - ", irc->root->host );
		while( read( fd, linebuf + len, 1 ) == 1 )
		{
			if( linebuf[len] == '\n' || len == max )
			{
				linebuf[len] = 0;
				irc_send_num( irc, 372, ":- %s", linebuf );
				len = 0;
			}
			else if( linebuf[len] == '%' )
			{
				read( fd, linebuf + len, 1 );
				if( linebuf[len] == 'h' )
					add = irc->root->host;
				else if( linebuf[len] == 'v' )
					add = BITLBEE_VERSION;
				else if( linebuf[len] == 'n' )
					add = irc->user->nick;
				else
					add = "%";
				
				strncpy( linebuf + len, add, max - len );
				while( linebuf[++len] );
			}
			else if( len < max )
			{
				len ++;
			}
		}
		irc_send_num( irc, 376, ":End of MOTD" );
		close( fd );
	}
}

void irc_usermsg( irc_t *irc, char *format, ... )
{
	irc_channel_t *ic;
	irc_user_t *iu;
	char text[1024];
	va_list params;
	
	va_start( params, format );
	g_vsnprintf( text, sizeof( text ), format, params );
	va_end( params );
	
	if( irc->last_root_cmd &&
	    irc_channel_name_ok( irc->last_root_cmd ) && 
	    ( ic = irc_channel_by_name( irc, irc->last_root_cmd ) ) &&
	    ic->flags & IRC_CHANNEL_JOINED )
		irc_send_msg( irc->root, "PRIVMSG", irc->last_root_cmd, text, NULL );
	else if( irc->last_root_cmd &&
	         ( iu = irc_user_by_name( irc, irc->last_root_cmd ) ) &&
	         iu->f == &irc_user_root_funcs )
		irc_send_msg( iu, "PRIVMSG", irc->user->nick, text, NULL );
	else
	{
		g_free( irc->last_root_cmd );
		irc->last_root_cmd = NULL;
		
		irc_send_msg( irc->root, "PRIVMSG", irc->user->nick, text, NULL );
	}
	
	/*return( irc_msgfrom( irc, u->nick, text ) );*/
}

void irc_send_join( irc_channel_t *ic, irc_user_t *iu )
{
	irc_t *irc = ic->irc;
	
	irc_write( irc, ":%s!%s@%s JOIN :%s", iu->nick, iu->user, iu->host, ic->name );
	
	if( iu == irc->user )
	{
		irc_write( irc, ":%s MODE %s +%s", irc->root->host, ic->name, ic->mode );
		irc_send_names( ic );
		if( ic->topic && *ic->topic )
			irc_send_topic( ic, FALSE );
	}
}

void irc_send_part( irc_channel_t *ic, irc_user_t *iu, const char *reason )
{
	irc_write( ic->irc, ":%s!%s@%s PART %s :%s", iu->nick, iu->user, iu->host, ic->name, reason );
}

void irc_send_names( irc_channel_t *ic )
{
	GSList *l;
	char namelist[385] = "";
	//char *ops = set_getstr( &ic->irc->b->set, "ops" );
	
	/* RFCs say there is no error reply allowed on NAMES, so when the
	   channel is invalid, just give an empty reply. */
	for( l = ic->users; l; l = l->next )
	{
		irc_user_t *iu = l->data;
		
		if( strlen( namelist ) + strlen( iu->nick ) > sizeof( namelist ) - 4 )
		{
			irc_send_num( ic->irc, 353, "= %s :%s", ic->name, namelist );
			*namelist = 0;
		}
		
		/*
		if( u->ic && !u->away && set_getbool( &irc->set, "away_devoice" ) )
			strcat( namelist, "+" );
		else if( ( strcmp( u->nick, irc->mynick ) == 0 && ( strcmp( ops, "root" ) == 0 || strcmp( ops, "both" ) == 0 ) ) ||
		         ( strcmp( u->nick, irc->nick ) == 0 && ( strcmp( ops, "user" ) == 0 || strcmp( ops, "both" ) == 0 ) ) )
			strcat( namelist, "@" );
		*/
		
		strcat( namelist, iu->nick );
		strcat( namelist, " " );
	}
	
	if( *namelist )
		irc_send_num( ic->irc, 353, "= %s :%s", ic->name, namelist );
	
	irc_send_num( ic->irc, 366, "%s :End of /NAMES list", ic->name );
}

void irc_send_topic( irc_channel_t *ic, gboolean topic_change )
{
	if( topic_change && ic->topic_who )
	{
		irc_write( ic->irc, ":%s TOPIC %s :%s", ic->topic_who, 
		           ic->name, ic->topic && *ic->topic ? ic->topic : "" );
	}
	else if( ic->topic )
	{
		irc_send_num( ic->irc, 332, "%s :%s", ic->name, ic->topic );
		if( ic->topic_who )
			irc_send_num( ic->irc, 333, "%s %s %d",
			              ic->name, ic->topic_who, (int) ic->topic_time );
	}
	else
		irc_send_num( ic->irc, 331, "%s :No topic for this channel", ic->name );
}

void irc_send_whois( irc_user_t *iu )
{
	irc_t *irc = iu->irc;
	
	irc_send_num( irc, 311, "%s %s %s * :%s",
	              iu->nick, iu->user, iu->host, iu->fullname );
	
	if( iu->bu )
	{
		bee_user_t *bu = iu->bu;
		
		irc_send_num( irc, 312, "%s %s.%s :%s network", iu->nick, bu->ic->acc->user,
		           bu->ic->acc->server && *bu->ic->acc->server ? bu->ic->acc->server : "",
		           bu->ic->acc->prpl->name );
		
		if( bu->status || bu->status_msg )
		{
			int num = bu->flags & BEE_USER_AWAY ? 301 : 320;
			
			if( bu->status && bu->status_msg )
				irc_send_num( irc, num, "%s :%s (%s)", iu->nick, bu->status, bu->status_msg );
			else
				irc_send_num( irc, num, "%s :%s", iu->nick, bu->status ? : bu->status_msg );
		}
	}
	else
	{
		irc_send_num( irc, 312, "%s %s :%s", iu->nick, irc->root->host, IRCD_INFO " " BITLBEE_VERSION );
	}
	
	irc_send_num( irc, 318, "%s :End of /WHOIS list", iu->nick );
}

void irc_send_who( irc_t *irc, GSList *l, const char *channel )
{
	while( l )
	{
		irc_user_t *iu = l->data;
		/* TODO(wilmer): Restore away/channel information here */
		irc_send_num( irc, 352, "%s %s %s %s %s %c :0 %s",
		              channel ? : "*", iu->user, iu->host, irc->root->host,
		              iu->nick, 'H', iu->fullname );
		l = l->next;
	}
	
	irc_send_num( irc, 315, "%s :End of /WHO list", channel );
}

void irc_send_msg( irc_user_t *iu, const char *type, const char *dst, const char *msg, const char *prefix )
{
	char last = 0;
	const char *s = msg, *line = msg;
	char raw_msg[strlen(msg)+1024];
	
	while( !last )
	{
		if( *s == '\r' && *(s+1) == '\n' )
			s++;
		if( *s == '\n' )
		{
			last = s[1] == 0;
		}
		else
		{
			last = s[0] == 0;
		}
		if( *s == 0 || *s == '\n' )
		{
			if( g_strncasecmp( line, "/me ", 4 ) == 0 && ( !prefix || !*prefix ) &&
			    g_strcasecmp( type, "PRIVMSG" ) == 0 )
			{
				strcpy( raw_msg, "\001ACTION " );
				strncat( raw_msg, line + 4, s - line - 4 );
				strcat( raw_msg, "\001" );
				irc_send_msg_raw( iu, type, dst, raw_msg );
			}
			else
			{
				*raw_msg = '\0';
				if( prefix && *prefix )
					strcpy( raw_msg, prefix );
				strncat( raw_msg, line, s - line );
				irc_send_msg_raw( iu, type, dst, raw_msg );
			}
			line = s + 1;
		}
		s ++;
	}
}

void irc_send_msg_raw( irc_user_t *iu, const char *type, const char *dst, const char *msg )
{
	irc_write( iu->irc, ":%s!%s@%s %s %s :%s",
	           iu->nick, iu->user, iu->host, type, dst, msg );
}

void irc_send_nick( irc_user_t *iu, const char *new )
{
	irc_write( iu->irc, ":%s!%s@%s NICK %s",
	           iu->nick, iu->user, iu->host, new );
}
