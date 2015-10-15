/********************************************************************\
* BitlBee -- An IRC to other IM-networks gateway                     *
*                                                                    *
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
  if not, write to the Free Software Foundation, Inc., 51 Franklin St.,
  Fifth Floor, Boston, MA  02110-1301  USA
*/

#define BITLBEE_CORE
#include "bitlbee.h"
#include "ft.h"
#include "dcc.h"
#include <netinet/tcp.h>
#include <regex.h>
#include "lib/ftutil.h"

/*
 * Since that might be confusing a note on naming:
 *
 * Generic dcc functions start with
 *
 *      dcc_
 *
 * ,methods specific to DCC SEND start with
 *
 *      dccs_
 *
 * . Since we can be on both ends of a DCC SEND,
 * functions specific to one end are called
 *
 *      dccs_send and dccs_recv
 *
 * ,respectively.
 */


/*
 * used to generate a unique local transfer id the user
 * can use to reject/cancel transfers
 */
unsigned int local_transfer_id = 1;

/*
 * just for debugging the nr. of chunks we received from im-protocols and the total data
 */
unsigned int receivedchunks = 0, receiveddata = 0;

void dcc_finish(file_transfer_t *file);
void dcc_close(file_transfer_t *file);
gboolean dccs_send_proto(gpointer data, gint fd, b_input_condition cond);
int dccs_send_request(struct dcc_file_transfer *df, irc_user_t *iu, struct sockaddr_storage *saddr);
gboolean dccs_recv_proto(gpointer data, gint fd, b_input_condition cond);
gboolean dccs_recv_write_request(file_transfer_t *ft);
gboolean dcc_progress(gpointer data, gint fd, b_input_condition cond);
gboolean dcc_abort(dcc_file_transfer_t *df, char *reason, ...);

dcc_file_transfer_t *dcc_alloc_transfer(const char *file_name, size_t file_size, struct im_connection *ic)
{
	file_transfer_t *file = g_new0(file_transfer_t, 1);
	dcc_file_transfer_t *df = file->priv = g_new0(dcc_file_transfer_t, 1);

	file->file_size = file_size;
	file->file_name = g_strdup(file_name);
	file->local_id = local_transfer_id++;
	file->ic = df->ic = ic;
	df->ft = file;

	return df;
}

/* This is where the sending magic starts... */
file_transfer_t *dccs_send_start(struct im_connection *ic, irc_user_t *iu, const char *file_name, size_t file_size)
{
	file_transfer_t *file;
	dcc_file_transfer_t *df;
	irc_t *irc = (irc_t *) ic->bee->ui_data;
	struct sockaddr_storage saddr;
	char *errmsg;
	char host[HOST_NAME_MAX];
	char port[6];

	if (file_size > global.conf->ft_max_size) {
		return NULL;
	}

	df = dcc_alloc_transfer(file_name, file_size, ic);
	file = df->ft;
	file->write = dccs_send_write;

	/* listen and request */

	if ((df->fd = ft_listen(&saddr, host, port, irc->fd, TRUE, &errmsg)) == -1) {
		dcc_abort(df, "Failed to listen locally, check your ft_listen setting in bitlbee.conf: %s", errmsg);
		return NULL;
	}

	file->status = FT_STATUS_LISTENING;

	if (!dccs_send_request(df, iu, &saddr)) {
		return NULL;
	}

	/* watch */
	df->watch_in = b_input_add(df->fd, B_EV_IO_READ, dccs_send_proto, df);

	irc->file_transfers = g_slist_prepend(irc->file_transfers, file);

	df->progress_timeout = b_timeout_add(DCC_MAX_STALL * 1000, dcc_progress, df);

	imcb_log(ic, "File transfer request from %s for %s (%zd kb).\n"
	         "Accept the file transfer if you'd like the file. If you don't, "
	         "issue the 'transfer reject' command.",
	         iu->nick, file_name, file_size / 1024);

	return file;
}

/* Used pretty much everywhere in the code to abort a transfer */
gboolean dcc_abort(dcc_file_transfer_t *df, char *reason, ...)
{
	file_transfer_t *file = df->ft;
	va_list params;

	va_start(params, reason);
	char *msg = g_strdup_vprintf(reason, params);
	va_end(params);

	file->status |= FT_STATUS_CANCELED;

	if (file->canceled) {
		file->canceled(file, msg);
	}

	imcb_log(df->ic, "File %s: DCC transfer aborted: %s", file->file_name, msg);

	g_free(msg);

	dcc_close(df->ft);

	return FALSE;
}

gboolean dcc_progress(gpointer data, gint fd, b_input_condition cond)
{
	struct dcc_file_transfer *df = data;

	if (df->bytes_sent == df->progress_bytes_last) {
		/* no progress. cancel */
		if (df->bytes_sent == 0) {
			return dcc_abort(df, "Couldn't establish transfer within %d seconds", DCC_MAX_STALL);
		} else {
			return dcc_abort(df, "Transfer stalled for %d seconds at %d kb", DCC_MAX_STALL,
			                 df->bytes_sent / 1024);
		}

	}

	df->progress_bytes_last = df->bytes_sent;

	return TRUE;
}

/* used extensively for socket operations */
#define ASSERTSOCKOP(op, msg) \
	if ((op) == -1) { \
		return dcc_abort(df, msg ": %s", strerror(errno)); }

/* Creates the "DCC SEND" line and sends it to the server */
int dccs_send_request(struct dcc_file_transfer *df, irc_user_t *iu, struct sockaddr_storage *saddr)
{
	char ipaddr[INET6_ADDRSTRLEN];
	const void *netaddr;
	int port;
	char *cmd;

	if (saddr->ss_family == AF_INET) {
		struct sockaddr_in *saddr_ipv4 = ( struct sockaddr_in *) saddr;

		sprintf(ipaddr, "%d",
		        ntohl(saddr_ipv4->sin_addr.s_addr));
		port = saddr_ipv4->sin_port;
	} else {
		struct sockaddr_in6 *saddr_ipv6 = ( struct sockaddr_in6 *) saddr;

		netaddr = &saddr_ipv6->sin6_addr.s6_addr;
		port = saddr_ipv6->sin6_port;

		/*
		 * Didn't find docs about this, but it seems that's the way irssi does it
		 */
		if (!inet_ntop(saddr->ss_family, netaddr, ipaddr, sizeof(ipaddr))) {
			return dcc_abort(df, "inet_ntop failed: %s", strerror(errno));
		}
	}

	port = ntohs(port);

	cmd = g_strdup_printf("\001DCC SEND %s %s %u %zu\001",
	                      df->ft->file_name, ipaddr, port, df->ft->file_size);

	irc_send_msg_raw(iu, "PRIVMSG", iu->irc->user->nick, cmd);

	g_free(cmd);

	return TRUE;
}

/*
 * After setup, the transfer itself is handled entirely by this function.
 * There are basically four things to handle: connect, receive, send, and error.
 */
gboolean dccs_send_proto(gpointer data, gint fd, b_input_condition cond)
{
	dcc_file_transfer_t *df = data;
	file_transfer_t *file = df->ft;

	if ((cond & B_EV_IO_READ) &&
	    (file->status & FT_STATUS_LISTENING)) {
		struct sockaddr *clt_addr;
		socklen_t ssize = sizeof(clt_addr);

		/* Connect */

		ASSERTSOCKOP(df->fd = accept(fd, (struct sockaddr *) &clt_addr, &ssize), "Accepting connection");

		closesocket(fd);
		fd = df->fd;
		file->status = FT_STATUS_TRANSFERRING;
		sock_make_nonblocking(fd);

		/* IM protocol callback */
		if (file->accept) {
			file->accept(file);
		}

		/* reschedule for reading on new fd */
		df->watch_in = b_input_add(fd, B_EV_IO_READ, dccs_send_proto, df);

		return FALSE;
	}

	if (cond & B_EV_IO_READ) {
		int ret;

		ASSERTSOCKOP(ret = recv(fd, ((char *) &df->acked) + df->acked_len,
		                        sizeof(df->acked) - df->acked_len, 0), "Receiving");

		if (ret == 0) {
			return dcc_abort(df, "Remote end closed connection");
		}

		/* How likely is it that a 32-bit integer gets split across
		   packet boundaries? Chances are rarely 0 so let's be sure. */
		if ((df->acked_len = (df->acked_len + ret) % 4) > 0) {
			return TRUE;
		}

		df->acked = ntohl(df->acked);

		/* If any of this is actually happening, the receiver should buy a new IRC client */

		if (df->acked > df->bytes_sent) {
			return dcc_abort(df,
			                 "Receiver magically received more bytes than sent ( %d > %d ) (BUG at receiver?)", df->acked,
			                 df->bytes_sent);
		}

		if (df->acked < file->bytes_transferred) {
			return dcc_abort(df, "Receiver lost bytes? ( has %d, had %d ) (BUG at receiver?)", df->acked,
			                 file->bytes_transferred);
		}

		file->bytes_transferred = df->acked;

		if (file->bytes_transferred >= file->file_size) {
			if (df->proto_finished) {
				dcc_finish(file);
			}
			return FALSE;
		}

		return TRUE;
	}

	return TRUE;
}

gboolean dccs_recv_start(file_transfer_t *ft)
{
	dcc_file_transfer_t *df = ft->priv;
	struct sockaddr_storage *saddr = &df->saddr;
	int fd;
	char ipaddr[INET6_ADDRSTRLEN];
	socklen_t sa_len = saddr->ss_family == AF_INET ?
	                   sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);

	if (!ft->write) {
		return dcc_abort(df, "BUG: protocol didn't register write()");
	}

	ASSERTSOCKOP(fd = df->fd = socket(saddr->ss_family, SOCK_STREAM, 0), "Opening Socket");

	sock_make_nonblocking(fd);

	if ((connect(fd, (struct sockaddr *) saddr, sa_len) == -1) &&
	    (errno != EINPROGRESS)) {
		return dcc_abort(df, "Connecting to %s:%d : %s",
		                 inet_ntop(saddr->ss_family,
		                           saddr->ss_family == AF_INET ?
		                           ( void * ) &(( struct sockaddr_in *) saddr)->sin_addr.s_addr :
		                           ( void * ) &(( struct sockaddr_in6 *) saddr)->sin6_addr.s6_addr,
		                           ipaddr,
		                           sizeof(ipaddr)),
		                 ntohs(saddr->ss_family == AF_INET ?
		                       (( struct sockaddr_in *) saddr)->sin_port :
		                       (( struct sockaddr_in6 *) saddr)->sin6_port),
		                 strerror(errno));
	}

	ft->status = FT_STATUS_CONNECTING;

	/* watch */
	df->watch_out = b_input_add(df->fd, B_EV_IO_WRITE, dccs_recv_proto, df);
	ft->write_request = dccs_recv_write_request;

	df->progress_timeout = b_timeout_add(DCC_MAX_STALL * 1000, dcc_progress, df);

	return TRUE;
}

gboolean dccs_recv_proto(gpointer data, gint fd, b_input_condition cond)
{
	dcc_file_transfer_t *df = data;
	file_transfer_t *ft = df->ft;

	if ((cond & B_EV_IO_WRITE) &&
	    (ft->status & FT_STATUS_CONNECTING)) {
		ft->status = FT_STATUS_TRANSFERRING;

		//df->watch_in = b_input_add( df->fd, B_EV_IO_READ, dccs_recv_proto, df );

		df->watch_out = 0;
		return FALSE;
	}

	if (cond & B_EV_IO_READ) {
		int ret, done;

		ASSERTSOCKOP(ret = recv(fd, ft->buffer, sizeof(ft->buffer), 0), "Receiving");

		if (ret == 0) {
			return dcc_abort(df, "Remote end closed connection");
		}

		if (!ft->write(df->ft, ft->buffer, ret)) {
			return FALSE;
		}

		df->bytes_sent += ret;

		done = df->bytes_sent >= ft->file_size;

		if (((df->bytes_sent - ft->bytes_transferred) > DCC_PACKET_SIZE) ||
		    done) {
			guint32 ack = htonl(ft->bytes_transferred = df->bytes_sent);
			int ackret;

			ASSERTSOCKOP(ackret = send(fd, &ack, 4, 0), "Sending DCC ACK");

			if (ackret != 4) {
				return dcc_abort(df, "Error sending DCC ACK, sent %d instead of 4 bytes", ackret);
			}
		}

		if (df->bytes_sent == ret) {
			ft->started = time(NULL);
		}

		if (done) {
			if (df->watch_out) {
				b_event_remove(df->watch_out);
			}

			df->watch_in = 0;

			if (df->proto_finished) {
				dcc_finish(ft);
			}

			return FALSE;
		}

		df->watch_in = 0;
		return FALSE;
	}

	return TRUE;
}

gboolean dccs_recv_write_request(file_transfer_t *ft)
{
	dcc_file_transfer_t *df = ft->priv;

	if (df->watch_in) {
		return dcc_abort(df, "BUG: write_request() called while watching");
	}

	df->watch_in = b_input_add(df->fd, B_EV_IO_READ, dccs_recv_proto, df);

	return TRUE;
}

gboolean dccs_send_can_write(gpointer data, gint fd, b_input_condition cond)
{
	struct dcc_file_transfer *df = data;

	df->watch_out = 0;

	df->ft->write_request(df->ft);
	return FALSE;
}

/*
 * Incoming data.
 *
 */
gboolean dccs_send_write(file_transfer_t *file, char *data, unsigned int data_len)
{
	dcc_file_transfer_t *df = file->priv;
	int ret;

	receivedchunks++; receiveddata += data_len;

	if (df->watch_out) {
		return dcc_abort(df, "BUG: write() called while watching");
	}

	ASSERTSOCKOP(ret = send(df->fd, data, data_len, 0), "Sending data");

	if (ret == 0) {
		return dcc_abort(df, "Remote end closed connection");
	}

	/* TODO: this should really not be fatal */
	if (ret < data_len) {
		return dcc_abort(df, "send() sent %d instead of %d", ret, data_len);
	}

	if (df->bytes_sent == 0) {
		file->started = time(NULL);
	}

	df->bytes_sent += ret;

	if (df->bytes_sent < df->ft->file_size) {
		df->watch_out = b_input_add(df->fd, B_EV_IO_WRITE, dccs_send_can_write, df);
	}

	return TRUE;
}

/*
 * Cleans up after a transfer.
 */
void dcc_close(file_transfer_t *file)
{
	dcc_file_transfer_t *df = file->priv;
	irc_t *irc = (irc_t *) df->ic->bee->ui_data;

	if (file->free) {
		file->free(file);
	}

	closesocket(df->fd);

	if (df->watch_in) {
		b_event_remove(df->watch_in);
	}

	if (df->watch_out) {
		b_event_remove(df->watch_out);
	}

	if (df->progress_timeout) {
		b_event_remove(df->progress_timeout);
	}

	irc->file_transfers = g_slist_remove(irc->file_transfers, file);

	g_free(df);
	g_free(file->file_name);
	g_free(file);
}

void dcc_finish(file_transfer_t *file)
{
	dcc_file_transfer_t *df = file->priv;
	time_t diff = time(NULL) - file->started ? : 1;

	file->status |= FT_STATUS_FINISHED;

	if (file->finished) {
		file->finished(file);
	}

	imcb_log(df->ic, "File %s transferred successfully at %d kb/s!", file->file_name,
	         (int) (file->bytes_transferred / 1024 / diff));
	dcc_close(file);
}

/*
 * DCC SEND <filename> <IP> <port> <filesize>
 *
 * filename can be in "" or not. If it is, " can probably be escaped...
 * IP can be an unsigned int (IPV4) or something else (IPV6)
 *
 */
file_transfer_t *dcc_request(struct im_connection *ic, char* const* ctcp)
{
	irc_t *irc = (irc_t *) ic->bee->ui_data;
	file_transfer_t *ft;
	dcc_file_transfer_t *df;
	int gret;
	size_t filesize;

	if (ctcp[5] != NULL &&
	    sscanf(ctcp[4], "%zd", &filesize) == 1 &&   /* Just int. validation. */
	    sscanf(ctcp[5], "%zd", &filesize) == 1) {
		char *filename, *host, *port;
		struct addrinfo hints, *rp;

		filename = ctcp[2];

		host = ctcp[3];
		while (*host && g_ascii_isdigit(*host)) {
			host++;                                    /* Just digits? */
		}
		if (*host == '\0') {
			struct in_addr ipaddr = { .s_addr = htonl(atoll(ctcp[3])) };
			host = inet_ntoa(ipaddr);
		} else {
			/* Contains non-numbers, hopefully an IPV6 address */
			host = ctcp[3];
		}

		port = ctcp[4];
		filesize = atoll(ctcp[5]);

		memset(&hints, 0, sizeof(struct addrinfo));
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_flags = AI_NUMERICSERV;

		if ((gret = getaddrinfo(host, port, &hints, &rp))) {
			imcb_log(ic, "DCC: getaddrinfo() failed with %s "
			         "when parsing incoming 'DCC SEND': "
			         "host %s, port %s",
			         gai_strerror(gret), host, port);
			return NULL;
		}

		df = dcc_alloc_transfer(filename, filesize, ic);
		ft = df->ft;
		ft->sending = TRUE;
		memcpy(&df->saddr, rp->ai_addr, rp->ai_addrlen);

		freeaddrinfo(rp);

		irc->file_transfers = g_slist_prepend(irc->file_transfers, ft);

		return ft;
	} else {
		imcb_log(ic, "DCC: couldnt parse `DCC SEND' line");
	}

	return NULL;
}
