/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  libpurple module - File transfer stuff                                   *
*                                                                           *
*  Copyright 2009-2010 Wilmer van der Gaast <wilmer@gaast.net>              *
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

/* Do file transfers via disk for now, since libpurple was really designed
   for straight-to/from disk fts and is only just learning how to pass the
   file contents the the UI instead (2.6.0 and higher it seems, and with
   varying levels of success). */

#include "bitlbee.h"
#include "bpurple.h"

#include <stdarg.h>

#include <glib.h>
#include <purple.h>

struct prpl_xfer_data {
	PurpleXfer *xfer;
	file_transfer_t *ft;
	struct im_connection *ic;
	int fd;
	char *fn, *handle;
	gboolean ui_wants_data;
	int timeout;
};

static file_transfer_t *next_ft;

struct im_connection *purple_ic_by_pa(PurpleAccount *pa);
static gboolean prplcb_xfer_new_send_cb(gpointer data, gint fd, b_input_condition cond);
static gboolean prpl_xfer_write_request(struct file_transfer *ft);


/* Receiving files (IM->UI): */
static void prpl_xfer_accept(struct file_transfer *ft)
{
	struct prpl_xfer_data *px = ft->data;

	purple_xfer_request_accepted(px->xfer, NULL);
	prpl_xfer_write_request(ft);
}

static void prpl_xfer_canceled(struct file_transfer *ft, char *reason)
{
	struct prpl_xfer_data *px = ft->data;

	if (px->xfer) {
		if (!purple_xfer_is_completed(px->xfer) && !purple_xfer_is_canceled(px->xfer)) {
			purple_xfer_cancel_local(px->xfer);
		}
		px->xfer->ui_data = NULL;
		purple_xfer_unref(px->xfer);
		px->xfer = NULL;
	}
}

static void prpl_xfer_free(struct file_transfer *ft)
{
	struct prpl_xfer_data *px = ft->data;
	struct purple_data *pd = px->ic->proto_data;

	pd->filetransfers = g_slist_remove(pd->filetransfers, px);

	if (px->xfer) {
		px->xfer->ui_data = NULL;
		purple_xfer_unref(px->xfer);
	}

	if (px->timeout) {
		b_event_remove(px->timeout);
	}

	g_free(px->fn);
	g_free(px->handle);
	if (px->fd >= 0) {
		close(px->fd);
	}
	g_free(px);
}

static void prplcb_xfer_new(PurpleXfer *xfer)
{
	purple_xfer_ref(xfer);

	if (purple_xfer_get_type(xfer) == PURPLE_XFER_RECEIVE) {
		struct prpl_xfer_data *px = g_new0(struct prpl_xfer_data, 1);
		struct purple_data *pd;

		xfer->ui_data = px;
		px->xfer = xfer;
		px->fn = mktemp(g_strdup("/tmp/bitlbee-purple-ft.XXXXXX"));
		px->fd = -1;
		px->ic = purple_ic_by_pa(xfer->account);

		pd = px->ic->proto_data;
		pd->filetransfers = g_slist_prepend(pd->filetransfers, px);

		purple_xfer_set_local_filename(xfer, px->fn);

		/* Sadly the xfer struct is still empty ATM so come back after
		   the caller is done. */
		b_timeout_add(0, prplcb_xfer_new_send_cb, xfer);
	} else {
		struct file_transfer *ft = next_ft;
		struct prpl_xfer_data *px = ft->data;

		xfer->ui_data = px;
		px->xfer = xfer;

		next_ft = NULL;
	}
}

static gboolean prplcb_xfer_new_send_cb(gpointer data, gint fd, b_input_condition cond)
{
	PurpleXfer *xfer = data;
	struct im_connection *ic = purple_ic_by_pa(xfer->account);
	struct prpl_xfer_data *px = xfer->ui_data;
	PurpleBuddy *buddy;
	const char *who;

	buddy = purple_find_buddy(xfer->account, xfer->who);
	who = buddy ? purple_buddy_get_name(buddy) : xfer->who;

	/* TODO(wilmer): After spreading some more const goodness in BitlBee,
	   remove the evil cast below. */
	px->ft = imcb_file_send_start(ic, (char *) who, xfer->filename, xfer->size);

	if (!px->ft) {
		return FALSE;
	}
	px->ft->data = px;

	px->ft->accept = prpl_xfer_accept;
	px->ft->canceled = prpl_xfer_canceled;
	px->ft->free = prpl_xfer_free;
	px->ft->write_request = prpl_xfer_write_request;

	return FALSE;
}

gboolean try_write_to_ui(gpointer data, gint fd, b_input_condition cond)
{
	struct file_transfer *ft = data;
	struct prpl_xfer_data *px = ft->data;
	struct stat fs;
	off_t tx_bytes;

	/* If we don't have the file opened yet, there's no data so wait. */
	if (px->fd < 0 || !px->ui_wants_data) {
		return FALSE;
	}

	tx_bytes = lseek(px->fd, 0, SEEK_CUR);
	fstat(px->fd, &fs);

	if (fs.st_size > tx_bytes) {
		char buf[1024];
		size_t n = MIN(fs.st_size - tx_bytes, sizeof(buf));

		if (read(px->fd, buf, n) == n && ft->write(ft, buf, n)) {
			px->ui_wants_data = FALSE;
		} else {
			purple_xfer_cancel_local(px->xfer);
			imcb_file_canceled(px->ic, ft, "Read error");
		}
	}

	if (lseek(px->fd, 0, SEEK_CUR) == px->xfer->size) {
		/*purple_xfer_end( px->xfer );*/
		imcb_file_finished(px->ic, ft);
	}

	return FALSE;
}

/* UI calls this when its buffer is empty and wants more data to send to the user. */
static gboolean prpl_xfer_write_request(struct file_transfer *ft)
{
	struct prpl_xfer_data *px = ft->data;

	px->ui_wants_data = TRUE;
	try_write_to_ui(ft, 0, 0);

	return FALSE;
}


static void prplcb_xfer_destroy(PurpleXfer *xfer)
{
	struct prpl_xfer_data *px = xfer->ui_data;

	if (px) {
		px->xfer = NULL;
	}
}

static void prplcb_xfer_progress(PurpleXfer *xfer, double percent)
{
	struct prpl_xfer_data *px = xfer->ui_data;

	if (px == NULL) {
		return;
	}

	if (purple_xfer_get_type(xfer) == PURPLE_XFER_SEND) {
		if (*px->fn) {
			char *slash;

			unlink(px->fn);
			if ((slash = strrchr(px->fn, '/'))) {
				*slash = '\0';
				rmdir(px->fn);
			}
			*px->fn = '\0';
		}

		return;
	}

	if (px->fd == -1 && percent > 0) {
		/* Weeeeeeeee, we're getting data! That means the file exists
		   by now so open it and start sending to the UI. */
		px->fd = open(px->fn, O_RDONLY);

		/* Unlink it now, because we don't need it after this. */
		unlink(px->fn);
	}

	if (percent < 1) {
		try_write_to_ui(px->ft, 0, 0);
	} else {
		/* Another nice problem: If we have the whole file, it only
		   gets closed when we return. Problem: There may still be
		   stuff buffered and not written, we'll only see it after
		   the caller close()s the file. So poll the file after that. */
		b_timeout_add(0, try_write_to_ui, px->ft);
	}
}

static void prplcb_xfer_cancel_remote(PurpleXfer *xfer)
{
	struct prpl_xfer_data *px = xfer->ui_data;

	if (px && px->ft) {
		imcb_file_canceled(px->ic, px->ft, "Canceled by remote end");
	} else if (px) {
		/* px->ft == NULL for sends, because of the two stages. :-/ */
		imcb_error(px->ic, "File transfer cancelled by remote end");
	}
}


/* Sending files (UI->IM): */
static gboolean prpl_xfer_write(struct file_transfer *ft, char *buffer, unsigned int len);
static gboolean purple_transfer_request_cb(gpointer data, gint fd, b_input_condition cond);

void purple_transfer_request(struct im_connection *ic, file_transfer_t *ft, char *handle)
{
	struct prpl_xfer_data *px = g_new0(struct prpl_xfer_data, 1);
	struct purple_data *pd;
	char *dir, *basename;

	ft->data = px;
	px->ft = ft;
	px->ft->free = prpl_xfer_free;

	dir = g_strdup("/tmp/bitlbee-purple-ft.XXXXXX");
	if (!mkdtemp(dir)) {
		imcb_error(ic, "Could not create temporary file for file transfer");
		g_free(px);
		g_free(dir);
		return;
	}

	if ((basename = strrchr(ft->file_name, '/'))) {
		basename++;
	} else {
		basename = ft->file_name;
	}
	px->fn = g_strdup_printf("%s/%s", dir, basename);
	px->fd = open(px->fn, O_WRONLY | O_CREAT, 0600);
	g_free(dir);

	if (px->fd < 0) {
		imcb_error(ic, "Could not create temporary file for file transfer");
		g_free(px);
		g_free(px->fn);
		return;
	}

	px->ic = ic;
	px->handle = g_strdup(handle);

	pd = px->ic->proto_data;
	pd->filetransfers = g_slist_prepend(pd->filetransfers, px);

	imcb_log(ic,
	         "Due to libpurple limitations, the file has to be cached locally before proceeding with the actual file transfer. Please wait...");

	px->timeout = b_timeout_add(0, purple_transfer_request_cb, ft);
}

static void purple_transfer_forward(struct file_transfer *ft)
{
	struct prpl_xfer_data *px = ft->data;
	struct purple_data *pd = px->ic->proto_data;

	/* xfer_new() will pick up this variable. It's a hack but we're not
	   multi-threaded anyway. */
	next_ft = ft;
	serv_send_file(purple_account_get_connection(pd->account),
                   px->handle, px->fn);
}

static gboolean purple_transfer_request_cb(gpointer data, gint fd, b_input_condition cond)
{
	file_transfer_t *ft = data;
	struct prpl_xfer_data *px = ft->data;

	px->timeout = 0;

	if (ft->write == NULL) {
		ft->write = prpl_xfer_write;
		imcb_file_recv_start(px->ic, ft);
	}

	ft->write_request(ft);

	return FALSE;
}

static gboolean prpl_xfer_write(struct file_transfer *ft, char *buffer, unsigned int len)
{
	struct prpl_xfer_data *px = ft->data;

	if (write(px->fd, buffer, len) != len) {
		imcb_file_canceled(px->ic, ft, "Error while writing temporary file");
		return FALSE;
	}

	if (lseek(px->fd, 0, SEEK_CUR) >= ft->file_size) {
		close(px->fd);
		px->fd = -1;

		purple_transfer_forward(ft);
		imcb_file_finished(px->ic, ft);
		px->ft = NULL;
	} else {
		px->timeout = b_timeout_add(0, purple_transfer_request_cb, ft);
	}

	return TRUE;
}

void purple_transfer_cancel_all(struct im_connection *ic)
{
	struct purple_data *pd = ic->proto_data;

	while (pd->filetransfers) {
		struct prpl_xfer_data *px = pd->filetransfers->data;

		if (px->ft) {
			imcb_file_canceled(ic, px->ft, "Logging out");
		}

		pd->filetransfers = g_slist_remove(pd->filetransfers, px);
	}
}



PurpleXferUiOps bee_xfer_uiops =
{
	prplcb_xfer_new,           /* new_xfer */
	prplcb_xfer_destroy,       /* destroy */
	NULL,                      /* add_xfer */
	prplcb_xfer_progress,      /* update_progress */
	NULL,                      /* cancel_local */
	prplcb_xfer_cancel_remote, /* cancel_remote */
	NULL,                      /* ui_write */
	NULL,                      /* ui_read */
	NULL,                      /* data_not_sent */
};
