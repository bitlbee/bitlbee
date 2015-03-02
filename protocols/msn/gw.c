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

struct msn_gw *msn_gw_new(struct msn_data *md)
{
	struct msn_gw *gw = g_new0(struct msn_gw, 1);
	gw->last_host = g_strdup(GATEWAY_HOST);
	gw->port = GATEWAY_PORT;
	gw->ssl = (GATEWAY_PORT == 443);
	gw->poll_timeout = -1;
	gw->data = md;
	gw->in = g_byte_array_new();
	gw->out = g_byte_array_new();
	return gw;
}

void msn_gw_free(struct msn_gw *gw)
{
	g_byte_array_free(gw->in, TRUE);
	g_byte_array_free(gw->out, TRUE);
	g_free(gw->session_id);
	g_free(gw->last_host);
	g_free(gw);
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
	char *value;
	struct msn_gw *gw = req->data;

	if ((value = get_rfc822_header(req->reply_headers, "X-MSN-Messenger", 0))) {
		if (!msn_gw_parse_session_header(gw, value)) {
			/* XXX handle this */
		}
		g_free(value);
	}
	
	if ((value = get_rfc822_header(req->reply_headers, "X-MSN-Host", 0))) {
		g_free(gw->last_host);
		gw->last_host = value; /* transfer */
	}

	/* XXX handle reply */

	if (gw->poll_timeout != -1) {
		b_event_remove(gw->poll_timeout);
	}
	gw->poll_timeout = b_timeout_add(5000, msn_gw_poll_timeout, gw);

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

	request = g_strdup_printf(REQUEST_TEMPLATE,
		gw->session_id ? : "", args ? : "", gw->last_host, bodylen, body ? : "");

	http_dorequest(gw->last_host, gw->port, gw->ssl, request, msn_gw_callback, gw);
	gw->waiting = TRUE;

	g_free(body);
	g_free(request);
}

void msn_gw_open(struct msn_gw *gw)
{
	msn_gw_dorequest(gw, "Action=open&Server=NS");
	gw->open = TRUE;
}

static gboolean msn_gw_poll_timeout(gpointer data, gint source, b_input_condition cond)
{
	struct msn_gw *gw = data;
	gw->poll_timeout = -1;
	if (!gw->waiting) {
		msn_gw_dorequest(gw, NULL);
	}
	return FALSE;
}
