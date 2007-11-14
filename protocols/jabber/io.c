/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Jabber module - I/O stuff (plain, SSL), queues, etc                      *
*                                                                           *
*  Copyright 2006 Wilmer van der Gaast <wilmer@gaast.net>                   *
*                                                                           *
*  This program is free software; you can redistribute it and/or modify     *
*  it under the terms of the GNU General Public License as published by     *
*  the Free Software Foundation; either version 2 of the License, or        *
*  (at your option) any later version.                                      *
*                                                                           *
*  This program is distributed in the hope that it will be useful,          *
*  but WITHOUT ANY WARRANTY; without even the implied warranty of           *
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
*  GNU General Public License for more details.                             *
*                                                                           *
*  You should have received a copy of the GNU General Public License along  *
*  with this program; if not, write to the Free Software Foundation, Inc.,  *
*  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.              *
*                                                                           *
\***************************************************************************/

#include "jabber.h"
#include "ssl_client.h"

static gboolean jabber_write_callback( gpointer data, gint fd, b_input_condition cond );
static gboolean jabber_write_queue( struct im_connection *ic );

int jabber_write_packet( struct im_connection *ic, struct xt_node *node )
{
	char *buf;
	int st;
	
	buf = xt_to_string( node );
	st = jabber_write( ic, buf, strlen( buf ) );
	g_free( buf );
	
	return st;
}

int jabber_write( struct im_connection *ic, char *buf, int len )
{
	struct jabber_data *jd = ic->proto_data;
	gboolean ret;
	
	if( jd->tx_len == 0 )
	{
		/* If the queue is empty, allocate a new buffer. */
		jd->tx_len = len;
		jd->txq = g_memdup( buf, len );
		
		/* Try if we can write it immediately so we don't have to do
		   it via the event handler. If not, add the handler. (In
		   most cases it probably won't be necessary.) */
		if( ( ret = jabber_write_queue( ic ) ) && jd->tx_len > 0 )
			jd->w_inpa = b_input_add( jd->fd, GAIM_INPUT_WRITE, jabber_write_callback, ic );
	}
	else
	{
		/* Just add it to the buffer if it's already filled. The
		   event handler is already set. */
		jd->txq = g_renew( char, jd->txq, jd->tx_len + len );
		memcpy( jd->txq + jd->tx_len, buf, len );
		jd->tx_len += len;
		
		/* The return value for write() doesn't necessarily mean
		   that everything got sent, it mainly means that the
		   connection (officially) still exists and can still
		   be accessed without hitting SIGSEGV. IOW: */
		ret = TRUE;
	}
	
	return ret;
}

/* Splitting up in two separate functions: One to use as a callback and one
   to use in the function above to escape from having to wait for the event
   handler to call us, if possible.
   
   Two different functions are necessary because of the return values: The
   callback should only return TRUE if the write was successful AND if the
   buffer is not empty yet (ie. if the handler has to be called again when
   the socket is ready for more data). */
static gboolean jabber_write_callback( gpointer data, gint fd, b_input_condition cond )
{
	struct jabber_data *jd = ((struct im_connection *)data)->proto_data;
	
	return jd->fd != -1 &&
	       jabber_write_queue( data ) &&
	       jd->tx_len > 0;
}

static gboolean jabber_write_queue( struct im_connection *ic )
{
	struct jabber_data *jd = ic->proto_data;
	int st;
	
	if( jd->ssl )
		st = ssl_write( jd->ssl, jd->txq, jd->tx_len );
	else
		st = write( jd->fd, jd->txq, jd->tx_len );
	
	if( st == jd->tx_len )
	{
		/* We wrote everything, clear the buffer. */
		g_free( jd->txq );
		jd->txq = NULL;
		jd->tx_len = 0;
		
		return TRUE;
	}
	else if( st == 0 || ( st < 0 && !sockerr_again() ) )
	{
		/* Set fd to -1 to make sure we won't write to it anymore. */
		closesocket( jd->fd );	/* Shouldn't be necessary after errors? */
		jd->fd = -1;
		
		imcb_error( ic, "Short write() to server" );
		imc_logout( ic, TRUE );
		return FALSE;
	}
	else if( st > 0 )
	{
		char *s;
		
		s = g_memdup( jd->txq + st, jd->tx_len - st );
		jd->tx_len -= st;
		g_free( jd->txq );
		jd->txq = s;
		
		return TRUE;
	}
	else
	{
		/* Just in case we had EINPROGRESS/EAGAIN: */
		
		return TRUE;
	}
}

static gboolean jabber_read_callback( gpointer data, gint fd, b_input_condition cond )
{
	struct im_connection *ic = data;
	struct jabber_data *jd = ic->proto_data;
	char buf[512];
	int st;
	
	if( jd->fd == -1 )
		return FALSE;
	
	if( jd->ssl )
		st = ssl_read( jd->ssl, buf, sizeof( buf ) );
	else
		st = read( jd->fd, buf, sizeof( buf ) );
	
	if( st > 0 )
	{
		/* Parse. */
		if( xt_feed( jd->xt, buf, st ) < 0 )
		{
			imcb_error( ic, "XML stream error" );
			imc_logout( ic, TRUE );
			return FALSE;
		}
		
		/* Execute all handlers. */
		if( !xt_handle( jd->xt, NULL, 1 ) )
		{
			/* Don't do anything, the handlers should have
			   aborted the connection already. */
			return FALSE;
		}
		
		if( jd->flags & JFLAG_STREAM_RESTART )
		{
			jd->flags &= ~JFLAG_STREAM_RESTART;
			jabber_start_stream( ic );
		}
		
		/* Garbage collection. */
		xt_cleanup( jd->xt, NULL, 1 );
		
		/* This is a bit hackish, unfortunately. Although xmltree
		   has nifty event handler stuff, it only calls handlers
		   when nodes are complete. Since the server should only
		   send an opening <stream:stream> tag, we have to check
		   this by hand. :-( */
		if( !( jd->flags & JFLAG_STREAM_STARTED ) && jd->xt && jd->xt->root )
		{
			if( g_strcasecmp( jd->xt->root->name, "stream:stream" ) == 0 )
			{
				jd->flags |= JFLAG_STREAM_STARTED;
				
				/* If there's no version attribute, assume
				   this is an old server that can't do SASL
				   authentication. */
				if( !sasl_supported( ic ) )
				{
					/* If there's no version= tag, we suppose
					   this server does NOT implement: XMPP 1.0,
					   SASL and TLS. */
					if( set_getbool( &ic->acc->set, "tls" ) )
					{
						imcb_error( ic, "TLS is turned on for this "
						          "account, but is not supported by this server" );
						imc_logout( ic, FALSE );
						return FALSE;
					}
					else
					{
						return jabber_init_iq_auth( ic );
					}
				}
			}
			else
			{
				imcb_error( ic, "XML stream error" );
				imc_logout( ic, TRUE );
				return FALSE;
			}
		}
	}
	else if( st == 0 || ( st < 0 && !sockerr_again() ) )
	{
		closesocket( jd->fd );
		jd->fd = -1;
		
		imcb_error( ic, "Error while reading from server" );
		imc_logout( ic, TRUE );
		return FALSE;
	}
	
	/* EAGAIN/etc or a successful read. */
	return TRUE;
}

gboolean jabber_connected_plain( gpointer data, gint source, b_input_condition cond )
{
	struct im_connection *ic = data;
	
	if( source == -1 )
	{
		imcb_error( ic, "Could not connect to server" );
		imc_logout( ic, TRUE );
		return FALSE;
	}
	
	imcb_log( ic, "Connected to server, logging in" );
	
	return jabber_start_stream( ic );
}

gboolean jabber_connected_ssl( gpointer data, void *source, b_input_condition cond )
{
	struct im_connection *ic = data;
	struct jabber_data *jd = ic->proto_data;
	
	if( source == NULL )
	{
		/* The SSL connection will be cleaned up by the SSL lib
		   already, set it to NULL here to prevent a double cleanup: */
		jd->ssl = NULL;
		
		imcb_error( ic, "Could not connect to server" );
		imc_logout( ic, TRUE );
		return FALSE;
	}
	
	imcb_log( ic, "Connected to server, logging in" );
	
	return jabber_start_stream( ic );
}

static xt_status jabber_end_of_stream( struct xt_node *node, gpointer data )
{
	imc_logout( data, TRUE );
	return XT_ABORT;
}

static xt_status jabber_pkt_features( struct xt_node *node, gpointer data )
{
	struct im_connection *ic = data;
	struct jabber_data *jd = ic->proto_data;
	struct xt_node *c, *reply;
	int trytls;
	
	trytls = g_strcasecmp( set_getstr( &ic->acc->set, "tls" ), "try" ) == 0;
	c = xt_find_node( node->children, "starttls" );
	if( c && !jd->ssl )
	{
		/* If the server advertises the STARTTLS feature and if we're
		   not in a secure connection already: */
		
		c = xt_find_node( c->children, "required" );
		
		if( c && ( !trytls && !set_getbool( &ic->acc->set, "tls" ) ) )
		{
			imcb_error( ic, "Server requires TLS connections, but TLS is turned off for this account" );
			imc_logout( ic, FALSE );
			
			return XT_ABORT;
		}
		
		/* Only run this if the tls setting is set to true or try: */
		if( ( trytls || set_getbool( &ic->acc->set, "tls" ) ) )
		{
			reply = xt_new_node( "starttls", NULL, NULL );
			xt_add_attr( reply, "xmlns", XMLNS_TLS );
			if( !jabber_write_packet( ic, reply ) )
			{
				xt_free_node( reply );
				return XT_ABORT;
			}
			xt_free_node( reply );
			
			return XT_HANDLED;
		}
	}
	else if( !c && !jd->ssl )
	{
		/* If the server does not advertise the STARTTLS feature and
		   we're not in a secure connection already: (Servers have a
		   habit of not advertising <starttls/> anymore when already
		   using SSL/TLS. */
		
		if( !trytls && set_getbool( &ic->acc->set, "tls" ) )
		{
			imcb_error( ic, "TLS is turned on for this account, but is not supported by this server" );
			imc_logout( ic, FALSE );
			
			return XT_ABORT;
		}
	}
	
	/* This one used to be in jabber_handlers[], but it has to be done
	   from here to make sure the TLS session will be initialized
	   properly before we attempt SASL authentication. */
	if( ( c = xt_find_node( node->children, "mechanisms" ) ) )
	{
		if( sasl_pkt_mechanisms( c, data ) == XT_ABORT )
			return XT_ABORT;
	}
	/* If the server *SEEMS* to support SASL authentication but doesn't
	   support it after all, we should try to do authentication the
	   other way. jabber.com doesn't seem to do SASL while it pretends
	   to be XMPP 1.0 compliant! */
	else if( !( jd->flags & JFLAG_AUTHENTICATED ) && sasl_supported( ic ) )
	{
		if( !jabber_init_iq_auth( ic ) )
			return XT_ABORT;
	}
	
	if( ( c = xt_find_node( node->children, "bind" ) ) )
	{
		reply = xt_new_node( "bind", NULL, xt_new_node( "resource", set_getstr( &ic->acc->set, "resource" ), NULL ) );
		xt_add_attr( reply, "xmlns", XMLNS_BIND );
		reply = jabber_make_packet( "iq", "set", NULL, reply );
		jabber_cache_add( ic, reply, jabber_pkt_bind_sess );
		
		if( !jabber_write_packet( ic, reply ) )
			return XT_ABORT;
		
		jd->flags |= JFLAG_WAIT_BIND;
	}
	
	if( ( c = xt_find_node( node->children, "session" ) ) )
	{
		reply = xt_new_node( "session", NULL, NULL );
		xt_add_attr( reply, "xmlns", XMLNS_SESSION );
		reply = jabber_make_packet( "iq", "set", NULL, reply );
		jabber_cache_add( ic, reply, jabber_pkt_bind_sess );
		
		if( !jabber_write_packet( ic, reply ) )
			return XT_ABORT;
		
		jd->flags |= JFLAG_WAIT_SESSION;
	}
	
	/* This flag is already set if we authenticated via SASL, so now
	   we can resume the session in the new stream, if we don't have
	   to bind/initialize the session. */
	if( jd->flags & JFLAG_AUTHENTICATED && ( jd->flags & ( JFLAG_WAIT_BIND | JFLAG_WAIT_SESSION ) ) == 0 )
	{
		if( !jabber_get_roster( ic ) )
			return XT_ABORT;
	}
	
	return XT_HANDLED;
}

static xt_status jabber_pkt_proceed_tls( struct xt_node *node, gpointer data )
{
	struct im_connection *ic = data;
	struct jabber_data *jd = ic->proto_data;
	char *xmlns;
	
	xmlns = xt_find_attr( node, "xmlns" );
	
	/* Just ignore it when it doesn't seem to be TLS-related (is that at
	   all possible??). */
	if( !xmlns || strcmp( xmlns, XMLNS_TLS ) != 0 )
		return XT_HANDLED;
	
	/* We don't want event handlers to touch our TLS session while it's
	   still initializing! */
	b_event_remove( jd->r_inpa );
	if( jd->tx_len > 0 )
	{
		/* Actually the write queue should be empty here, but just
		   to be sure... */
		b_event_remove( jd->w_inpa );
		g_free( jd->txq );
		jd->txq = NULL;
		jd->tx_len = 0;
	}
	jd->w_inpa = jd->r_inpa = 0;
	
	imcb_log( ic, "Converting stream to TLS" );
	
	jd->ssl = ssl_starttls( jd->fd, jabber_connected_ssl, ic );
	
	return XT_HANDLED;
}

static xt_status jabber_pkt_stream_error( struct xt_node *node, gpointer data )
{
	struct im_connection *ic = data;
	struct xt_node *c;
	char *s, *type = NULL, *text = NULL;
	int allow_reconnect = TRUE;
	
	for( c = node->children; c; c = c->next )
	{
		if( !( s = xt_find_attr( c, "xmlns" ) ) ||
		    strcmp( s, XMLNS_STREAM_ERROR ) != 0 )
			continue;
		
		if( strcmp( c->name, "text" ) != 0 )
		{
			type = c->name;
		}
		/* Only use the text if it doesn't have an xml:lang attribute,
		   if it's empty or if it's set to something English. */
		else if( !( s = xt_find_attr( c, "xml:lang" ) ) ||
		         !*s || strncmp( s, "en", 2 ) == 0 )
		{
			text = c->text;
		}
	}
	
	/* Tssk... */
	if( type == NULL )
	{
		imcb_error( ic, "Unknown stream error reported by server" );
		imc_logout( ic, allow_reconnect );
		return XT_ABORT;
	}
	
	/* We know that this is a fatal error. If it's a "conflict" error, we
	   should turn off auto-reconnect to make sure we won't get some nasty
	   infinite loop! */
	if( strcmp( type, "conflict" ) == 0 )
	{
		imcb_error( ic, "Account and resource used from a different location" );
		allow_reconnect = FALSE;
	}
	else
	{
		imcb_error( ic, "Stream error: %s%s%s", type, text ? ": " : "", text ? text : "" );
	}
	
	imc_logout( ic, allow_reconnect );
	
	return XT_ABORT;
}

static const struct xt_handler_entry jabber_handlers[] = {
	{ "stream:stream",      "<root>",               jabber_end_of_stream },
	{ "message",            "stream:stream",        jabber_pkt_message },
	{ "presence",           "stream:stream",        jabber_pkt_presence },
	{ "iq",                 "stream:stream",        jabber_pkt_iq },
	{ "stream:features",    "stream:stream",        jabber_pkt_features },
	{ "stream:error",       "stream:stream",        jabber_pkt_stream_error },
	{ "proceed",            "stream:stream",        jabber_pkt_proceed_tls },
	{ "challenge",          "stream:stream",        sasl_pkt_challenge },
	{ "success",            "stream:stream",        sasl_pkt_result },
	{ "failure",            "stream:stream",        sasl_pkt_result },
	{ NULL,                 NULL,                   NULL }
};

gboolean jabber_start_stream( struct im_connection *ic )
{
	struct jabber_data *jd = ic->proto_data;
	int st;
	char *greet;
	
	/* We'll start our stream now, so prepare everything to receive one
	   from the server too. */
	xt_free( jd->xt );	/* In case we're RE-starting. */
	jd->xt = xt_new( ic );
	jd->xt->handlers = (struct xt_handler_entry*) jabber_handlers;
	
	if( jd->r_inpa <= 0 )
		jd->r_inpa = b_input_add( jd->fd, GAIM_INPUT_READ, jabber_read_callback, ic );
	
	greet = g_strdup_printf( "<?xml version='1.0' ?>"
	                         "<stream:stream to=\"%s\" xmlns=\"jabber:client\" "
	                          "xmlns:stream=\"http://etherx.jabber.org/streams\" version=\"1.0\">", jd->server );
	
	st = jabber_write( ic, greet, strlen( greet ) );
	
	g_free( greet );
	
	return st;
}

void jabber_end_stream( struct im_connection *ic )
{
	struct jabber_data *jd = ic->proto_data;
	
	/* Let's only do this if the queue is currently empty, otherwise it'd
	   take too long anyway. */
	if( jd->tx_len == 0 )
	{
		char eos[] = "</stream:stream>";
		struct xt_node *node;
		int st = 1;
		
		if( ic->flags & OPT_LOGGED_IN )
		{
			node = jabber_make_packet( "presence", "unavailable", NULL, NULL );
			st = jabber_write_packet( ic, node );
			xt_free_node( node );
		}
		
		if( st )
			jabber_write( ic, eos, strlen( eos ) );
	}
}
