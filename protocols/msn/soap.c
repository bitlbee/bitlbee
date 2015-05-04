/********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2012 Wilmer van der Gaast and others                *
  \********************************************************************/

/* MSN module - All the SOAPy XML stuff.
   Some manager at Microsoft apparently thought MSNP wasn't XMLy enough so
   someone stepped up and changed that. This is the result. Kilobytes and
   more kilobytes of XML vomit to transfer tiny bits of informaiton. */

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

#include "http_client.h"
#include "soap.h"
#include "msn.h"
#include "bitlbee.h"
#include "url.h"
#include "misc.h"
#include "sha1.h"
#include "base64.h"
#include "xmltree.h"
#include <ctype.h>
#include <errno.h>

/* This file tries to make SOAP stuff pretty simple to do by letting you just
   provide a function to build a request, a few functions to parse various
   parts of the response, and a function to run when the full response was
   received and parsed. See the various examples below. */

typedef enum {
	MSN_SOAP_OK,
	MSN_SOAP_RETRY,
	MSN_SOAP_REAUTH,
	MSN_SOAP_ABORT,
} msn_soap_result_t;

struct msn_soap_req_data;
typedef int (*msn_soap_func) (struct msn_soap_req_data *);

struct msn_soap_req_data {
	void *data;
	struct im_connection *ic;
	int ttl;
	char *error;

	char *url, *action, *payload;
	struct http_request *http_req;

	const struct xt_handler_entry *xml_parser;
	msn_soap_func build_request, handle_response, free_data;
};

static int msn_soap_send_request(struct msn_soap_req_data *req);
static void msn_soap_free(struct msn_soap_req_data *soap_req);
static void msn_soap_debug_print(const char *headers, const char *payload);

static int msn_soap_start(struct im_connection *ic,
                          void *data,
                          msn_soap_func build_request,
                          const struct xt_handler_entry *xml_parser,
                          msn_soap_func handle_response,
                          msn_soap_func free_data)
{
	struct msn_soap_req_data *req = g_new0(struct msn_soap_req_data, 1);

	req->ic = ic;
	req->data = data;
	req->xml_parser = xml_parser;
	req->build_request = build_request;
	req->handle_response = handle_response;
	req->free_data = free_data;
	req->ttl = 3;

	return msn_soap_send_request(req);
}

static void msn_soap_handle_response(struct http_request *http_req);

static int msn_soap_send_request(struct msn_soap_req_data *soap_req)
{
	char *http_req;
	char *soap_action = NULL;
	url_t url;

	soap_req->build_request(soap_req);

	if (soap_req->action) {
		soap_action = g_strdup_printf("SOAPAction: \"%s\"\r\n", soap_req->action);
	}

	url_set(&url, soap_req->url);
	http_req = g_strdup_printf(SOAP_HTTP_REQUEST, url.file, url.host,
	                           soap_action ? soap_action : "",
	                           strlen(soap_req->payload), soap_req->payload);

	msn_soap_debug_print(http_req, soap_req->payload);

	soap_req->http_req = http_dorequest(url.host, url.port, url.proto == PROTO_HTTPS,
	                                    http_req, msn_soap_handle_response, soap_req);

	g_free(http_req);
	g_free(soap_action);

	return soap_req->http_req != NULL;
}

static void msn_soap_handle_response(struct http_request *http_req)
{
	struct msn_soap_req_data *soap_req = http_req->data;
	int st;

	if (g_slist_find(msn_connections, soap_req->ic) == NULL) {
		msn_soap_free(soap_req);
		return;
	}

	msn_soap_debug_print(http_req->reply_headers, http_req->reply_body);

	if (http_req->body_size > 0) {
		struct xt_parser *parser;
		struct xt_node *err;

		parser = xt_new(soap_req->xml_parser, soap_req);
		xt_feed(parser, http_req->reply_body, http_req->body_size);
		if (http_req->status_code == 500 &&
		    (err = xt_find_path(parser->root, "soap:Body/soap:Fault/detail/errorcode")) &&
		    err->text_len > 0) {
			if (strcmp(err->text, "PassportAuthFail") == 0) {
				xt_free(parser);
				st = MSN_SOAP_REAUTH;
				goto fail;
			}
			/* TODO: Handle/report other errors. */
		}

		xt_handle(parser, NULL, -1);
		xt_free(parser);
	}

	if (http_req->status_code != 200) {
		soap_req->error = g_strdup(http_req->status_string);
	}

	st = soap_req->handle_response(soap_req);

fail:
	g_free(soap_req->url);
	g_free(soap_req->action);
	g_free(soap_req->payload);
	g_free(soap_req->error);
	soap_req->url = soap_req->action = soap_req->payload = soap_req->error = NULL;

	if (st == MSN_SOAP_RETRY && --soap_req->ttl) {
		msn_soap_send_request(soap_req);
	} else if (st == MSN_SOAP_REAUTH) {
		struct msn_data *md = soap_req->ic->proto_data;

		if (!(md->flags & MSN_REAUTHING)) {
			/* Nonce shouldn't actually be touched for re-auths. */
			msn_soap_passport_sso_request(soap_req->ic, "blaataap");
			md->flags |= MSN_REAUTHING;
		}
		md->soapq = g_slist_append(md->soapq, soap_req);
	} else {
		soap_req->free_data(soap_req);
		g_free(soap_req);
	}
}

static char *msn_soap_abservice_build(const char *body_fmt, const char *scenario, const char *ticket, ...)
{
	va_list params;
	char *ret, *format, *body;

	format = g_markup_printf_escaped(SOAP_ABSERVICE_PAYLOAD, scenario, ticket);

	va_start(params, ticket);
	body = g_strdup_vprintf(body_fmt, params);
	va_end(params);

	ret = g_strdup_printf(format, body);
	g_free(body);
	g_free(format);

	return ret;
}

static void msn_soap_debug_print(const char *headers, const char *payload)
{
	char *s;

	if (!getenv("BITLBEE_DEBUG")) {
		return;
	}
	fprintf(stderr, "\n\x1b[90mSOAP:\n");

	if (headers) {
		if ((s = strstr(headers, "\r\n\r\n"))) {
			write(2, headers, s - headers + 4);
		} else {
			write(2, headers, strlen(headers));
		}
	}

	if (payload) {
		struct xt_node *xt = xt_from_string(payload, 0);
		if (xt) {
			xt_print(xt);
		}
		xt_free_node(xt);
	}
	fprintf(stderr, "\n\x1b[97m\n");
}

int msn_soapq_flush(struct im_connection *ic, gboolean resend)
{
	struct msn_data *md = ic->proto_data;

	while (md->soapq) {
		if (resend) {
			msn_soap_send_request((struct msn_soap_req_data*) md->soapq->data);
		} else {
			msn_soap_free((struct msn_soap_req_data*) md->soapq->data);
		}
		md->soapq = g_slist_remove(md->soapq, md->soapq->data);
	}

	return MSN_SOAP_OK;
}

static void msn_soap_free(struct msn_soap_req_data *soap_req)
{
	soap_req->free_data(soap_req);
	g_free(soap_req->url);
	g_free(soap_req->action);
	g_free(soap_req->payload);
	g_free(soap_req->error);
	g_free(soap_req);
}


/* passport_sso: Authentication MSNP15+ */

struct msn_soap_passport_sso_data {
	char *nonce;
	char *secret;
	char *error;
	char *redirect;
};

static int msn_soap_passport_sso_build_request(struct msn_soap_req_data *soap_req)
{
	struct msn_soap_passport_sso_data *sd = soap_req->data;
	struct im_connection *ic = soap_req->ic;
	struct msn_data *md = ic->proto_data;
	char pass[MAX_PASSPORT_PWLEN + 1];

	if (sd->redirect) {
		soap_req->url = sd->redirect;
		sd->redirect = NULL;
	}
	/* MS changed this URL and broke the old MSN-specific one. The generic
	   one works, forwarding us to a msn.com URL that works. Takes an extra
	   second, but that's better than not being able to log in at all. :-/
	else if( g_str_has_suffix( ic->acc->user, "@msn.com" ) )
	        soap_req->url = g_strdup( SOAP_PASSPORT_SSO_URL_MSN );
	*/
	else {
		soap_req->url = g_strdup(SOAP_PASSPORT_SSO_URL);
	}

	strncpy(pass, ic->acc->pass, MAX_PASSPORT_PWLEN);
	pass[MAX_PASSPORT_PWLEN] = '\0';
	soap_req->payload = g_markup_printf_escaped(SOAP_PASSPORT_SSO_PAYLOAD,
	                                            ic->acc->user, pass, md->pp_policy);

	return MSN_SOAP_OK;
}

static xt_status msn_soap_passport_sso_token(struct xt_node *node, gpointer data)
{
	struct msn_soap_req_data *soap_req = data;
	struct msn_soap_passport_sso_data *sd = soap_req->data;
	struct msn_data *md = soap_req->ic->proto_data;
	struct xt_node *p;
	char *id;

	if ((id = xt_find_attr(node, "Id")) == NULL) {
		return XT_HANDLED;
	}
	id += strlen(id) - 1;
	if (*id == '1' &&
	    (p = xt_find_path(node, "../../wst:RequestedProofToken/wst:BinarySecret")) &&
	    p->text) {
		sd->secret = g_strdup(p->text);
	}

	*id -= '1';
	if (*id >= 0 && *id < sizeof(md->tokens) / sizeof(md->tokens[0])) {
		g_free(md->tokens[(int) *id]);
		md->tokens[(int) *id] = g_strdup(node->text);
	}

	return XT_HANDLED;
}

static xt_status msn_soap_passport_failure(struct xt_node *node, gpointer data)
{
	struct msn_soap_req_data *soap_req = data;
	struct msn_soap_passport_sso_data *sd = soap_req->data;
	struct xt_node *code = xt_find_node(node->children, "faultcode");
	struct xt_node *string = xt_find_node(node->children, "faultstring");
	struct xt_node *url;

	if (code == NULL || code->text_len == 0) {
		sd->error = g_strdup("Unknown error");
	} else if (strcmp(code->text, "psf:Redirect") == 0 &&
	           (url = xt_find_node(node->children, "psf:redirectUrl")) &&
	           url->text_len > 0) {
		sd->redirect = g_strdup(url->text);
	} else {
		sd->error = g_strdup_printf("%s (%s)", code->text, string && string->text_len ?
		                            string->text : "no description available");
	}

	return XT_HANDLED;
}

static const struct xt_handler_entry msn_soap_passport_sso_parser[] = {
	{ "wsse:BinarySecurityToken", "wst:RequestedSecurityToken", msn_soap_passport_sso_token },
	{ "S:Fault", "S:Envelope", msn_soap_passport_failure },
	{ NULL, NULL, NULL }
};

static char *msn_key_fuckery(char *key, int key_len, char *type)
{
	unsigned char hash1[20 + strlen(type) + 1];
	unsigned char hash2[20];
	char *ret;

	sha1_hmac(key, key_len, type, 0, hash1);
	strcpy((char *) hash1 + 20, type);
	sha1_hmac(key, key_len, (char *) hash1, sizeof(hash1) - 1, hash2);

	/* This is okay as hash1 is read completely before it's overwritten. */
	sha1_hmac(key, key_len, (char *) hash1, 20, hash1);
	sha1_hmac(key, key_len, (char *) hash1, sizeof(hash1) - 1, hash1);

	ret = g_malloc(24);
	memcpy(ret, hash2, 20);
	memcpy(ret + 20, hash1, 4);
	return ret;
}

static int msn_soap_passport_sso_handle_response(struct msn_soap_req_data *soap_req)
{
	struct msn_soap_passport_sso_data *sd = soap_req->data;
	struct im_connection *ic = soap_req->ic;
	struct msn_data *md = ic->proto_data;
	char *key1, *key2, *key3, *blurb64;
	int key1_len;
	unsigned char *padnonce, *des3res;

	struct {
		unsigned int uStructHeaderSize; // 28. Does not count data
		unsigned int uCryptMode; // CRYPT_MODE_CBC (1)
		unsigned int uCipherType; // TripleDES (0x6603)
		unsigned int uHashType; // SHA1 (0x8004)
		unsigned int uIVLen;    // 8
		unsigned int uHashLen;  // 20
		unsigned int uCipherLen; // 72
		unsigned char iv[8];
		unsigned char hash[20];
		unsigned char cipherbytes[72];
	} blurb = {
		GUINT32_TO_LE(28),
		GUINT32_TO_LE(1),
		GUINT32_TO_LE(0x6603),
		GUINT32_TO_LE(0x8004),
		GUINT32_TO_LE(8),
		GUINT32_TO_LE(20),
		GUINT32_TO_LE(72),
	};

	if (sd->redirect) {
		return MSN_SOAP_RETRY;
	}

	if (md->soapq) {
		md->flags &= ~MSN_REAUTHING;
		return msn_soapq_flush(ic, TRUE);
	}

	if (sd->secret == NULL) {
		msn_auth_got_passport_token(ic, NULL, sd->error ? sd->error : soap_req->error);
		return MSN_SOAP_OK;
	}

	key1_len = base64_decode(sd->secret, (unsigned char **) &key1);

	key2 = msn_key_fuckery(key1, key1_len, "WS-SecureConversationSESSION KEY HASH");
	key3 = msn_key_fuckery(key1, key1_len, "WS-SecureConversationSESSION KEY ENCRYPTION");

	sha1_hmac(key2, 24, sd->nonce, 0, blurb.hash);
	padnonce = g_malloc(strlen(sd->nonce) + 8);
	strcpy((char *) padnonce, sd->nonce);
	memset(padnonce + strlen(sd->nonce), 8, 8);

	random_bytes(blurb.iv, 8);

	ssl_des3_encrypt((unsigned char *) key3, 24, padnonce, strlen(sd->nonce) + 8, blurb.iv, &des3res);
	memcpy(blurb.cipherbytes, des3res, 72);

	blurb64 = base64_encode((unsigned char *) &blurb, sizeof(blurb));
	msn_auth_got_passport_token(ic, blurb64, NULL);

	g_free(padnonce);
	g_free(blurb64);
	g_free(des3res);
	g_free(key1);
	g_free(key2);
	g_free(key3);

	return MSN_SOAP_OK;
}

static int msn_soap_passport_sso_free_data(struct msn_soap_req_data *soap_req)
{
	struct msn_soap_passport_sso_data *sd = soap_req->data;

	g_free(sd->nonce);
	g_free(sd->secret);
	g_free(sd->error);
	g_free(sd->redirect);
	g_free(sd);

	return MSN_SOAP_OK;
}

int msn_soap_passport_sso_request(struct im_connection *ic, const char *nonce)
{
	struct msn_soap_passport_sso_data *sd = g_new0(struct msn_soap_passport_sso_data, 1);

	sd->nonce = g_strdup(nonce);

	return msn_soap_start(ic, sd, msn_soap_passport_sso_build_request,
	                      msn_soap_passport_sso_parser,
	                      msn_soap_passport_sso_handle_response,
	                      msn_soap_passport_sso_free_data);
}


/* memlist: Fetching the membership list (NOT address book) */

static int msn_soap_memlist_build_request(struct msn_soap_req_data *soap_req)
{
	struct msn_data *md = soap_req->ic->proto_data;

	soap_req->url = g_strdup(SOAP_MEMLIST_URL);
	soap_req->action = g_strdup(SOAP_MEMLIST_ACTION);
	soap_req->payload = msn_soap_abservice_build(SOAP_MEMLIST_PAYLOAD, "Initial", md->tokens[1]);

	return 1;
}

static xt_status msn_soap_memlist_member(struct xt_node *node, gpointer data)
{
	bee_user_t *bu;
	struct msn_buddy_data *bd;
	struct xt_node *p;
	char *role = NULL, *handle = NULL;
	struct msn_soap_req_data *soap_req = data;
	struct im_connection *ic = soap_req->ic;

	if ((p = xt_find_path(node, "../../MemberRole"))) {
		role = p->text;
	}

	if ((p = xt_find_node(node->children, "PassportName"))) {
		handle = p->text;
	}

	if (!role || !handle ||
	    !((bu = bee_user_by_handle(ic->bee, ic, handle)) ||
	      (bu = bee_user_new(ic->bee, ic, handle, 0)))) {
		return XT_HANDLED;
	}

	bd = bu->data;
	if (strcmp(role, "Allow") == 0) {
		bd->flags |= MSN_BUDDY_AL;
		ic->permit = g_slist_prepend(ic->permit, g_strdup(handle));
	} else if (strcmp(role, "Block") == 0) {
		bd->flags |= MSN_BUDDY_BL;
		ic->deny = g_slist_prepend(ic->deny, g_strdup(handle));
	} else if (strcmp(role, "Reverse") == 0) {
		bd->flags |= MSN_BUDDY_RL;
	} else if (strcmp(role, "Pending") == 0) {
		bd->flags |= MSN_BUDDY_PL;
	}

	if (getenv("BITLBEE_DEBUG")) {
		fprintf(stderr, "%p %s %d\n", bu, handle, bd->flags);
	}

	return XT_HANDLED;
}

static const struct xt_handler_entry msn_soap_memlist_parser[] = {
	{ "Member", "Members", msn_soap_memlist_member },
	{ NULL,               NULL,     NULL                        }
};

static int msn_soap_memlist_handle_response(struct msn_soap_req_data *soap_req)
{
	msn_soap_addressbook_request(soap_req->ic);

	return MSN_SOAP_OK;
}

static int msn_soap_memlist_free_data(struct msn_soap_req_data *soap_req)
{
	return 0;
}

int msn_soap_memlist_request(struct im_connection *ic)
{
	return msn_soap_start(ic, NULL, msn_soap_memlist_build_request,
	                      msn_soap_memlist_parser,
	                      msn_soap_memlist_handle_response,
	                      msn_soap_memlist_free_data);
}

/* Variant: Adding/Removing people */
struct msn_soap_memlist_edit_data {
	char *handle;
	gboolean add;
	msn_buddy_flags_t list;
};

static int msn_soap_memlist_edit_build_request(struct msn_soap_req_data *soap_req)
{
	struct msn_data *md = soap_req->ic->proto_data;
	struct msn_soap_memlist_edit_data *med = soap_req->data;
	char *add, *scenario, *list;

	soap_req->url = g_strdup(SOAP_MEMLIST_URL);
	if (med->add) {
		soap_req->action = g_strdup(SOAP_MEMLIST_ADD_ACTION);
		add = "Add";
	} else {
		soap_req->action = g_strdup(SOAP_MEMLIST_DEL_ACTION);
		add = "Delete";
	}
	switch (med->list) {
	case MSN_BUDDY_AL:
		scenario = "BlockUnblock";
		list = "Allow";
		break;
	case MSN_BUDDY_BL:
		scenario = "BlockUnblock";
		list = "Block";
		break;
	case MSN_BUDDY_RL:
		scenario = "Timer";
		list = "Reverse";
		break;
	case MSN_BUDDY_PL:
	default:
		scenario = "Timer";
		list = "Pending";
		break;
	}
	soap_req->payload = msn_soap_abservice_build(SOAP_MEMLIST_EDIT_PAYLOAD,
	                                             scenario, md->tokens[1], add, list, med->handle, add);

	return 1;
}

static int msn_soap_memlist_edit_handle_response(struct msn_soap_req_data *soap_req)
{
	return MSN_SOAP_OK;
}

static int msn_soap_memlist_edit_free_data(struct msn_soap_req_data *soap_req)
{
	struct msn_soap_memlist_edit_data *med = soap_req->data;

	g_free(med->handle);
	g_free(med);

	return 0;
}

int msn_soap_memlist_edit(struct im_connection *ic, const char *handle, gboolean add, int list)
{
	struct msn_soap_memlist_edit_data *med;

	med = g_new0(struct msn_soap_memlist_edit_data, 1);
	med->handle = g_strdup(handle);
	med->add = add;
	med->list = list;

	return msn_soap_start(ic, med, msn_soap_memlist_edit_build_request,
	                      NULL,
	                      msn_soap_memlist_edit_handle_response,
	                      msn_soap_memlist_edit_free_data);
}


/* addressbook: Fetching the membership list (NOT address book) */

static int msn_soap_addressbook_build_request(struct msn_soap_req_data *soap_req)
{
	struct msn_data *md = soap_req->ic->proto_data;

	soap_req->url = g_strdup(SOAP_ADDRESSBOOK_URL);
	soap_req->action = g_strdup(SOAP_ADDRESSBOOK_ACTION);
	soap_req->payload = msn_soap_abservice_build(SOAP_ADDRESSBOOK_PAYLOAD, "Initial", md->tokens[1]);

	return 1;
}

static xt_status msn_soap_addressbook_group(struct xt_node *node, gpointer data)
{
	struct xt_node *p;
	char *id = NULL, *name = NULL;
	struct msn_soap_req_data *soap_req = data;
	struct msn_data *md = soap_req->ic->proto_data;

	if ((p = xt_find_path(node, "../groupId"))) {
		id = p->text;
	}

	if ((p = xt_find_node(node->children, "name"))) {
		name = p->text;
	}

	if (id && name) {
		struct msn_group *mg = g_new0(struct msn_group, 1);
		mg->id = g_strdup(id);
		mg->name = g_strdup(name);
		md->groups = g_slist_prepend(md->groups, mg);
	}

	if (getenv("BITLBEE_DEBUG")) {
		fprintf(stderr, "%s %s\n", id, name);
	}

	return XT_HANDLED;
}

static xt_status msn_soap_addressbook_contact(struct xt_node *node, gpointer data)
{
	bee_user_t *bu;
	struct msn_buddy_data *bd;
	struct xt_node *p;
	char *id = NULL, *type = NULL, *handle = NULL, *is_msgr = "false",
	*display_name = NULL, *group_id = NULL;
	struct msn_soap_req_data *soap_req = data;
	struct im_connection *ic = soap_req->ic;
	struct msn_group *group;

	if ((p = xt_find_path(node, "../contactId"))) {
		id = p->text;
	}
	if ((p = xt_find_node(node->children, "contactType"))) {
		type = p->text;
	}
	if ((p = xt_find_node(node->children, "passportName"))) {
		handle = p->text;
	}
	if ((p = xt_find_node(node->children, "displayName"))) {
		display_name = p->text;
	}
	if ((p = xt_find_node(node->children, "isMessengerUser"))) {
		is_msgr = p->text;
	}
	if ((p = xt_find_path(node, "groupIds/guid"))) {
		group_id = p->text;
	}

	if (type && g_strcasecmp(type, "me") == 0) {
		set_t *set = set_find(&ic->acc->set, "display_name");
		g_free(set->value);
		set->value = g_strdup(display_name);

		/* Try to fetch the profile; if the user has one, that's where
		   we can find the persistent display_name. */
		if ((p = xt_find_node(node->children, "CID")) && p->text) {
			msn_soap_profile_get(ic, p->text);
		}

		return XT_HANDLED;
	}

	if (!bool2int(is_msgr) || handle == NULL) {
		return XT_HANDLED;
	}

	if (!(bu = bee_user_by_handle(ic->bee, ic, handle)) &&
	    !(bu = bee_user_new(ic->bee, ic, handle, 0))) {
		return XT_HANDLED;
	}

	bd = bu->data;
	bd->flags |= MSN_BUDDY_FL;
	g_free(bd->cid);
	bd->cid = g_strdup(id);

	imcb_rename_buddy(ic, handle, display_name);

	if (group_id && (group = msn_group_by_id(ic, group_id))) {
		imcb_add_buddy(ic, handle, group->name);
	}

	if (getenv("BITLBEE_DEBUG")) {
		fprintf(stderr, "%s %s %s %s\n", id, type, handle, display_name);
	}

	return XT_HANDLED;
}

static const struct xt_handler_entry msn_soap_addressbook_parser[] = {
	{ "contactInfo", "Contact", msn_soap_addressbook_contact },
	{ "groupInfo", "Group", msn_soap_addressbook_group },
	{ NULL,               NULL,     NULL                        }
};

static int msn_soap_addressbook_handle_response(struct msn_soap_req_data *soap_req)
{
	GSList *l;
	int wtf = 0;

	for (l = soap_req->ic->bee->users; l; l = l->next) {
		struct bee_user *bu = l->data;
		struct msn_buddy_data *bd = bu->data;

		if (bu->ic == soap_req->ic && bd) {
			msn_buddy_ask(bu);

			if ((bd->flags & (MSN_BUDDY_AL | MSN_BUDDY_BL)) ==
			    (MSN_BUDDY_AL | MSN_BUDDY_BL)) {
				/* both allow and block, delete block, add wtf */
				bd->flags &= ~MSN_BUDDY_BL;
				wtf++;
			}


			if ((bd->flags & (MSN_BUDDY_AL | MSN_BUDDY_BL)) == 0) {
				/* neither allow or block, add allow */
				bd->flags |= MSN_BUDDY_AL;
			}
		}
	}

	if (wtf) {
		imcb_log(soap_req->ic, "Warning: %d contacts were in both your "
		         "block and your allow list. Assuming they're all "
		         "allowed. Use the official WLM client once to fix "
		         "this.", wtf);
	}

	msn_auth_got_contact_list(soap_req->ic);

	return MSN_SOAP_OK;
}

static int msn_soap_addressbook_free_data(struct msn_soap_req_data *soap_req)
{
	return 0;
}

int msn_soap_addressbook_request(struct im_connection *ic)
{
	return msn_soap_start(ic, NULL, msn_soap_addressbook_build_request,
	                      msn_soap_addressbook_parser,
	                      msn_soap_addressbook_handle_response,
	                      msn_soap_addressbook_free_data);
}

/* Variant: Change our display name. */
static int msn_soap_ab_namechange_build_request(struct msn_soap_req_data *soap_req)
{
	struct msn_data *md = soap_req->ic->proto_data;

	soap_req->url = g_strdup(SOAP_ADDRESSBOOK_URL);
	soap_req->action = g_strdup(SOAP_AB_NAMECHANGE_ACTION);
	soap_req->payload = msn_soap_abservice_build(SOAP_AB_NAMECHANGE_PAYLOAD,
	                                             "Timer", md->tokens[1], (char *) soap_req->data);

	return 1;
}

static int msn_soap_ab_namechange_handle_response(struct msn_soap_req_data *soap_req)
{
	/* TODO: Ack the change? Not sure what the NAKs look like.. */
	return MSN_SOAP_OK;
}

static int msn_soap_ab_namechange_free_data(struct msn_soap_req_data *soap_req)
{
	g_free(soap_req->data);
	return 0;
}

int msn_soap_addressbook_set_display_name(struct im_connection *ic, const char *new)
{
	return msn_soap_start(ic, g_strdup(new),
	                      msn_soap_ab_namechange_build_request,
	                      NULL,
	                      msn_soap_ab_namechange_handle_response,
	                      msn_soap_ab_namechange_free_data);
}

/* Add a contact. */
static int msn_soap_ab_contact_add_build_request(struct msn_soap_req_data *soap_req)
{
	struct msn_data *md = soap_req->ic->proto_data;
	bee_user_t *bu = soap_req->data;

	soap_req->url = g_strdup(SOAP_ADDRESSBOOK_URL);
	soap_req->action = g_strdup(SOAP_AB_CONTACT_ADD_ACTION);
	soap_req->payload = msn_soap_abservice_build(SOAP_AB_CONTACT_ADD_PAYLOAD,
	                                             "ContactSave", md->tokens[1], bu->handle,
	                                             bu->fullname ? bu->fullname : bu->handle);

	return 1;
}

static xt_status msn_soap_ab_contact_add_cid(struct xt_node *node, gpointer data)
{
	struct msn_soap_req_data *soap_req = data;
	bee_user_t *bu = soap_req->data;
	struct msn_buddy_data *bd = bu->data;

	g_free(bd->cid);
	bd->cid = g_strdup(node->text);

	return XT_HANDLED;
}

static const struct xt_handler_entry msn_soap_ab_contact_add_parser[] = {
	{ "guid", "ABContactAddResult", msn_soap_ab_contact_add_cid },
	{ NULL,               NULL,     NULL                        }
};

static int msn_soap_ab_contact_add_handle_response(struct msn_soap_req_data *soap_req)
{
	/* TODO: Ack the change? Not sure what the NAKs look like.. */
	return MSN_SOAP_OK;
}

static int msn_soap_ab_contact_add_free_data(struct msn_soap_req_data *soap_req)
{
	return 0;
}

int msn_soap_ab_contact_add(struct im_connection *ic, bee_user_t *bu)
{
	return msn_soap_start(ic, bu,
	                      msn_soap_ab_contact_add_build_request,
	                      msn_soap_ab_contact_add_parser,
	                      msn_soap_ab_contact_add_handle_response,
	                      msn_soap_ab_contact_add_free_data);
}

/* Remove a contact. */
static int msn_soap_ab_contact_del_build_request(struct msn_soap_req_data *soap_req)
{
	struct msn_data *md = soap_req->ic->proto_data;
	const char *cid = soap_req->data;

	soap_req->url = g_strdup(SOAP_ADDRESSBOOK_URL);
	soap_req->action = g_strdup(SOAP_AB_CONTACT_DEL_ACTION);
	soap_req->payload = msn_soap_abservice_build(SOAP_AB_CONTACT_DEL_PAYLOAD,
	                                             "Timer", md->tokens[1], cid);

	return 1;
}

static int msn_soap_ab_contact_del_handle_response(struct msn_soap_req_data *soap_req)
{
	/* TODO: Ack the change? Not sure what the NAKs look like.. */
	return MSN_SOAP_OK;
}

static int msn_soap_ab_contact_del_free_data(struct msn_soap_req_data *soap_req)
{
	g_free(soap_req->data);
	return 0;
}

int msn_soap_ab_contact_del(struct im_connection *ic, bee_user_t *bu)
{
	struct msn_buddy_data *bd = bu->data;

	return msn_soap_start(ic, g_strdup(bd->cid),
	                      msn_soap_ab_contact_del_build_request,
	                      NULL,
	                      msn_soap_ab_contact_del_handle_response,
	                      msn_soap_ab_contact_del_free_data);
}



/* Storage stuff: Fetch profile. */
static int msn_soap_profile_get_build_request(struct msn_soap_req_data *soap_req)
{
	struct msn_data *md = soap_req->ic->proto_data;

	soap_req->url = g_strdup(SOAP_STORAGE_URL);
	soap_req->action = g_strdup(SOAP_PROFILE_GET_ACTION);
	soap_req->payload = g_markup_printf_escaped(SOAP_PROFILE_GET_PAYLOAD,
	                                            md->tokens[3], (char *) soap_req->data);

	return 1;
}

static xt_status msn_soap_profile_get_result(struct xt_node *node, gpointer data)
{
	struct msn_soap_req_data *soap_req = data;
	struct im_connection *ic = soap_req->ic;
	struct msn_data *md = soap_req->ic->proto_data;
	struct xt_node *dn;

	if ((dn = xt_find_node(node->children, "DisplayName")) && dn->text) {
		set_t *set = set_find(&ic->acc->set, "display_name");
		g_free(set->value);
		set->value = g_strdup(dn->text);

		md->flags |= MSN_GOT_PROFILE_DN;
	}

	return XT_HANDLED;
}

static xt_status msn_soap_profile_get_rid(struct xt_node *node, gpointer data)
{
	struct msn_soap_req_data *soap_req = data;
	struct msn_data *md = soap_req->ic->proto_data;

	g_free(md->profile_rid);
	md->profile_rid = g_strdup(node->text);

	return XT_HANDLED;
}

static const struct xt_handler_entry msn_soap_profile_get_parser[] = {
	{ "ExpressionProfile", "GetProfileResult", msn_soap_profile_get_result },
	{ "ResourceID",        "GetProfileResult", msn_soap_profile_get_rid },
	{ NULL,               NULL,     NULL                        }
};

static int msn_soap_profile_get_handle_response(struct msn_soap_req_data *soap_req)
{
	struct msn_data *md = soap_req->ic->proto_data;

	md->flags |= MSN_GOT_PROFILE;
	msn_ns_finish_login(soap_req->ic);

	return MSN_SOAP_OK;
}

static int msn_soap_profile_get_free_data(struct msn_soap_req_data *soap_req)
{
	g_free(soap_req->data);
	return 0;
}

int msn_soap_profile_get(struct im_connection *ic, const char *cid)
{
	return msn_soap_start(ic, g_strdup(cid),
	                      msn_soap_profile_get_build_request,
	                      msn_soap_profile_get_parser,
	                      msn_soap_profile_get_handle_response,
	                      msn_soap_profile_get_free_data);
}

/* Update profile (display name). */
static int msn_soap_profile_set_dn_build_request(struct msn_soap_req_data *soap_req)
{
	struct msn_data *md = soap_req->ic->proto_data;

	soap_req->url = g_strdup(SOAP_STORAGE_URL);
	soap_req->action = g_strdup(SOAP_PROFILE_SET_DN_ACTION);
	soap_req->payload = g_markup_printf_escaped(SOAP_PROFILE_SET_DN_PAYLOAD,
	                                            md->tokens[3], md->profile_rid, (char *) soap_req->data);

	return 1;
}

static const struct xt_handler_entry msn_soap_profile_set_dn_parser[] = {
	{ NULL,               NULL,     NULL                        }
};

static int msn_soap_profile_set_dn_handle_response(struct msn_soap_req_data *soap_req)
{
	return MSN_SOAP_OK;
}

static int msn_soap_profile_set_dn_free_data(struct msn_soap_req_data *soap_req)
{
	g_free(soap_req->data);
	return 0;
}

int msn_soap_profile_set_dn(struct im_connection *ic, const char *dn)
{
	return msn_soap_start(ic, g_strdup(dn),
	                      msn_soap_profile_set_dn_build_request,
	                      msn_soap_profile_set_dn_parser,
	                      msn_soap_profile_set_dn_handle_response,
	                      msn_soap_profile_set_dn_free_data);
}
