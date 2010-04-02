  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2010 Wilmer van der Gaast and others                *
  \********************************************************************/

/* Some glue to put the IRC and the IM stuff together.                  */

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


/* IM->IRC callbacks */

static const struct irc_user_funcs irc_user_im_funcs;

static gboolean bee_irc_user_new( bee_t *bee, bee_user_t *bu )
{
	irc_user_t *iu;
	char nick[MAX_NICK_LENGTH+1], *s;
	
	memset( nick, 0, MAX_NICK_LENGTH + 1 );
	strcpy( nick, nick_get( bu->ic->acc, bu->handle ) );
	
	bu->ui_data = iu = irc_user_new( (irc_t*) bee->ui_data, nick );
	iu->bu = bu;
	
	if( ( s = strchr( bu->handle, '@' ) ) )
	{
		iu->host = g_strdup( s + 1 );
		iu->user = g_strndup( bu->handle, s - bu->handle );
	}
	else if( bu->ic->acc->server )
	{
		iu->host = g_strdup( bu->ic->acc->server );
		iu->user = g_strdup( bu->handle );
		
		/* s/ /_/ ... important for AOL screennames */
		for( s = iu->user; *s; s ++ )
			if( *s == ' ' )
				*s = '_';
	}
	else
	{
		iu->host = g_strdup( bu->ic->acc->prpl->name );
		iu->user = g_strdup( bu->handle );
	}
	
	if( set_getbool( &bee->set, "private" ) )
		iu->flags |= IRC_USER_PRIVATE;
	
	iu->f = &irc_user_im_funcs;
	//iu->last_typing_notice = 0;
	
	return TRUE;
}

static gboolean bee_irc_user_free( bee_t *bee, bee_user_t *bu )
{
	return irc_user_free( bee->ui_data, bu->ui_data );
}

static gboolean bee_irc_user_status( bee_t *bee, bee_user_t *bu, bee_user_t *old )
{
	irc_t *irc = bee->ui_data;
	irc_channel_t *ic = irc->channels->data; /* For now, just pick the first channel. */
	
	if( ( bu->flags & BEE_USER_ONLINE ) != ( old->flags & BEE_USER_ONLINE ) )
	{
		if( bu->flags & BEE_USER_ONLINE )
			irc_channel_add_user( ic, (irc_user_t*) bu->ui_data );
		else
			irc_channel_del_user( ic, (irc_user_t*) bu->ui_data );
	}
	
	return TRUE;
}

static gboolean bee_irc_user_msg( bee_t *bee, bee_user_t *bu, const char *msg, time_t sent_at )
{
	irc_t *irc = bee->ui_data;
	irc_channel_t *ic = irc->channels->data;
	irc_user_t *iu = (irc_user_t *) bu->ui_data;
	char *dst, *prefix = NULL;
	char *wrapped;
	
	if( iu->flags & IRC_USER_PRIVATE )
	{
		dst = irc->user->nick;
	}
	else
	{
		dst = ic->name;
		prefix = g_strdup_printf( "%s%s", irc->user->nick, set_getstr( &bee->set, "to_char" ) );
	}
	
	wrapped = word_wrap( msg, 425 );
	irc_send_msg( iu, "PRIVMSG", dst, wrapped, prefix );
	
	g_free( wrapped );
	g_free( prefix );
	
	return TRUE;
}

const struct bee_ui_funcs irc_ui_funcs = {
	bee_irc_user_new,
	bee_irc_user_free,
	bee_irc_user_status,
	bee_irc_user_msg,
};


/* IRC->IM calls */

static gboolean bee_irc_user_privmsg( irc_user_t *iu, const char *msg )
{
	if( iu->bu )
		return bee_user_msg( iu->irc->b, iu->bu, msg, 0 );
	else
		return FALSE;
}

static const struct irc_user_funcs irc_user_im_funcs = {
	bee_irc_user_privmsg,
};
