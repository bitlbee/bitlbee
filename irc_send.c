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
	
	return;
}

void irc_send_login( irc_t *irc )
{
	irc_user_t *iu = irc->user;
	
	irc->user = irc_user_new( irc, iu->nick );
	irc->user->user = iu->user;
	irc->user->fullname = iu->fullname;
	g_free( iu->nick );
	g_free( iu );
	
	irc_send_num( irc,   1, ":Welcome to the BitlBee gateway, %s", irc->user->nick );
	irc_send_num( irc,   2, ":Host %s is running BitlBee " BITLBEE_VERSION " " ARCH "/" CPU ".", irc->root->host );
	irc_send_num( irc,   3, ":%s", IRCD_INFO );
	irc_send_num( irc,   4, "%s %s %s %s", irc->root->host, BITLBEE_VERSION, UMODES UMODES_PRIV, CMODES );
	irc_send_num( irc,   5, "PREFIX=(ov)@+ CHANTYPES=%s CHANMODES=,,,%s NICKLEN=%d NETWORK=BitlBee "
	                        "CASEMAPPING=rfc1459 MAXTARGETS=1 WATCH=128 :are supported by this server",
	                        CTYPES, CMODES, MAX_NICK_LENGTH - 1 );
	irc_send_motd( irc );
	irc->umode[0] = '\0';
	/*irc_umode_set( irc, "+" UMODE, 1 );*/
	
	irc_usermsg( irc, "Welcome to the BitlBee gateway!\n\n"
	                  "If you've never used BitlBee before, please do read the help "
	                  "information using the \x02help\x02 command. Lots of FAQs are "
	                  "answered there.\n"
	                  "If you already have an account on this server, just use the "
	                  "\x02identify\x02 command to identify yourself." );
	
	if( global.conf->runmode == RUNMODE_FORKDAEMON || global.conf->runmode == RUNMODE_DAEMON )
		ipc_to_master_str( "CLIENT %s %s :%s\r\n", irc->user->host, irc->user->nick, irc->user->fullname );
	
	irc->status |= USTATUS_LOGGED_IN;
	
	/* This is for bug #209 (use PASS to identify to NickServ). */
	if( irc->password != NULL )
	{
		char *send_cmd[] = { "identify", g_strdup( irc->password ), NULL };
		
		/*irc_setpass( irc, NULL );*/
		/*root_command( irc, send_cmd );*/
		g_free( send_cmd[1] );
	}
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

/* FIXME/REPLACEME */
int irc_usermsg( irc_t *irc, char *format, ... )
{
	char text[1024];
	va_list params;
	//irc_user_t *iu;
	
	va_start( params, format );
	g_vsnprintf( text, sizeof( text ), format, params );
	va_end( params );
	
	fprintf( stderr, "%s\n", text );
	
	return 1;
	
	/*return( irc_msgfrom( irc, u->nick, text ) );*/
}
