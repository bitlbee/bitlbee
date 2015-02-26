#include "bitlbee.h"
#include "bee.h"
#include "nogaim.h"
#include "parson.h"

static int next_rpc_id = 1;

struct rpc_plugin {
	struct sockaddr *addr;
	socklen_t addrlen;
};

struct rpc_connection {
	int fd;
	char *buf;
	int buflen;
};

struct rpc_groupchat {
	struct groupchat *bee_gc;
	char *rpc_handle;
};

static JSON_Value *rpc_out_new(const char *method, JSON_Array **params_) {
	JSON_Value *rpc = json_value_init_object();
	json_object_set_string(json_object(rpc), "method", method);
	json_object_set_number(json_object(rpc), "id", next_rpc_id++);

	JSON_Value *params = json_value_init_array();
	json_object_set_value(json_object(rpc), "params", params);

	if (params_)
		*params_ = json_array(params);

	return rpc;
}

#define RPC_OUT_INIT(method) \
	JSON_Array *params; \
	JSON_Value *rpc = rpc_out_new(method, &params);

/** Sends an RPC object. Takes ownership (i.e. frees it when done). */
static void rpc_send(struct im_connection *ic, JSON_Value *rpc) {
	char *buf = json_serialize_to_string(rpc);

	g_free(buf);
	json_value_free(rpc);
}

static JSON_Value *rpc_ser_account(const account_t *acc) {
	JSON_Value *v = json_value_init_object();
	JSON_Object *o = json_object(v);
	json_object_set_string(o, "user", acc->user);
	json_object_set_string(o, "pass", acc->user);
	json_object_set_string(o, "server", acc->server);
	return v;
}

static void rpc_init(account_t *acc) {
	// Add settings. Probably should not RPC at all.
}

static gboolean rpc_login_cb(gpointer data, gint fd, b_input_condition cond);
static gboolean rpc_in_event(gpointer data, gint fd, b_input_condition cond);

static void rpc_login(account_t *acc) {
	struct im_connection *ic = imcb_new(acc);
	struct rpc_connection *rd = ic->proto_data = g_new0(struct rpc_connection, 1);
	struct rpc_plugin *pd = acc->prpl->data;
	rd->fd = socket(pd->addr->sa_family, SOCK_STREAM, 0);
	sock_make_nonblocking(rd->fd);
	if (connect(rd->fd, pd->addr, pd->addrlen) == -1) {
		closesocket(rd->fd);
		return;
	}
	ic->inpa = b_input_add(rd->fd, B_EV_IO_WRITE, rpc_login_cb, ic);
}

static gboolean rpc_login_cb(gpointer data, gint fd, b_input_condition cond) {
	struct im_connection *ic = data;
	struct rpc_connection *rd = ic->proto_data;
	RPC_OUT_INIT("login");
	json_array_append_value(params, rpc_ser_account(ic->acc));
	rpc_send(ic, rpc);

	ic->inpa = b_input_add(rd->fd, B_EV_IO_READ, rpc_in_event, ic);

	return FALSE;
}

static void rpc_keepalive(struct im_connection *ic) {
	RPC_OUT_INIT("keepalive");
	rpc_send(ic, rpc);
}

static void rpc_logout(struct im_connection *ic) {
	RPC_OUT_INIT("logout");
	rpc_send(ic, rpc);
}

static int rpc_buddy_msg(struct im_connection *ic, char *to, char *message, int flags) {
	RPC_OUT_INIT("buddy_msg");
	json_array_append_string(params, to);
	json_array_append_string(params, message);
	json_array_append_number(params, flags);
	rpc_send(ic, rpc);

	return 1; // BOGUS
}

static void rpc_set_away(struct im_connection *ic, char *state, char *message) {
	RPC_OUT_INIT("set_away");
	json_array_append_string(params, state);
	json_array_append_string(params, message);
	rpc_send(ic, rpc);
}

static int rpc_send_typing(struct im_connection *ic, char *who, int flags) {
	RPC_OUT_INIT("send_typing");
	json_array_append_string(params, who);
	json_array_append_number(params, flags);
	rpc_send(ic, rpc);

	return 1; // BOGUS
}

static void rpc_add_buddy(struct im_connection *ic, char *name, char *group) {
	RPC_OUT_INIT("add_buddy");
	json_array_append_string(params, name);
	json_array_append_string(params, group);
	rpc_send(ic, rpc);
}

static void rpc_remove_buddy(struct im_connection *ic, char *name, char *group) {
	RPC_OUT_INIT("remove_buddy");
	json_array_append_string(params, name);
	json_array_append_string(params, group);
	rpc_send(ic, rpc);
}

static void rpc_add_permit(struct im_connection *ic, char *who) {
	RPC_OUT_INIT("add_permit");
	json_array_append_string(params, who);
	rpc_send(ic, rpc);
}

static void rpc_add_deny(struct im_connection *ic, char *who) {
	RPC_OUT_INIT("add_deny");
	json_array_append_string(params, who);
	rpc_send(ic, rpc);
}

static void rpc_rem_permit(struct im_connection *ic, char *who) {
	RPC_OUT_INIT("rem_permit");
	json_array_append_string(params, who);
	rpc_send(ic, rpc);
}

static void rpc_rem_deny(struct im_connection *ic, char *who) {
	RPC_OUT_INIT("rem_deny");
	json_array_append_string(params, who);
	rpc_send(ic, rpc);
}

static void rpc_get_info(struct im_connection *ic, char *who) {
	RPC_OUT_INIT("get_info");
	json_array_append_string(params, who);
	rpc_send(ic, rpc);
}

static void rpc_chat_invite(struct groupchat *gc, char *who, char *message) {
	RPC_OUT_INIT("chat_invite");
	json_array_append_string(params, who);
	json_array_append_string(params, message);
	rpc_send(gc->ic, rpc);
}

static void rpc_chat_kick(struct groupchat *gc, char *who, const char *message) {
	RPC_OUT_INIT("chat_kick");
	json_array_append_string(params, who);
	json_array_append_string(params, message);
	rpc_send(gc->ic, rpc);
}

static void rpc_chat_leave(struct groupchat *gc) {
	RPC_OUT_INIT("chat_leave");
	rpc_send(gc->ic, rpc);
}

static void rpc_chat_msg(struct groupchat *gc, char *msg, int flags) {
	RPC_OUT_INIT("chat_msg");	
	struct rpc_groupchat *data = gc->data;
	json_array_append_string(params, data->rpc_handle);
	json_array_append_string(params, msg);
	json_array_append_number(params, flags);
	rpc_send(gc->ic, rpc);
}

static struct groupchat *rpc_chat_with(struct im_connection *ic, char *who) {
	RPC_OUT_INIT("chat_with");
	json_array_append_string(params, who);
	rpc_send(ic, rpc);

	return NULL;
}

static struct groupchat *rpc_chat_join(struct im_connection *ic, const char *room, const char *nick,
                                       const char *password, set_t **sets) {
	RPC_OUT_INIT("chat_join");
	json_array_append_string(params, room);
	json_array_append_string(params, nick);
	json_array_append_string(params, password);
	//json_array_append_value(params, rpc_ser_sets(sets));
	rpc_send(ic, rpc);

	return NULL;
}

static void rpc_chat_topic(struct groupchat *gc, char *topic) {
	RPC_OUT_INIT("chat_topic");
	json_array_append_string(params, topic);
	rpc_send(gc->ic, rpc);
}

static void rpc_cmd_in(const char *cmd, JSON_Array *params) {

}

static void rpc_in(struct im_connection *ic, JSON_Object *rpc) {
	JSON_Object *res = json_object_get_object(rpc, "result");
	const char *cmd = json_object_get_string(rpc, "method");
	JSON_Value *id = json_object_get_value(rpc, "id");
	JSON_Array *params = json_object_get_array(rpc, "params");

	if ((!cmd && !res) || !id || (cmd && !params)) {
		imcb_log(ic, "Received invalid JSON-RPC object.");
		imc_logout(ic, TRUE);
		return;
	}

	if (res) {
		// handle response. I think I mostly won't.
	} else {
		rpc_cmd_in(cmd, params);
	}
}

static gboolean rpc_in_event(gpointer data, gint fd, b_input_condition cond) {
	struct im_connection *ic = data;
	struct rpc_connection *rd = ic->proto_data;
	char buf[2048];
	int st;

	while ((st = read(rd->fd, buf, sizeof(buf))) > 0) {
		rd->buf = g_realloc(rd->buf, rd->buflen + st + 1);
		memcpy(rd->buf + rd->buflen, buf, st);
	}

	if (st == 0 || (st == -1 && !sockerr_again())) {
		imcb_log(ic, "Read error");
		imc_logout(ic, TRUE);
		return FALSE;
	}
	rd->buf[rd->buflen] = '\0';

	JSON_Value *parsed;
	const char *end;
	while ((parsed = json_parse_first(rd->buf, &end))) {
		rpc_in(ic, json_object(parsed));
		json_value_free(parsed);

		if (end == rd->buf + rd->buflen) {
			g_free(rd->buf);
			rd->buf = NULL;
		} else {
			int newlen = rd->buf + rd->buflen - end;
			char new[newlen];
			memcpy(new, end, newlen);
			rd->buf = g_realloc(rd->buf, newlen + 1);
			memcpy(rd->buf, new, newlen);
			rd->buf[rd->buflen] = '\0';
		}
	}

	return TRUE;
}

static void rpc_imcb_buddy_typing(struct im_connection *ic, const char *cmd, JSON_Array *params) {
	const char *handle = json_array_get_string(params, 0);
	int flags = json_array_get_number(params, 1);
	imcb_buddy_typing(ic, handle, flags);
}

struct rpc_in_method {
	char *name;
	void (* func) (struct im_connection *ic, const char *cmd, JSON_Array *params);
	char *args;
};

#define RPC_ADD_FUNC(func) \
	if (g_hash_table_contains(methods, #func)) \
		ret->func = rpc_ ## func

void rpc_initmodule_sock(struct sockaddr *address, socklen_t addrlen) {
	int st, fd, i;

	fd = socket(address->sa_family, SOCK_STREAM, 0);
	if (fd == -1 || connect(fd, address, addrlen) == -1)
		return;

	RPC_OUT_INIT("init");
	JSON_Value *d = json_value_init_object();
	json_object_set_string(json_object(d), "version_str", BITLBEE_VERSION);
	json_object_set_number(json_object(d), "version", BITLBEE_VERSION_CODE);
	json_array_append_value(params, d);
	char *s = json_serialize_to_string(rpc);

	if ((st = write(fd, s, strlen(s))) != strlen(s)) {
		// LOG ERROR
		return;
	}
	g_free(s);

	char *resp = NULL;
	int buflen = 4096, resplen = 0;
	JSON_Value *parsed;
	do {
		fd_set rfds;
		struct timeval to;

		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		to.tv_sec = 1;
		to.tv_usec = 0;
		st = select(fd + 1, &rfds, NULL, NULL, &to);

		if (st == 0) {
			// LOG ERROR
			return;
		}
		
		if (resplen >= buflen)
			buflen *= 2;
		resp = g_realloc(resp, buflen + 1);
		st = read(fd, resp + resplen, buflen - resplen);
		if (st == -1) {
			if (sockerr_again())
				continue;
			// LOG ERROR
			return;
		}
		resplen += st;
		resp[resplen] = '\0';
	}
	while (!(parsed = json_parse_string(resp)));

	JSON_Object *isup = json_object_get_object(json_object(parsed), "result");
	if (isup == NULL) {
		// LOG ERROR
		return;
	}

	struct prpl *ret = g_new0(struct prpl, 1);
	
	JSON_Array *methods_a = json_object_get_array(isup, "methods");
	GHashTable *methods = g_hash_table_new(g_str_hash, g_str_equal);
	for (i = 0; i < json_array_get_count(methods_a); i++)
		g_hash_table_add(methods, (void*) json_array_get_string(methods_a, i));

	ret->init = rpc_init;
	RPC_ADD_FUNC(login);
	RPC_ADD_FUNC(keepalive);
	RPC_ADD_FUNC(logout);
	RPC_ADD_FUNC(buddy_msg);
	RPC_ADD_FUNC(set_away);
	RPC_ADD_FUNC(send_typing);
	RPC_ADD_FUNC(add_buddy);
	RPC_ADD_FUNC(remove_buddy);
	RPC_ADD_FUNC(add_permit);
	RPC_ADD_FUNC(add_deny);
	RPC_ADD_FUNC(rem_permit);
	RPC_ADD_FUNC(rem_deny);
	RPC_ADD_FUNC(get_info);
	RPC_ADD_FUNC(chat_invite);
	RPC_ADD_FUNC(chat_kick);
	RPC_ADD_FUNC(chat_leave);
	RPC_ADD_FUNC(chat_msg);
	RPC_ADD_FUNC(chat_with);
	RPC_ADD_FUNC(chat_join);
	RPC_ADD_FUNC(chat_topic);
	
	g_hash_table_destroy(methods);

	// TODO: Property for a few standard nickcmp implementations.
	
	JSON_Array *settings = json_object_get_array(isup, "settings");
	for (i = 0; i < json_array_get_count(settings); i++) {
		//JSON_Object *set = json_array_get_object(settings, i);
		// set..name, set..type, set..default, set..flags ?
	}

	ret->name = g_strdup(json_object_get_string(isup, "name"));

	struct rpc_plugin *proto_data = g_new0(struct rpc_plugin, 1);
	proto_data->addr = g_memdup(address, addrlen);
	proto_data->addrlen = addrlen;
	ret->data = proto_data;

	register_protocol(ret);
}

void rpc_initmodule() {
}

