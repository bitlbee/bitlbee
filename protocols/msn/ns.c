  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2010 Wilmer van der Gaast and others                *
  \********************************************************************/

/* MSN module - Notification server callbacks                           */

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

#include <ctype.h>
#include "nogaim.h"
#include "msn.h"
#include "md5.h"
#include "soap.h"
#include "xmltree.h"

static gboolean msn_ns_callback( gpointer data, gint source, b_input_condition cond );
static int msn_ns_command( gpointer data, char **cmd, int num_parts );
static int msn_ns_message( gpointer data, char *msg, int msglen, char **cmd, int num_parts );

static void msn_ns_send_adl_start( struct im_connection *ic );
static void msn_ns_send_adl( struct im_connection *ic );

gboolean msn_ns_connected( gpointer data, gint source, b_input_condition cond )
{
	struct im_connection *ic = data;
	struct msn_data *md;
	char s[1024];
	
	if( !g_slist_find( msn_connections, ic ) )
		return FALSE;
	
	if( source == -1 )
	{
		imcb_error( ic, "Could not connect to server" );
		imc_logout( ic, TRUE );
		return FALSE;
	}
	
	md = ic->proto_data;
	
	if( !md->handler )
	{
		md->handler = g_new0( struct msn_handler_data, 1 );
		md->handler->data = ic;
		md->handler->exec_command = msn_ns_command;
		md->handler->exec_message = msn_ns_message;
	}
	else
	{
		if( md->handler->rxq )
			g_free( md->handler->rxq );
		
		md->handler->rxlen = 0;
	}
	
	md->handler->fd = md->fd;
	md->handler->rxq = g_new0( char, 1 );
	
	g_snprintf( s, sizeof( s ), "VER %d %s CVR0\r\n", ++md->trId, MSNP_VER );
	if( msn_write( ic, s, strlen( s ) ) )
	{
		ic->inpa = b_input_add( md->fd, B_EV_IO_READ, msn_ns_callback, ic );
		imcb_log( ic, "Connected to server, waiting for reply" );
	}
	
	return FALSE;
}

static gboolean msn_ns_callback( gpointer data, gint source, b_input_condition cond )
{
	struct im_connection *ic = data;
	struct msn_data *md = ic->proto_data;
	
	if( msn_handler( md->handler ) == -1 ) /* Don't do this on ret == 0, it's already done then. */
	{
		imcb_error( ic, "Error while reading from server" );
		imc_logout( ic, TRUE );
		
		return FALSE;
	}
	else
		return TRUE;
}

static int msn_ns_command( gpointer data, char **cmd, int num_parts )
{
	struct im_connection *ic = data;
	struct msn_data *md = ic->proto_data;
	char buf[1024];
	
	if( num_parts == 0 )
	{
		/* Hrrm... Empty command...? Ignore? */
		return( 1 );
	}
	
	if( strcmp( cmd[0], "VER" ) == 0 )
	{
		if( cmd[2] && strncmp( cmd[2], MSNP_VER, 5 ) != 0 )
		{
			imcb_error( ic, "Unsupported protocol" );
			imc_logout( ic, FALSE );
			return( 0 );
		}
		
		g_snprintf( buf, sizeof( buf ), "CVR %d 0x0409 mac 10.2.0 ppc macmsgs 3.5.1 macmsgs %s\r\n",
		                                ++md->trId, ic->acc->user );
		return( msn_write( ic, buf, strlen( buf ) ) );
	}
	else if( strcmp( cmd[0], "CVR" ) == 0 )
	{
		/* We don't give a damn about the information we just received */
		g_snprintf( buf, sizeof( buf ), "USR %d SSO I %s\r\n", ++md->trId, ic->acc->user );
		return( msn_write( ic, buf, strlen( buf ) ) );
	}
	else if( strcmp( cmd[0], "XFR" ) == 0 )
	{
		char *server;
		int port;
		
		if( num_parts >= 6 && strcmp( cmd[2], "NS" ) == 0 )
		{
			b_event_remove( ic->inpa );
			ic->inpa = 0;
			closesocket( md->fd );
			
			server = strchr( cmd[3], ':' );
			if( !server )
			{
				imcb_error( ic, "Syntax error" );
				imc_logout( ic, TRUE );
				return( 0 );
			}
			*server = 0;
			port = atoi( server + 1 );
			server = cmd[3];
			
			imcb_log( ic, "Transferring to other server" );
			
			md->fd = proxy_connect( server, port, msn_ns_connected, ic );
		}
		else if( num_parts >= 6 && strcmp( cmd[2], "SB" ) == 0 )
		{
			struct msn_switchboard *sb;
			
			server = strchr( cmd[3], ':' );
			if( !server )
			{
				imcb_error( ic, "Syntax error" );
				imc_logout( ic, TRUE );
				return( 0 );
			}
			*server = 0;
			port = atoi( server + 1 );
			server = cmd[3];
			
			if( strcmp( cmd[4], "CKI" ) != 0 )
			{
				imcb_error( ic, "Unknown authentication method for switchboard" );
				imc_logout( ic, TRUE );
				return( 0 );
			}
			
			debug( "Connecting to a new switchboard with key %s", cmd[5] );

			if( ( sb = msn_sb_create( ic, server, port, cmd[5], MSN_SB_NEW ) ) == NULL )
			{
				/* Although this isn't strictly fatal for the NS connection, it's
				   definitely something serious (we ran out of file descriptors?). */
				imcb_error( ic, "Could not create new switchboard" );
				imc_logout( ic, TRUE );
				return( 0 );
			}
			
			if( md->msgq )
			{
				struct msn_message *m = md->msgq->data;
				GSList *l;
				
				sb->who = g_strdup( m->who );
				
				/* Move all the messages to the first user in the message
				   queue to the switchboard message queue. */
				l = md->msgq;
				while( l )
				{
					m = l->data;
					l = l->next;
					if( strcmp( m->who, sb->who ) == 0 )
					{
						sb->msgq = g_slist_append( sb->msgq, m );
						md->msgq = g_slist_remove( md->msgq, m );
					}
				}
			}
		}
		else
		{
			imcb_error( ic, "Syntax error" );
			imc_logout( ic, TRUE );
			return( 0 );
		}
	}
	else if( strcmp( cmd[0], "USR" ) == 0 )
	{
		if( num_parts >= 6 && strcmp( cmd[2], "SSO" ) == 0 &&
		    strcmp( cmd[3], "S" ) == 0 )
		{
			msn_soap_passport_sso_request( ic, cmd[4], cmd[5] );
		}
		else if( strcmp( cmd[2], "OK" ) == 0 )
		{
			imcb_log( ic, "Authenticated, getting buddy list" );
			msn_soap_memlist_request( ic );
		}
		else
		{
			imcb_error( ic, "Unknown authentication type" );
			imc_logout( ic, FALSE );
			return( 0 );
		}
	}
	else if( strcmp( cmd[0], "MSG" ) == 0 )
	{
		if( num_parts < 4 )
		{
			imcb_error( ic, "Syntax error" );
			imc_logout( ic, TRUE );
			return( 0 );
		}
		
		md->handler->msglen = atoi( cmd[3] );
		
		if( md->handler->msglen <= 0 )
		{
			imcb_error( ic, "Syntax error" );
			imc_logout( ic, TRUE );
			return( 0 );
		}
	}
	else if( strcmp( cmd[0], "BLP" ) == 0 )
	{
		msn_ns_send_adl_start( ic );
	}
	else if( strcmp( cmd[0], "ADL" ) == 0 )
	{
		if( num_parts >= 3 && strcmp( cmd[2], "OK" ) == 0 )
		{
			msn_ns_send_adl( ic );
			
			if( md->adl_todo < 0 && !( ic->flags & OPT_LOGGED_IN ) )
			{
				char buf[1024];
				char *fn_raw;
				char *fn;
				
				if( ( fn_raw = set_getstr( &ic->acc->set, "display_name" ) ) == NULL )
					fn_raw = ic->acc->user;
				fn = g_malloc( strlen( fn_raw ) * 3 + 1 );
				strcpy( fn, fn_raw );
				http_encode( fn );
				
				g_snprintf( buf, sizeof( buf ), "PRP %d MFN %s\r\n",
				            ++md->trId, fn );
				g_free( fn );
				
				msn_write( ic, buf, strlen( buf ) );
			}
		}
		else if( num_parts >= 3 )
		{
			md->handler->msglen = atoi( cmd[2] );
		}
	}
	else if( strcmp( cmd[0], "PRP" ) == 0 )
	{
		imcb_connected( ic );
	}
	else if( strcmp( cmd[0], "CHL" ) == 0 )
	{
		char *resp;
		
		if( num_parts < 3 )
		{
			imcb_error( ic, "Syntax error" );
			imc_logout( ic, TRUE );
			return( 0 );
		}
		
		resp = msn_p11_challenge( cmd[2] );
		g_snprintf( buf, sizeof( buf ), "QRY %d %s %zd\r\n%s",
		            ++md->trId, MSNP11_PROD_ID,
		            strlen( resp ), resp );
		g_free( resp );
		
		return( msn_write( ic, buf, strlen( buf ) ) );
	}
	else if( strcmp( cmd[0], "ILN" ) == 0 )
	{
		const struct msn_away_state *st;
		
		if( num_parts < 6 )
		{
			imcb_error( ic, "Syntax error" );
			imc_logout( ic, TRUE );
			return( 0 );
		}
		
		http_decode( cmd[5] );
		imcb_rename_buddy( ic, cmd[3], cmd[5] );
		
		st = msn_away_state_by_code( cmd[2] );
		if( !st )
		{
			/* FIXME: Warn/Bomb about unknown away state? */
			st = msn_away_state_list + 1;
		}
		
		imcb_buddy_status( ic, cmd[3], OPT_LOGGED_IN | 
		                   ( st != msn_away_state_list ? OPT_AWAY : 0 ),
		                   st->name, NULL );
	}
	else if( strcmp( cmd[0], "FLN" ) == 0 )
	{
		if( cmd[1] == NULL )
			return 1;
		
		imcb_buddy_status( ic, cmd[1], 0, NULL, NULL );
		
		msn_sb_start_keepalives( msn_sb_by_handle( ic, cmd[1] ), TRUE );
	}
	else if( strcmp( cmd[0], "NLN" ) == 0 )
	{
		const struct msn_away_state *st;
		
		if( num_parts < 5 )
		{
			imcb_error( ic, "Syntax error" );
			imc_logout( ic, TRUE );
			return( 0 );
		}
		
		http_decode( cmd[4] );
		imcb_rename_buddy( ic, cmd[2], cmd[4] );
		
		st = msn_away_state_by_code( cmd[1] );
		if( !st )
		{
			/* FIXME: Warn/Bomb about unknown away state? */
			st = msn_away_state_list + 1;
		}
		
		imcb_buddy_status( ic, cmd[2], OPT_LOGGED_IN | 
		                   ( st != msn_away_state_list ? OPT_AWAY : 0 ),
		                   st->name, NULL );
		
		msn_sb_stop_keepalives( msn_sb_by_handle( ic, cmd[2] ) );
	}
	else if( strcmp( cmd[0], "RNG" ) == 0 )
	{
		struct msn_switchboard *sb;
		char *server;
		int session, port;
		
		if( num_parts < 7 )
		{
			imcb_error( ic, "Syntax error" );
			imc_logout( ic, TRUE );
			return( 0 );
		}
		
		session = atoi( cmd[1] );
		
		server = strchr( cmd[2], ':' );
		if( !server )
		{
			imcb_error( ic, "Syntax error" );
			imc_logout( ic, TRUE );
			return( 0 );
		}
		*server = 0;
		port = atoi( server + 1 );
		server = cmd[2];
		
		if( strcmp( cmd[3], "CKI" ) != 0 )
		{
			imcb_error( ic, "Unknown authentication method for switchboard" );
			imc_logout( ic, TRUE );
			return( 0 );
		}
		
		debug( "Got a call from %s (session %d). Key = %s", cmd[5], session, cmd[4] );
		
		if( ( sb = msn_sb_create( ic, server, port, cmd[4], session ) ) == NULL )
		{
			/* Although this isn't strictly fatal for the NS connection, it's
			   definitely something serious (we ran out of file descriptors?). */
			imcb_error( ic, "Could not create new switchboard" );
			imc_logout( ic, TRUE );
			return( 0 );
		}
		else
		{
			sb->who = g_strdup( cmd[5] );
		}
	}
	else if( strcmp( cmd[0], "OUT" ) == 0 )
	{
		int allow_reconnect = TRUE;
		
		if( cmd[1] && strcmp( cmd[1], "OTH" ) == 0 )
		{
			imcb_error( ic, "Someone else logged in with your account" );
			allow_reconnect = FALSE;
		}
		else if( cmd[1] && strcmp( cmd[1], "SSD" ) == 0 )
		{
			imcb_error( ic, "Terminating session because of server shutdown" );
		}
		else
		{
			imcb_error( ic, "Session terminated by remote server (reason unknown)" );
		}
		
		imc_logout( ic, allow_reconnect );
		return( 0 );
	}
	else if( strcmp( cmd[0], "IPG" ) == 0 )
	{
		imcb_error( ic, "Received IPG command, we don't handle them yet." );
		
		md->handler->msglen = atoi( cmd[1] );
		
		if( md->handler->msglen <= 0 )
		{
			imcb_error( ic, "Syntax error" );
			imc_logout( ic, TRUE );
			return( 0 );
		}
	}
#if 0
	else if( strcmp( cmd[0], "ADG" ) == 0 )
	{
		char *group = g_strdup( cmd[3] );
		int groupnum, i;
		GSList *l, *next;
		
		http_decode( group );
		if( sscanf( cmd[4], "%d", &groupnum ) == 1 )
		{
			if( groupnum >= md->groupcount )
			{
				md->grouplist = g_renew( char *, md->grouplist, groupnum + 1 );
				for( i = md->groupcount; i <= groupnum; i ++ )
					md->grouplist[i] = NULL;
				md->groupcount = groupnum + 1;
			}
			g_free( md->grouplist[groupnum] );
			md->grouplist[groupnum] = group;
		}
		else
		{
			/* Shouldn't happen, but if it does, give up on the group. */
			g_free( group );
			imcb_error( ic, "Syntax error" );
			imc_logout( ic, TRUE );
			return 0;
		}
		
		for( l = md->grpq; l; l = next )
		{
			struct msn_groupadd *ga = l->data;
			next = l->next;
			if( g_strcasecmp( ga->group, group ) == 0 )
			{
				if( !msn_buddy_list_add( ic, "FL", ga->who, ga->who, group ) )
					return 0;
				
				g_free( ga->group );
				g_free( ga->who );
				g_free( ga );
				md->grpq = g_slist_remove( md->grpq, ga );
			}
		}
	}
#endif
	else if( strcmp( cmd[0], "GCF" ) == 0 )
	{
		/* Coming up is cmd[2] bytes of stuff we're supposed to
		   censore. Meh. */
		md->handler->msglen = atoi( cmd[2] );
	}
	else if( strcmp( cmd[0], "UBX" ) == 0 )
	{
		/* Status message. */
		if( num_parts >= 4 )
			md->handler->msglen = atoi( cmd[3] );
	}
	else if( strcmp( cmd[0], "NOT" ) == 0 )
	{
		/* Some kind of notification, poorly documented but
		   apparently used to announce address book changes. */
		if( num_parts >= 2 )
			md->handler->msglen = atoi( cmd[1] );
	}
	else if( isdigit( cmd[0][0] ) )
	{
		int num = atoi( cmd[0] );
		const struct msn_status_code *err = msn_status_by_number( num );
		
		imcb_error( ic, "Error reported by MSN server: %s", err->text );
		
		if( err->flags & STATUS_FATAL )
		{
			imc_logout( ic, TRUE );
			return( 0 );
		}
	}
	else
	{
		/* debug( "Received unknown command from main server: %s", cmd[0] ); */
	}
	
	return( 1 );
}

static int msn_ns_message( gpointer data, char *msg, int msglen, char **cmd, int num_parts )
{
	struct im_connection *ic = data;
	char *body;
	int blen = 0;
	
	if( !num_parts )
		return( 1 );
	
	if( ( body = strstr( msg, "\r\n\r\n" ) ) )
	{
		body += 4;
		blen = msglen - ( body - msg );
	}
	
	if( strcmp( cmd[0], "MSG" ) == 0 )
	{
		if( g_strcasecmp( cmd[1], "Hotmail" ) == 0 )
		{
			char *ct = msn_findheader( msg, "Content-Type:", msglen );
			
			if( !ct )
				return( 1 );
			
			if( g_strncasecmp( ct, "application/x-msmsgssystemmessage", 33 ) == 0 )
			{
				char *mtype;
				char *arg1;
				
				if( !body )
					return( 1 );
				
				mtype = msn_findheader( body, "Type:", blen );
				arg1 = msn_findheader( body, "Arg1:", blen );
				
				if( mtype && strcmp( mtype, "1" ) == 0 )
				{
					if( arg1 )
						imcb_log( ic, "The server is going down for maintenance in %s minutes.", arg1 );
				}
				
				g_free( arg1 );
				g_free( mtype );
			}
			else if( g_strncasecmp( ct, "text/x-msmsgsprofile", 20 ) == 0 )
			{
				/* We don't care about this profile for now... */
			}
			else if( g_strncasecmp( ct, "text/x-msmsgsinitialemailnotification", 37 ) == 0 )
			{
				if( set_getbool( &ic->acc->set, "mail_notifications" ) )
				{
					char *inbox = msn_findheader( body, "Inbox-Unread:", blen );
					char *folders = msn_findheader( body, "Folders-Unread:", blen );

					if( inbox && folders )
						imcb_log( ic, "INBOX contains %s new messages, plus %s messages in other folders.", inbox, folders );
					
					g_free( inbox );
					g_free( folders );
				}
			}
			else if( g_strncasecmp( ct, "text/x-msmsgsemailnotification", 30 ) == 0 )
			{
				if( set_getbool( &ic->acc->set, "mail_notifications" ) )
				{
					char *from = msn_findheader( body, "From-Addr:", blen );
					char *fromname = msn_findheader( body, "From:", blen );
					
					if( from && fromname )
						imcb_log( ic, "Received an e-mail message from %s <%s>.", fromname, from );

					g_free( from );
					g_free( fromname );
				}
			}
			else if( g_strncasecmp( ct, "text/x-msmsgsactivemailnotification", 35 ) == 0 )
			{
				/* Sorry, but this one really is *USELESS* */
			}
			else
			{
				debug( "Can't handle %s packet from notification server", ct );
			}
			
			g_free( ct );
		}
	}
	else if( strcmp( cmd[0], "UBX" ) == 0 )
	{
		struct xt_node *psm;
		char *psm_text = NULL;
		
		psm = xt_from_string( msg );
		if( psm && strcmp( psm->name, "Data" ) == 0 &&
		    ( psm = xt_find_node( psm->children, "PSM" ) ) )
			psm_text = psm->text;
		
		imcb_buddy_status_msg( ic, cmd[1], psm_text );
		xt_free_node( psm );
	}
	else if( strcmp( cmd[0], "ADL" ) == 0 )
	{
		struct xt_node *adl, *d, *c;
		
		if( !( adl = xt_from_string( msg ) ) )
			return 1;
		
		for( d = adl->children; d; d = d->next )
		{
			char *dn;
			if( strcmp( d->name, "d" ) != 0 ||
			    ( dn = xt_find_attr( d, "n" ) ) == NULL )
				continue;
			for( c = d->children; c; c = c->next )
			{
				bee_user_t *bu;
				struct msn_buddy_data *bd;
				char *cn, *handle, *f, *l;
				int flags;
				
				if( strcmp( c->name, "c" ) != 0 ||
				    ( l = xt_find_attr( c, "l" ) ) == NULL ||
				    ( cn = xt_find_attr( c, "n" ) ) == NULL )
					continue;
				
				handle = g_strdup_printf( "%s@%s", cn, dn );
				if( !( ( bu = bee_user_by_handle( ic->bee, ic, handle ) ) ||
				       ( bu = bee_user_new( ic->bee, ic, handle, 0 ) ) ) )
				{
					g_free( handle );
					continue;
				}
				g_free( handle );
				bd = bu->data;
				
				if( ( f = xt_find_attr( c, "f" ) ) )
				{
					http_decode( f );
					imcb_rename_buddy( ic, bu->handle, f );
				}
				
				flags = atoi( l ) & 15;
				if( bd->flags != flags )
				{
					bd->flags = flags;
					msn_buddy_ask( bu );
				}
			}
		}
	}
	
	return( 1 );
}

void msn_auth_got_passport_token( struct im_connection *ic, char *token )
{
	struct msn_data *md;
	
	/* Dead connection? */
	if( g_slist_find( msn_connections, ic ) == NULL )
		return;
	
	md = ic->proto_data;
	
	{
		char buf[1536];
		
		g_snprintf( buf, sizeof( buf ), "USR %d SSO S %s %s\r\n", ++md->trId, md->tokens[0], token );
		msn_write( ic, buf, strlen( buf ) );
	}
}

void msn_auth_got_contact_list( struct im_connection *ic )
{
	char buf[64];
	struct msn_data *md;
	
	/* Dead connection? */
	if( g_slist_find( msn_connections, ic ) == NULL )
		return;
	
	md = ic->proto_data;
	
	
	g_snprintf( buf, sizeof( buf ), "BLP %d %s\r\n", ++md->trId, "BL" );
	msn_write( ic, buf, strlen( buf ) );
}

static gboolean msn_ns_send_adl_1( gpointer key, gpointer value, gpointer data )
{
	struct xt_node *adl = data, *d, *c;
	struct bee_user *bu = value;
	struct msn_buddy_data *bd = bu->data;
	struct msn_data *md = bu->ic->proto_data;
	char handle[strlen(bu->handle)];
	char *domain;
	char l[4];
	
	if( ( bd->flags & 7 ) == 0 || ( bd->flags & MSN_BUDDY_ADL_SYNCED ) )
		return FALSE;
	
	strcpy( handle, bu->handle );
	if( ( domain = strchr( handle, '@' ) ) == NULL ) /* WTF */
		return FALSE; 
	*domain = '\0';
	domain ++;
	
	if( ( d = adl->children ) == NULL ||
	    g_strcasecmp( xt_find_attr( d, "n" ), domain ) != 0 )
	{
		d = xt_new_node( "d", NULL, NULL );
		xt_add_attr( d, "n", domain );
		xt_insert_child( adl, d );
	}
	
	g_snprintf( l, sizeof( l ), "%d", bd->flags & 7 );
	c = xt_new_node( "c", NULL, NULL );
	xt_add_attr( c, "n", handle );
	xt_add_attr( c, "l", l );
	xt_add_attr( c, "t", "1" ); /* 1 means normal, 4 means mobile? */
	xt_insert_child( d, c );
	
	/* Do this in batches of 100. */
	bd->flags |= MSN_BUDDY_ADL_SYNCED;
	return (--md->adl_todo % 140) == 0;
}

static void msn_ns_send_adl( struct im_connection *ic )
{
	struct xt_node *adl;
	struct msn_data *md = ic->proto_data;
	char *adls, buf[64];
	
	adl = xt_new_node( "ml", NULL, NULL );
	xt_add_attr( adl, "l", "1" );
	g_tree_foreach( md->domaintree, msn_ns_send_adl_1, adl );
	if( adl->children == NULL )
	{
		/* This tells the caller that we're done now. */
		md->adl_todo = -1;
		xt_free_node( adl );
		return;
	}
	adls = xt_to_string( adl );
	
	g_snprintf( buf, sizeof( buf ), "ADL %d %zd\r\n", ++md->trId, strlen( adls ) );
	if( msn_write( ic, buf, strlen( buf ) ) )
		msn_write( ic, adls, strlen( adls ) );
	
	g_free( adls );
}

static void msn_ns_send_adl_start( struct im_connection *ic )
{
	struct msn_data *md;
	GSList *l;
	
	/* Dead connection? */
	if( g_slist_find( msn_connections, ic ) == NULL )
		return;
	
	md = ic->proto_data;
	md->adl_todo = 0;
	for( l = ic->bee->users; l; l = l->next )
	{
		bee_user_t *bu = l->data;
		struct msn_buddy_data *bd = bu->data;
		
		if( bu->ic != ic || ( bd->flags & 7 ) == 0 )
			continue;
		
		bd->flags &= ~MSN_BUDDY_ADL_SYNCED;
		md->adl_todo++;
	}
	
	msn_ns_send_adl( ic );
}
