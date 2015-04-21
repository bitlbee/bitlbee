#include "bitlbee.h"
#include "lib/http_client.h"
#include "msn.h"

#define GATEWAY_HOST "geo.gateway.messenger.live.com"
#define GATEWAY_PORT 443

#define REQUEST_TEMPLATE \
	"POST /gateway/gateway.dll?SessionID=%s&%s HTTP/1.1\r\n" \
	"Host: %s\r\n" \
	"Content-Length: %zd\r\n" \
	"\r\n" \
	"%s"

static gboolean msn_gw_poll_timeout(gpointer data, gint source, b_input_condition cond);

struct msn_gw *msn_gw_new(struct im_connection *ic)
{
	struct msn_gw *gw = g_new0(struct msn_gw, 1);
	gw->last_host = g_strdup(GATEWAY_HOST);
	gw->port = GATEWAY_PORT;
	gw->ssl = (GATEWAY_PORT == 443);
	gw->poll_timeout = -1;
	gw->ic = ic;
	gw->md = ic->proto_data;
	gw->in = g_byte_array_new();
	gw->out = g_byte_array_new();
	return gw;
}

void msn_gw_free(struct msn_gw *gw)
{
	if (gw->poll_timeout != -1) {
		b_event_remove(gw->poll_timeout);
	}
	g_byte_array_free(gw->in, TRUE);
	g_byte_array_free(gw->out, TRUE);
	g_free(gw->session_id);
	g_free(gw->last_host);
	g_free(gw);
}

static struct msn_gw *msn_gw_from_ic(struct im_connection *ic)
{
	if (g_slist_find(msn_connections, ic) == NULL) {
		return NULL;
	} else {
		struct msn_data *md = ic->proto_data;
		return md->gw;
	}
}

static gboolean msn_gw_parse_session_header(struct msn_gw *gw, char *value)
{
	int i;
	char **subvalues;
	gboolean closed = FALSE;

	subvalues = g_strsplit(value, "; ", 0);

	for (i = 0; subvalues[i]; i++) {
		if (strcmp(subvalues[i], "Session=close") == 0) {
			/* gateway closed, signal the death of the socket */
			closed = TRUE;
		} else if (g_str_has_prefix(subvalues[i], "SessionID=")) {
			/* copy the part after the = to session_id*/
			g_free(gw->session_id);
			gw->session_id = g_strdup(subvalues[i] + 10);
		}
	}

	g_strfreev(subvalues);

	return !closed;
}

void msn_gw_callback(struct http_request *req)
{
	struct msn_gw *gw;
	char *value;

	if (!(gw = msn_gw_from_ic(req->data))) {
		return;
	}

	gw->waiting = FALSE;
	gw->polling = FALSE;

	if (getenv("BITLBEE_DEBUG")) {
		fprintf(stderr, "\n\x1b[90mHTTP:%s\n", req->reply_body);
		fprintf(stderr, "\n\x1b[97m\n");
	}

	if (req->status_code != 200) {
		gw->callback(gw->md, -1, B_EV_IO_READ);
		return;
	}

	if ((value = get_rfc822_header(req->reply_headers, "X-MSN-Messenger", 0))) {
		if (!msn_gw_parse_session_header(gw, value)) {
			gw->callback(gw->md, -1, B_EV_IO_READ);
			g_free(value);
			return;
		}
		g_free(value);
	}
	
	if ((value = get_rfc822_header(req->reply_headers, "X-MSN-Host", 0))) {
		g_free(gw->last_host);
		gw->last_host = value; /* transfer */
	}

	if (req->body_size) {
		g_byte_array_append(gw->in, (const guint8 *) req->reply_body, req->body_size);
		gw->callback(gw->md, -1, B_EV_IO_READ);
	}

	if (gw->poll_timeout != -1) {
		b_event_remove(gw->poll_timeout);
	}
	gw->poll_timeout = b_timeout_add(500, msn_gw_poll_timeout, gw->ic);

}

void msn_gw_dorequest(struct msn_gw *gw, char *args)
{
	char *request = NULL;
	char *body = NULL;
	size_t bodylen = 0;

	if (gw->out) {
		bodylen = gw->out->len;
		g_byte_array_append(gw->out, (guint8 *) "", 1); /* nullnullnull */
		body = (char *) g_byte_array_free(gw->out, FALSE);
		gw->out = g_byte_array_new();
	}

	if (!bodylen && !args) {
		args = "Action=poll&Lifespan=60";
		gw->polling = TRUE;
	}

	request = g_strdup_printf(REQUEST_TEMPLATE,
		gw->session_id ? : "", args ? : "", gw->last_host, bodylen, body ? : "");

	http_dorequest(gw->last_host, gw->port, gw->ssl, request, msn_gw_callback, gw->ic);
	gw->open = TRUE;
	gw->waiting = TRUE;

	g_free(body);
	g_free(request);
}

void msn_gw_open(struct msn_gw *gw)
{
	msn_gw_dorequest(gw, "Action=open&Server=NS");
}

static gboolean msn_gw_poll_timeout(gpointer data, gint source, b_input_condition cond)
{
	struct msn_gw *gw;

	if (!(gw = msn_gw_from_ic(data))) {
		return FALSE;
	}

	gw->poll_timeout = -1;
	if (!gw->waiting) {
		msn_gw_dorequest(gw, NULL);
	}
	return FALSE;
}

ssize_t msn_gw_read(struct msn_gw *gw, char **buf)
{
	size_t bodylen;
	if (!gw->open) {
		return 0;
	}

	bodylen = gw->in->len;
	g_byte_array_append(gw->in, (guint8 *) "", 1); /* nullnullnull */
	*buf = (char *) g_byte_array_free(gw->in, FALSE);
	gw->in = g_byte_array_new();
	return bodylen;
}

void msn_gw_write(struct msn_gw *gw, char *buf, size_t len)
{
	g_byte_array_append(gw->out, (const guint8 *) buf, len);
	if (!gw->open) {
		msn_gw_open(gw);
	} else if (gw->polling || !gw->waiting) {
		msn_gw_dorequest(gw, NULL);
	}
}
