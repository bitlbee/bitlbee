#include "bitlbee.h"

bee_t *bee_new()
{
	bee_t *b = g_new0( bee_t, 1 );
	set_t *s;
	
	s = set_add( &b->set, "away", NULL, NULL/*set_eval_away_status*/, b );
	s->flags |= SET_NULL_OK;
	s = set_add( &b->set, "auto_connect", "true", set_eval_bool, b );
	s = set_add( &b->set, "auto_reconnect", "true", set_eval_bool, b );
	s = set_add( &b->set, "auto_reconnect_delay", "5*3<900", NULL/*set_eval_account_reconnect_delay*/, b );
	s = set_add( &b->set, "debug", "false", set_eval_bool, b );
	s = set_add( &b->set, "password", NULL, NULL/*set_eval_password*/, b );
	s->flags |= SET_NULL_OK;
	s = set_add( &b->set, "save_on_quit", "true", set_eval_bool, b );
	s = set_add( &b->set, "status", NULL, NULL/*set_eval_away_status*/, b );
	s->flags |= SET_NULL_OK;
	s = set_add( &b->set, "strip_html", "true", NULL, b );
	
	return b;
}

void bee_free( bee_t *b )
{
	account_t *acc = b->accounts;
	
	while( acc )
	{
		if( acc->ic )
			imc_logout( acc->ic, FALSE );
		else if( acc->reconnect )
			cancel_auto_reconnect( acc );
		
		if( acc->ic == NULL )
			account_del( b, acc );
		else
			/* Nasty hack, but account_del() doesn't work in this
			   case and we don't want infinite loops, do we? ;-) */
			acc = acc->next;
	}
	
	while( b->set )
		set_del( &b->set, b->set->key );
	
	g_free( b );
}
