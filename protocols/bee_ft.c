/********************************************************************\
* BitlBee -- An IRC to other IM-networks gateway                     *
*                                                                    *
* Copyright 2010 Wilmer van der Gaast <wilmer@gaast.net>             *
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
  if not, write to the Free Software Foundation, Inc., 51 Franklin St.,
  Fifth Floor, Boston, MA  02110-1301  USA
*/

#define BITLBEE_CORE
#include "bitlbee.h"
#include "ft.h"

file_transfer_t *imcb_file_send_start(struct im_connection *ic, char *handle, char *file_name, size_t file_size)
{
	bee_t *bee = ic->bee;
	bee_user_t *bu = bee_user_by_handle(bee, ic, handle);

	if (bee->ui->ft_in_start) {
		return bee->ui->ft_in_start(bee, bu, file_name, file_size);
	} else {
		return NULL;
	}
}

gboolean imcb_file_recv_start(struct im_connection *ic, file_transfer_t *ft)
{
	bee_t *bee = ic->bee;

	if (bee->ui->ft_out_start) {
		return bee->ui->ft_out_start(ic, ft);
	} else {
		return FALSE;
	}
}

void imcb_file_canceled(struct im_connection *ic, file_transfer_t *file, char *reason)
{
	bee_t *bee = ic->bee;

	if (file->canceled) {
		file->canceled(file, reason);
	}

	if (bee->ui->ft_close) {
		bee->ui->ft_close(ic, file);
	}
}

void imcb_file_finished(struct im_connection *ic, file_transfer_t *file)
{
	bee_t *bee = ic->bee;

	if (bee->ui->ft_finished) {
		bee->ui->ft_finished(ic, file);
	}
}
