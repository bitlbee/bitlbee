/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Jabber module - SI packets                                               *
*                                                                           *
*  Copyright 2007 Uli Meis <a.sporto+bee@gmail.com>                         *
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
#include "sha1.h"

void jabber_si_answer_request( file_transfer_t *ft );

/* imcb callback */
void jabber_si_free_transfer( file_transfer_t *ft)
{
	struct jabber_transfer *tf = ft->data;
	struct jabber_data *jd = tf->ic->proto_data;

	if ( tf->watch_in )
		b_event_remove( tf->watch_in );

	jd->filetransfers = g_slist_remove( jd->filetransfers, tf );

	if( tf->fd )
	{
		close( tf->fd );
		tf->fd = 0;
	}

	g_free( tf->ini_jid );
	g_free( tf->tgt_jid );
	g_free( tf->iq_id );
	g_free( tf->sid );
}

/* imcb callback */
void jabber_si_finished( file_transfer_t *ft )
{
	struct jabber_transfer *tf = ft->data;

	imcb_log( tf->ic, "File %s transferred successfully!" , ft->file_name );
}

/* imcb callback */
void jabber_si_canceled( file_transfer_t *ft, char *reason )
{
	struct jabber_transfer *tf = ft->data;
	struct xt_node *reply, *iqnode;

	if( tf->accepted )
		return;
	
	iqnode = jabber_make_packet( "iq", "error", tf->ini_jid, NULL );
	xt_add_attr( iqnode, "id", tf->iq_id );
	reply = jabber_make_error_packet( iqnode, "forbidden", "cancel", "403" );
	xt_free_node( iqnode );
	
	if( !jabber_write_packet( tf->ic, reply ) )
		imcb_log( tf->ic, "WARNING: Error generating reply to file transfer request" );
	xt_free_node( reply );

}

/*
 * First function that gets called when a file transfer request comes in.
 * A lot to parse.
 *
 * We choose a stream type from the options given by the initiator.
 * Then we wait for imcb to call the accept or cancel callbacks.
 */
int jabber_si_handle_request( struct im_connection *ic, struct xt_node *node, struct xt_node *sinode)
{
	struct xt_node *c, *d, *reply;
	char *sid, *ini_jid, *tgt_jid, *iq_id, *s, *ext_jid;
	struct jabber_buddy *bud;
	int requestok = FALSE;
	char *name;
	size_t size;
	struct jabber_transfer *tf;
	struct jabber_data *jd = ic->proto_data;
	file_transfer_t *ft;
	
	/* All this means we expect something like this: ( I think )
	 * <iq from=... to=... id=...>
	 * 	<si id=id xmlns=si profile=ft>
	 * 		<file xmlns=ft/>
	 * 		<feature xmlns=feature>
	 * 			<x xmlns=xdata type=submit>
	 * 				<field var=stream-method>
	 *
	 */
	if( !( ini_jid 		= xt_find_attr(   node, "from" ) 			) ||
	    !( tgt_jid 		= xt_find_attr(   node, "to" ) 				) ||
	    !( iq_id 		= xt_find_attr(   node, "id" ) 				) ||
	    !( sid 		= xt_find_attr( sinode, "id" ) 				) ||
	    !( strcmp( xt_find_attr( sinode, "profile" ), XMLNS_FILETRANSFER ) == 0	) ||
	    !( d 		= xt_find_node( sinode->children, "file" ) 		) ||
	    !( strcmp( xt_find_attr( d, "xmlns" ), XMLNS_FILETRANSFER ) == 0 		) ||
	    !( name 		= xt_find_attr( d, "name" ) 				) ||
	    !( size 		= (size_t) atoll( xt_find_attr( d, "size" ) ) 		) ||
	    !( d 		= xt_find_node( sinode->children, "feature" ) 		) ||
	    !( strcmp( xt_find_attr( d, "xmlns" ), XMLNS_FEATURE ) == 0 		) ||
	    !( d 		= xt_find_node( d->children, "x" ) 			) ||
	    !( strcmp( xt_find_attr( d, "xmlns" ), XMLNS_XDATA ) == 0 			) ||
	    !( strcmp( xt_find_attr( d, "type" ), "form" ) == 0 			) ||
	    !( d 		= xt_find_node( d->children, "field" ) 			) ||
	    !( strcmp( xt_find_attr( d, "var" ), "stream-method" ) == 0 		) )
	{
		imcb_log( ic, "WARNING: Received incomplete Stream Initiation request" );
	} else
	{
		/* Check if we support one of the options */

		c = d->children;
		while( ( c = xt_find_node( c, "option" ) ) )
			if( 	( d = xt_find_node( c->children, "value" ) ) &&
				( strcmp( d->text, XMLNS_BYTESTREAMS ) == 0 ) )
			{
				requestok = TRUE;
				break;
			}
	}
	
	if ( requestok )
	{
		/* Figure out who the transfer should come frome... */

		if( ( s = strchr( ini_jid, '/' ) ) )
		{
			if( ( bud = jabber_buddy_by_jid( ic, ini_jid, GET_BUDDY_EXACT ) ) )
			{
				bud->last_act = time( NULL );
				ext_jid = bud->ext_jid ? : bud->bare_jid;
			}
			else
				*s = 0; /* We need to generate a bare JID now. */
		}

		if( !( ft = imcb_file_send_start( ic, ext_jid, name, size ) ) )
		{ 
			imcb_log( ic, "WARNING: Error handling transfer request from %s", ini_jid);
			requestok = FALSE;
		}

		*s = '/';
	} else 
		imcb_log( ic, "WARNING: Unsupported file transfer request from %s", ini_jid);

	if ( !requestok )
	{ 
		reply = jabber_make_error_packet( node, "item-not-found", "cancel", NULL );
		if (!jabber_write_packet( ic, reply ))
			imcb_log( ic, "WARNING: Error generating reply to file transfer request" );
		xt_free_node( reply );
		return XT_HANDLED;
	}

	/* Request is fine. */

	imcb_log( ic, "File transfer request from %s for %s (%zd kb). ", xt_find_attr( node, "from" ), name, size/1024 );

	imcb_log( ic, "Accept the DCC transfer if you'd like the file. If you don't, issue the 'transfers reject' command.");

	tf = g_new0( struct jabber_transfer, 1 );

	tf->ini_jid = g_strdup( ini_jid );
	tf->tgt_jid = g_strdup( tgt_jid );
	tf->iq_id = g_strdup( iq_id );
	tf->sid = g_strdup( sid );
	tf->ic = ic;
	tf->ft = ft;
	tf->ft->data = tf;
	tf->ft->accept = jabber_si_answer_request;
	tf->ft->free = jabber_si_free_transfer;
	tf->ft->finished = jabber_si_finished;
	tf->ft->canceled = jabber_si_canceled;

	jd->filetransfers = g_slist_prepend( jd->filetransfers, tf );

	return XT_HANDLED;
}

/*
 * imcb called the accept callback which probably means that the user accepted this file transfer.
 * We send our response to the initiator.
 * In the next step, the initiator will send us a request for the given stream type.
 * (currently that can only be a SOCKS5 bytestream)
 */
void jabber_si_answer_request( file_transfer_t *ft ) {
	struct jabber_transfer *tf = ft->data;
	struct xt_node *node, *sinode, *reply;

	/* generate response, start with the SI tag */
	sinode = xt_new_node( "si", NULL, NULL );
	xt_add_attr( sinode, "xmlns", XMLNS_SI );
	xt_add_attr( sinode, "profile", XMLNS_FILETRANSFER );
	xt_add_attr( sinode, "id", tf->sid );

	/* now the file tag */
	node = xt_new_node( "file", NULL, NULL );
	xt_add_attr( node, "xmlns", XMLNS_FILETRANSFER );

	xt_add_child( sinode, node );

	/* and finally the feature tag */
	node = xt_new_node( "field", NULL, NULL );
	xt_add_attr( node, "var", "stream-method" );
	xt_add_attr( node, "type", "list-single" );

	/* Currently all we can do. One could also implement in-band (IBB) */
	xt_add_child( node, xt_new_node( "value", XMLNS_BYTESTREAMS, NULL ) );

	node = xt_new_node( "x", NULL, node );
	xt_add_attr( node, "xmlns", XMLNS_XDATA );
	xt_add_attr( node, "type", "submit" );

	node = xt_new_node( "feature", NULL, node );
	xt_add_attr( node, "xmlns", XMLNS_FEATURE );

	xt_add_child( sinode, node );

	reply = jabber_make_packet( "iq", "result", tf->ini_jid, sinode );
	xt_add_attr( reply, "id", tf->iq_id );
	
	if( !jabber_write_packet( tf->ic, reply ) )
		imcb_log( tf->ic, "WARNING: Error generating reply to file transfer request" );
	else
		tf->accepted = TRUE;
	xt_free_node( reply );
}
