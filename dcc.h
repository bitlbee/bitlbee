/********************************************************************\
* BitlBee -- An IRC to other IM-networks gateway                     *
*                                                                    *
* Copyright 2006 Marijn Kruisselbrink and others                     *
* Copyright 2007 Uli Meis <a.sporto+bee@gmail.com>                   *
\********************************************************************/

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

/* 
 * DCC SEND
 *
 * Historically, DCC means send 1024 Bytes and wait for a 4 byte reply
 * acknowledging all transferred data. This is ridiculous for two reasons.  The
 * first being that TCP is a stream oriented protocol that doesn't care much
 * about your idea of a packet. The second reason being that TCP is a reliable
 * transfer protocol with its own sophisticated ACK mechanism, making DCCs ACK
 * mechanism look like a joke. For these reasons, DCCs requirements have
 * (hopefully) been relaxed in most implementations and this implementation
 * depends upon at least the following: The 1024 bytes need not be transferred
 * at once, i.e. packets can be smaller. A second relaxation has apparently
 * gotten the name "DCC SEND ahead" which basically means to not give a damn
 * about those DCC ACKs and just send data as you please. This behaviour is
 * enabled by default. Note that this also means that packets may be as large
 * as the maximum segment size.
 */ 

#ifndef _DCC_H
#define _DCC_H

/* don't wait for acknowledgments */
#define DCC_SEND_AHEAD

/* This multiplier specifies how many bytes we
 * can go ahead within one event loop cycle. Notice that all in all,
 * we can easily be more ahead if the event loop shoots often enough.
 * (or the receiver processes slow enough)
 *
 * Setting this value too high will cause send buffer overflows.
 */
#define DCC_SEND_AHEAD_MUL 10

/*
 * queue thresholds for the out of data and overflow conditions
 */
#define DCC_QUEUE_THRESHOLD_LOW 2048
#define DCC_QUEUE_THRESHOLD_HIGH 65536

/* only used in non-ahead mode */
#define DCC_PACKET_SIZE 1024

/* stores buffers handed over by IM protocols */
struct dcc_buffer {
	char *b;
	int len;
};

typedef struct dcc_file_transfer {

	struct im_connection *ic;

	/*
	 * Depending in the status of the file transfer, this is either the socket that is
	 * being listened on for connections, or the socket over which the file transfer is
	 * taking place.
	 */
	int fd;
	
	/*
	 * IDs returned by b_input_add for watch_ing over the above socket.
	 */
	gint watch_in;   /* readable */
	gint watch_out;  /* writable */
	
	/*
	 * The total number of queued bytes. The following equality should always hold:
	 *
	 * 	queued_bytes = sum(queued_buffers.len) - buffer_pos
	 */
	unsigned int queued_bytes;

	/* 
	 * A list of dcc_buffer structures.
	 * These are provided by the protocols directly so that no copying is neccessary.
	 */
	GSList *queued_buffers;
	
	/* 
	 * current position within the first buffer.
	 * Is non-null if the whole buffer couldn't be sent at once.
	 */
	int buffer_pos;

	/*
	 * The total amount of bytes that have been sent to the irc client.
	 */
	size_t bytes_sent;
	
	/* imc's handle */
	file_transfer_t *ft;

	/* if we're receiving, this is the sender's socket address */
	struct sockaddr_storage saddr;

} dcc_file_transfer_t;

file_transfer_t *dccs_send_start( struct im_connection *ic, char *user_nick, char *file_name, size_t file_size );

void dcc_canceled( file_transfer_t *file, char *reason );

gboolean dccs_send_write( file_transfer_t *file, gpointer data, unsigned int data_size );

file_transfer_t *dcc_request( struct im_connection *ic, char *line );
#endif
