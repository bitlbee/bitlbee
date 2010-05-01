  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2010 Wilmer van der Gaast and others                *
  \********************************************************************/

/* Some stuff that doesn't belong anywhere else.                        */

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

char *set_eval_timezone( set_t *set, char *value )
{
	char *s;
	
	if( strcmp( value, "local" ) == 0 ||
	    strcmp( value, "gmt" ) == 0 || strcmp( value, "utc" ) == 0 )
		return value;
	
	/* Otherwise: +/- at the beginning optional, then one or more numbers,
	   possibly followed by a colon and more numbers. Don't bother bound-
	   checking them since users are free to shoot themselves in the foot. */
	s = value;
	if( *s == '+' || *s == '-' )
		s ++;
	
	/* \d+ */
	if( !isdigit( *s ) )
		return SET_INVALID;
	while( *s && isdigit( *s ) ) s ++;
	
	/* EOS? */
	if( *s == '\0' )
		return value;
	
	/* Otherwise, colon */
	if( *s != ':' )
		return SET_INVALID;
	s ++;
	
	/* \d+ */
	if( !isdigit( *s ) )
		return SET_INVALID;
	while( *s && isdigit( *s ) ) s ++;
	
	/* EOS */
	return *s == '\0' ? value : SET_INVALID;
}

char *irc_format_timestamp( irc_t *irc, time_t msg_ts )
{
	time_t now_ts = time( NULL );
	struct tm now, msg;
	char *set;
	
	/* If the timestamp is <= 0 or less than a minute ago, discard it as
	   it doesn't seem to add to much useful info and/or might be noise. */
	if( msg_ts <= 0 || msg_ts > now_ts - 60 )
		return NULL;
	
	set = set_getstr( &irc->b->set, "timezone" );
	if( strcmp( set, "local" ) == 0 )
	{
		localtime_r( &now_ts, &now );
		localtime_r( &msg_ts, &msg );
	}
	else
	{
		int hr, min = 0, sign = 60;
		
		if( set[0] == '-' )
		{
			sign *= -1;
			set ++;
		}
		else if( set[0] == '+' )
		{
			set ++;
		}
		
		if( sscanf( set, "%d:%d", &hr, &min ) >= 1 )
		{
			msg_ts += sign * ( hr * 60 + min );
			now_ts += sign * ( hr * 60 + min );
		}
		
		gmtime_r( &now_ts, &now );
		gmtime_r( &msg_ts, &msg );
	}
	
	if( msg.tm_year == now.tm_year && msg.tm_yday == now.tm_yday )
		return g_strdup_printf( "\x02[\x02\x02\x02%02d:%02d:%02d\x02]\x02 ",
		                        msg.tm_hour, msg.tm_min, msg.tm_sec );
	else
		return g_strdup_printf( "\x02[\x02\x02\x02%04d-%02d-%02d "
		                        "%02d:%02d:%02d\x02]\x02 ",
		                        msg.tm_year + 1900, msg.tm_mon + 1, msg.tm_mday,
		                        msg.tm_hour, msg.tm_min, msg.tm_sec );
}
