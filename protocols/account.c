  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2010 Wilmer van der Gaast and others                *
  \********************************************************************/

/* Account management functions                                         */

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
#include "account.h"

static char *set_eval_nick_source( set_t *set, char *value );

account_t *account_add( bee_t *bee, struct prpl *prpl, char *user, char *pass )
{
	account_t *a;
	set_t *s;
	char tag[strlen(prpl->name)+10];
	
	if( bee->accounts )
	{
		for( a = bee->accounts; a->next; a = a->next );
		a = a->next = g_new0( account_t, 1 );
	}
	else
	{
		bee->accounts = a = g_new0 ( account_t, 1 );
	}
	
	a->prpl = prpl;
	a->user = g_strdup( user );
	a->pass = g_strdup( pass );
	a->auto_connect = 1;
	a->bee = bee;
	
	s = set_add( &a->set, "auto_connect", "true", set_eval_account, a );
	s->flags |= ACC_SET_NOSAVE;
	
	s = set_add( &a->set, "auto_reconnect", "true", set_eval_bool, a );
	
	s = set_add( &a->set, "nick_format", NULL, NULL, a );
	s->flags |= SET_NULL_OK;
	
	s = set_add( &a->set, "nick_source", "handle", set_eval_nick_source, a );
	s->flags |= ACC_SET_NOSAVE; /* Just for bw compatibility! */
	
	s = set_add( &a->set, "password", NULL, set_eval_account, a );
	s->flags |= ACC_SET_NOSAVE | SET_NULL_OK | SET_PASSWORD;
	
	s = set_add( &a->set, "tag", NULL, set_eval_account, a );
	s->flags |= ACC_SET_NOSAVE;
	
	s = set_add( &a->set, "username", NULL, set_eval_account, a );
	s->flags |= ACC_SET_NOSAVE | ACC_SET_OFFLINE_ONLY;
	set_setstr( &a->set, "username", user );
	
	/* Hardcode some more clever tag guesses. */
	strcpy( tag, prpl->name );
	if( strcmp( prpl->name, "oscar" ) == 0 )
	{
		if( isdigit( a->user[0] ) )
			strcpy( tag, "icq" );
		else
			strcpy( tag, "aim" );
	}
	else if( strcmp( prpl->name, "jabber" ) == 0 )
	{
		if( strstr( a->user, "@gmail.com" ) ||
		    strstr( a->user, "@googlemail.com" ) )
			strcpy( tag, "gtalk" );
		else if( strstr( a->user, "@chat.facebook.com" ) )
			strcpy( tag, "fb" );
	}
	
	if( account_by_tag( bee, tag ) )
	{
		char *numpos = tag + strlen( tag );
		int i;

		for( i = 2; i < 10000; i ++ )
		{
			sprintf( numpos, "%d", i );
			if( !account_by_tag( bee, tag ) )
				break;
		}
	}
	set_setstr( &a->set, "tag", tag );
	
	a->nicks = g_hash_table_new_full( g_str_hash, g_str_equal, g_free, g_free );
	
	/* This function adds some more settings (and might want to do more
	   things that have to be done now, although I can't think of anything. */
	if( prpl->init )
		prpl->init( a );
	
	s = set_add( &a->set, "away", NULL, set_eval_account, a );
	s->flags |= SET_NULL_OK;
	
	if( a->flags & ACC_FLAG_STATUS_MESSAGE )
	{
		s = set_add( &a->set, "status", NULL, set_eval_account, a );
		s->flags |= SET_NULL_OK;
	}
	
	return a;
}

char *set_eval_account( set_t *set, char *value )
{
	account_t *acc = set->data;
	
	/* Double-check: We refuse to edit on-line accounts. */
	if( set->flags & ACC_SET_OFFLINE_ONLY && acc->ic )
		return SET_INVALID;
	
	if( strcmp( set->key, "server" ) == 0 )
	{
		g_free( acc->server );
		if( value && *value )
		{
			acc->server = g_strdup( value );
			return value;
		}
		else
		{
			acc->server = g_strdup( set->def );
			return g_strdup( set->def );
		}
	}
	else if( strcmp( set->key, "username" ) == 0 )
	{
		g_free( acc->user );
		acc->user = g_strdup( value );
		return value;
	}
	else if( strcmp( set->key, "password" ) == 0 )
	{
		/* set -del should be allowed now, but I don't want to have any
		   NULL pointers to have to deal with. */
		if( !value )
			value = "";
		
		g_free( acc->pass );
		acc->pass = g_strdup( value );
		return NULL;	/* password shouldn't be visible in plaintext! */
	}
	else if( strcmp( set->key, "tag" ) == 0 )
	{
		account_t *oa;
		
		/* Enforce uniqueness. */
		if( ( oa = account_by_tag( acc->bee, value ) ) && oa != acc )
			return SET_INVALID;
		
		g_free( acc->tag );
		acc->tag = g_strdup( value );
		return value;
	}
	else if( strcmp( set->key, "auto_connect" ) == 0 )
	{
		if( !is_bool( value ) )
			return SET_INVALID;
		
		acc->auto_connect = bool2int( value );
		return value;
	}
	else if( strcmp( set->key, "away" ) == 0 ||
	         strcmp( set->key, "status" ) == 0 )
	{
		if( acc->ic && acc->ic->flags & OPT_LOGGED_IN )
		{
			/* If we're currently on-line, set the var now already
			   (bit of a hack) and send an update. */
			g_free( set->value );
			set->value = g_strdup( value );
			
			imc_away_send_update( acc->ic );
		}
		
		return value;
	}
	
	return SET_INVALID;
}

/* For bw compatibility, have this write-only setting. */
static char *set_eval_nick_source( set_t *set, char *value )
{
	account_t *a = set->data;
	
	if( strcmp( value, "full_name" ) == 0 )
		set_setstr( &a->set, "nick_format", "%full_name" );
	else if( strcmp( value, "first_name" ) == 0 )
		set_setstr( &a->set, "nick_format", "%first_name" );
	else
		set_setstr( &a->set, "nick_format", "%-@nick" );
	
	return value;
}

account_t *account_get( bee_t *bee, const char *id )
{
	account_t *a, *ret = NULL;
	char *handle, *s;
	int nr;
	
	/* Tags get priority above anything else. */
	if( ( a = account_by_tag( bee, id ) ) )
		return a;
	
	/* This checks if the id string ends with (...) */
	if( ( handle = strchr( id, '(' ) ) && ( s = strchr( handle, ')' ) ) && s[1] == 0 )
	{
		struct prpl *proto;
		
		*s = *handle = 0;
		handle ++;
		
		if( ( proto = find_protocol( id ) ) )
		{
			for( a = bee->accounts; a; a = a->next )
				if( a->prpl == proto &&
				    a->prpl->handle_cmp( handle, a->user ) == 0 )
					ret = a;
		}
		
		/* Restore the string. */
		handle --;
		*handle = '(';
		*s = ')';
		
		if( ret )
			return ret;
	}
	
	if( sscanf( id, "%d", &nr ) == 1 && nr < 1000 )
	{
		for( a = bee->accounts; a; a = a->next )
			if( ( nr-- ) == 0 )
				return( a );
		
		return( NULL );
	}
	
	for( a = bee->accounts; a; a = a->next )
	{
		if( g_strcasecmp( id, a->prpl->name ) == 0 )
		{
			if( !ret )
				ret = a;
			else
				return( NULL ); /* We don't want to match more than one... */
		}
		else if( strstr( a->user, id ) )
		{
			if( !ret )
				ret = a;
			else
				return( NULL );
		}
	}
	
	return( ret );
}

account_t *account_by_tag( bee_t *bee, const char *tag )
{
	account_t *a;
	
	for( a = bee->accounts; a; a = a->next )
		if( a->tag && g_strcasecmp( tag, a->tag ) == 0 )
			return a;
	
	return NULL;
}

void account_del( bee_t *bee, account_t *acc )
{
	account_t *a, *l = NULL;
	
	if( acc->ic )
		/* Caller should have checked, accounts still in use can't be deleted. */
		return;
	
	for( a = bee->accounts; a; a = (l=a)->next )
		if( a == acc )
		{
			if( l )
				l->next = a->next;
			else
				bee->accounts = a->next;
			
			/** FIXME
			for( c = bee->chatrooms; c; c = nc )
			{
				nc = c->next;
				if( acc == c->acc )
					chat_del( bee, c );
			}
			*/
			
			while( a->set )
				set_del( &a->set, a->set->key );
			
			g_hash_table_destroy( a->nicks );
			
			g_free( a->tag );
			g_free( a->user );
			g_free( a->pass );
			g_free( a->server );
			if( a->reconnect )	/* This prevents any reconnect still queued to happen */
				cancel_auto_reconnect( a );
			g_free( a );
			
			break;
		}
}

static gboolean account_on_timeout( gpointer d, gint fd, b_input_condition cond );

void account_on( bee_t *bee, account_t *a )
{
	if( a->ic )
	{
		/* Trying to enable an already-enabled account */
		return;
	}
	
	cancel_auto_reconnect( a );
	
	a->reconnect = 0;
	a->prpl->login( a );
	
	if( a->ic && !( a->ic->flags & ( OPT_SLOW_LOGIN | OPT_LOGGED_IN ) ) )
		a->ic->keepalive = b_timeout_add( 120000, account_on_timeout, a->ic );
}

void account_off( bee_t *bee, account_t *a )
{
	imc_logout( a->ic, FALSE );
	a->ic = NULL;
	if( a->reconnect )
	{
		/* Shouldn't happen */
		cancel_auto_reconnect( a );
	}
}

static gboolean account_on_timeout( gpointer d, gint fd, b_input_condition cond )
{
	struct im_connection *ic = d;
	
	if( !( ic->flags & ( OPT_SLOW_LOGIN | OPT_LOGGED_IN ) ) )
	{
		imcb_error( ic, "Connection timeout" );
		imc_logout( ic, TRUE );
	}
	
	return FALSE;
}

struct account_reconnect_delay
{
	int start;
	char op;
	int step;
	int max;
};

int account_reconnect_delay_parse( char *value, struct account_reconnect_delay *p )
{
	memset( p, 0, sizeof( *p ) );
	/* A whole day seems like a sane "maximum maximum". */
	p->max = 86400;
	
	/* Format: /[0-9]+([*+][0-9]+(<[0-9+])?)?/ */
	while( *value && isdigit( *value ) )
		p->start = p->start * 10 + *value++ - '0';
	
	/* Sure, call me evil for implementing my own fscanf here, but it's
	   dead simple and I immediately know where to continue parsing. */
	
	if( *value == 0 )
		/* If the string ends now, the delay is constant. */
		return 1;
	else if( *value != '+' && *value != '*' )
		/* Otherwise allow either a + or a * */
		return 0;
	
	p->op = *value++;
	
	/* + or * the delay by this number every time. */
	while( *value && isdigit( *value ) )
		p->step = p->step * 10 + *value++ - '0';
	
	if( *value == 0 )
		/* Use the default maximum (one day). */
		return 1;
	else if( *value != '<' )
		return 0;
	
	p->max = 0;
	value ++;
	while( *value && isdigit( *value ) )
		p->max = p->max * 10 + *value++ - '0';
	
	return p->max > 0;
}

char *set_eval_account_reconnect_delay( set_t *set, char *value )
{
	struct account_reconnect_delay p;
	
	return account_reconnect_delay_parse( value, &p ) ? value : SET_INVALID;
}

int account_reconnect_delay( account_t *a )
{
	char *setting = set_getstr( &a->bee->set, "auto_reconnect_delay" );
	struct account_reconnect_delay p;
	
	if( account_reconnect_delay_parse( setting, &p ) )
	{
		if( a->auto_reconnect_delay == 0 )
			a->auto_reconnect_delay = p.start;
		else if( p.op == '+' )
			a->auto_reconnect_delay += p.step;
		else if( p.op == '*' )
			a->auto_reconnect_delay *= p.step;
		
		if( a->auto_reconnect_delay > p.max )
			a->auto_reconnect_delay = p.max;
	}
	else
	{
		a->auto_reconnect_delay = 0;
	}
	
	return a->auto_reconnect_delay;
}
