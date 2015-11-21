/********************************************************************\
* BitlBee -- An IRC to other IM-networks gateway                     *
*                                                                    *
* Copyright 2006 Marijn Kruisselbrink and others                     *
\********************************************************************/

/* Generic file transfer header                                     */

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
  if not, write to the Free Software Foundation, Inc., 51 Franklin St.,
  Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef _FT_H
#define _FT_H

/*
 * One buffer is needed for each transfer. The receiver stores a message
 * in it and gives it to the sender. The sender will stall the receiver
 * till the buffer has been sent out.
 */
#define FT_BUFFER_SIZE 2048

typedef enum {
	FT_STATUS_LISTENING     = 1,
	FT_STATUS_TRANSFERRING  = 2,
	FT_STATUS_FINISHED      = 4,
	FT_STATUS_CANCELED      = 8,
	FT_STATUS_CONNECTING    = 16
} file_status_t;

/*
 * This structure holds all irc specific information regarding an incoming (from the point of view of
 * the irc client) file transfer. New instances of this struct should only be created by calling the
 * imcb_file_send_start() method, which will initialize most of the fields. The data field and the various
 * methods are zero-initialized. Instances will automatically be deleted once the transfer is completed,
 * canceled, or the connection to the irc client has been lost (note that also if only the irc connection
 * and not the file transfer connection is lost, the file transfer will still be canceled and freed).
 *
 * The following (poor ascii-art) diagram illustrates what methods are called for which status-changes:
 *
 *	                        /-----------\                    /----------\
 *	               -------> | LISTENING | -----------------> | CANCELED |
 *	                        \-----------/  [canceled,]free   \----------/
 *	                              |
 *	                              | accept
 *	                              V
 *	               /------ /-------------\                    /--------------------------\
 *	   out_of_data |       | TRANSFERRING | -----------------> | TRANSFERRING | CANCELED |
 *	               \-----> \-------------/  [canceled,]free   \--------------------------/
 *	                              |
 *	                              | finished,free
 *	                              V
 *	                 /-------------------------\
 *	                 | TRANSFERRING | FINISHED |
 *	                 \-------------------------/
 */
typedef struct file_transfer {

	/* Are we sending something? */
	int sending;

	/*
	 * The current status of this file transfer.
	 */
	file_status_t status;

	/*
	 * file size
	 */
	size_t file_size;

	/*
	 * Number of bytes that have been successfully transferred.
	 */
	size_t bytes_transferred;

	/*
	 * Time started. Used to calculate kb/s.
	 */
	time_t started;

	/*
	 * file name
	 */
	char *file_name;

	/*
	 * A unique local ID for this file transfer.
	 */
	unsigned int local_id;

	/*
	 * IM-protocol specific data associated with this file transfer.
	 */
	gpointer data;
	struct im_connection *ic;

	/*
	 * Private data.
	 */
	gpointer priv;

	/*
	 * If set, called after successful connection setup.
	 */
	void (*accept)(struct file_transfer *file);

	/*
	 * If set, called when the transfer is canceled or finished.
	 * Subsequently, this structure will be freed.
	 *
	 */
	void (*free)(struct file_transfer *file);

	/*
	 * If set, called when the transfer is finished and successful.
	 */
	void (*finished)(struct file_transfer *file);

	/*
	 * If set, called when the transfer is canceled.
	 * ( canceled either by the transfer implementation or by
	 *  a call to imcb_file_canceled )
	 */
	void (*canceled)(struct file_transfer *file, char *reason);

	/*
	 * called by the sending side to indicate that it is writable.
	 * The callee should check if data is available and call the
	 * function(as seen below) if that is the case.
	 */
	gboolean (*write_request)(struct file_transfer *file);

	/*
	 * When sending files, protocols register this function to receive data.
	 * This should only be called once after write_request is called. The caller
	 * should not read more data until write_request is called again. This technique
	 * avoids buffering.
	 */
	gboolean (*write)(struct file_transfer *file, char *buffer, unsigned int len);

	/* The send buffer associated with this transfer.
	 * Since receivers always wait for a write_request call one is enough.
	 */
	char buffer[FT_BUFFER_SIZE];

} file_transfer_t;

/*
 * This starts a file transfer from bitlbee to the user.
 */
file_transfer_t *imcb_file_send_start(struct im_connection *ic, char *user_nick, char *file_name, size_t file_size);

/*
 * This should be called by a protocol when the transfer is canceled. Note that
 * the canceled() and free() callbacks given in file will be called by this function.
 */
void imcb_file_canceled(struct im_connection *ic, file_transfer_t *file, char *reason);

gboolean imcb_file_recv_start(struct im_connection *ic, file_transfer_t *ft);

void imcb_file_finished(struct im_connection *ic, file_transfer_t *file);
#endif
