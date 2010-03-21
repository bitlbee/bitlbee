/********************************************************************\
* BitlBee -- An IRC to other IM-networks gateway                     *
*                                                                    *
* Copyright 2006 Marijn Kruisselbrink and others                     *
\********************************************************************/

/* MSN module - File transfer support             */

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

#ifndef _MSN_INVITATION_H
#define _MSN_INVITATION_H

#include "msn.h"

#define MSN_INVITE_HEADERS	"MIME-Version: 1.0\r\n" \
				"Content-Type: text/x-msmsgsinvite; charset=UTF-8\r\n" \
				"\r\n"

#define MSNFTP_PSIZE 2048

typedef enum {
	MSN_TRANSFER_RECEIVING	= 1,
	MSN_TRANSFER_SENDING	= 2
} msn_filetransfer_status_t;

typedef struct msn_filetransfer
{
/* Generic invitation data */	
	/* msn_data instance this invitation was received with. */
	struct msn_data *md;
	/* Cookie specifying this invitation. */
	unsigned int invite_cookie;
	/* Handle of user that started this invitation. */
	char *handle;

/* File transfer specific data */
	/* Current status of the file transfer. */
	msn_filetransfer_status_t status;
	/* Pointer to the dcc structure for this transfer. */
	file_transfer_t *dcc;
	/* Socket the transfer is taking place over. */
	int fd;
	/* Cookie received in the original invitation, this must be sent as soon as
	   a connection has been established. */
	unsigned int auth_cookie;
	/* Data remaining to be received in the current packet. */
	unsigned int data_remaining;
	/* Buffer containing received, but unprocessed text. */
	char tbuf[256];
	unsigned int tbufpos;
	
	unsigned int data_sent;

	gint r_event_id;
	gint w_event_id;
	
	unsigned char sbuf[2048];
	int sbufpos;

} msn_filetransfer_t;

void msn_invitation_invite( struct msn_switchboard *sb, char *handle, unsigned int cookie, char *body, int blen );
void msn_invitation_accept( struct msn_switchboard *sb, char *handle, unsigned int cookie, char *body, int blen );
void msn_invitation_cancel( struct msn_switchboard *sb, char *handle, unsigned int cookie, char *body, int blen );

#endif
