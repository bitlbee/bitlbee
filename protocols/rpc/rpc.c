#include <sys/socket.h>
#include <sys/un.h>

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
	GHashTable *groupchats;
};

struct rpc_groupchat {
	int id;
	struct groupchat *gc;
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
static gboolean rpc_send(struct im_connection *ic, JSON_Value *rpc) {
	struct rpc_connection *rd = ic->proto_data;
	char *buf = json_serialize_to_string(rpc);
	int len = strlen(buf);
	int st;

	buf = g_realloc(buf, len + 3);
	strcpy(buf + len, "\r\n");
	len += 2;

	st = write(rd->fd, buf, len);
	g_free(buf);
	json_value_free(rpc);

	if (st != len) {
		imcb_log(ic, "Write error");
		imc_logout(ic, TRUE);
		return FALSE;
	}

	return TRUE;
}

static JSON_Value *rpc_ser_account(const account_t *acc) {
	JSON_Value *v = json_value_init_object();
	JSON_Object *o = json_object(v);
	json_object_set_string(o, "user", acc->user);
	json_object_set_string(o, "pass", acc->user);
	if (acc->server)
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
	rd->groupchats = g_hash_table_new(g_int_hash, g_int_equal);
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
	if (!rpc_send(ic, rpc))
		return;

	struct rpc_connection *rd = ic->proto_data;
	b_event_remove(ic->inpa);
	closesocket(rd->fd);
	g_free(rd->buf);
	g_hash_table_destroy(rd->groupchats);
	g_free(rd);
}

static int rpc_buddy_msg(struct im_connection *ic, char *to, char *message, int flags) {
	RPC_OUT_INIT("buddy_msg");
	json_array_append_string(params, to);
	json_array_append_string(params, message);
	json_array_append_number(params, flags);
	return rpc_send(ic, rpc);
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
	return rpc_send(ic, rpc);
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
	struct rpc_groupchat *rc = gc->data;
	json_array_append_number(params, rc->id);
	json_array_append_string(params, who);
	json_array_append_string(params, message);
	rpc_send(gc->ic, rpc);
}

static void rpc_chat_kick(struct groupchat *gc, char *who, const char *message) {
	RPC_OUT_INIT("chat_kick");
	struct rpc_groupchat *rc = gc->data;
	json_array_append_number(params, rc->id);
	json_array_append_string(params, who);
	json_array_append_string(params, message);
	rpc_send(gc->ic, rpc);
}

static void rpc_chat_leave(struct groupchat *gc) {
	RPC_OUT_INIT("chat_leave");
	struct rpc_groupchat *rc = gc->data;
	json_array_append_number(params, rc->id);
	rpc_send(gc->ic, rpc);
}

static void rpc_chat_msg(struct groupchat *gc, char *msg, int flags) {
	RPC_OUT_INIT("chat_msg");	
	struct rpc_groupchat *rc = gc->data;
	json_array_append_number(params, rc->id);
	json_array_append_string(params, msg);
	json_array_append_number(params, flags);
	rpc_send(gc->ic, rpc);
}

static struct groupchat *rpc_groupchat_new(struct im_connection *ic, const char *handle) {
	struct groupchat *gc = imcb_chat_new(ic, handle);
	struct rpc_groupchat *rc = gc->data = g_new0(struct rpc_groupchat, 1);
	rc->id = next_rpc_id;
	rc->gc = gc;
	return gc;  // TODO: RETVAL HERE AND BELOW
}

static struct groupchat *rpc_chat_with(struct im_connection *ic, char *who) {
	RPC_OUT_INIT("chat_with");
	struct groupchat *gc = rpc_groupchat_new(ic, who);
	struct rpc_groupchat *rc = gc->data;
	json_array_append_number(params, rc->id);
	json_array_append_string(params, who);
	rpc_send(ic, rpc);

	return gc; 
}

static struct groupchat *rpc_chat_join(struct im_connection *ic, const char *room, const char *nick,
                                       const char *password, set_t **sets) {
	RPC_OUT_INIT("chat_join");
	struct groupchat *gc = rpc_groupchat_new(ic, room);
	struct rpc_groupchat *rc = gc->data;
	json_array_append_number(params, rc->id);
	json_array_append_string(params, room);
	json_array_append_string(params, nick);
	json_array_append_string(params, password);
	//json_array_append_value(params, rpc_ser_sets(sets));
	rpc_send(ic, rpc);

	return gc;
}

static void rpc_chat_topic(struct groupchat *gc, char *topic) {
	RPC_OUT_INIT("chat_topic");
	struct rpc_groupchat *rc = gc->data;
	json_array_append_number(params, rc->id);
	json_array_append_string(params, topic);
	rpc_send(gc->ic, rpc);
}

static gboolean rpc_cmd_in(struct im_connection *ic, const char *cmd, JSON_Array *params);

static gboolean rpc_in(struct im_connection *ic, JSON_Object *rpc) {
	JSON_Object *res = json_object_get_object(rpc, "result");
	const char *cmd = json_object_get_string(rpc, "method");
	JSON_Value *id = json_object_get_value(rpc, "id");
	JSON_Array *params = json_object_get_array(rpc, "params");

	if ((!cmd && !res) || !id || (cmd && !params)) {
		imcb_log(ic, "Received invalid JSON-RPC object.");
		imc_logout(ic, TRUE);
		return FALSE;
	}

	if (res) {
		// handle response. I think I mostly won't.
		return TRUE;
	} else {
		gboolean st = rpc_cmd_in(ic, cmd, params);
		JSON_Value *resp = json_value_init_object();
		json_object_set_value(json_object(resp), "id", json_value_deep_copy(id));
		if (st)
			json_object_set_value(json_object(resp), "result", json_value_init_object());
		else
			json_object_set_value(json_object(resp), "error", json_value_init_object());
		return rpc_send(ic, resp);
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
		rd->buflen += st;
	}

	if (st == 0 || (st == -1 && !(sockerr_again() || errno == EAGAIN))) {
		imcb_log(ic, "Read error");
		imc_logout(ic, TRUE);
		return FALSE;
	}
	rd->buf[rd->buflen] = '\0';

	JSON_Value *parsed;
	const char *end;
	while ((parsed = json_parse_first(rd->buf, &end))) {
		st = rpc_in(ic, json_object(parsed));
		json_value_free(parsed);

		if (!st)
			return FALSE;

		if (end == rd->buf + rd->buflen) {
			g_free(rd->buf);
			rd->buf = NULL;
		} else {
			int newlen = rd->buf + rd->buflen - end;
			char new[newlen];
			memcpy(new, end, newlen);
			rd->buf = g_realloc(rd->buf, newlen + 1);
			memcpy(rd->buf, new, newlen);
			rd->buflen = newlen;
			rd->buf[rd->buflen] = '\0';
		}
	}

	return TRUE;
}

static void rpc_imcb_log(struct im_connection *ic, void *func_, JSON_Array *params) {
	void (*func)(struct im_connection*, char*, ...) = func_;
	func(ic, "%s", json_array_get_string(params, 0));
}

static void rpc_imcb_connected(struct im_connection *ic, void *func_, JSON_Array *params) {
	void (*func)(struct im_connection*) = func_;
	func(ic);
}

static void rpc_imc_logout(struct im_connection *ic, void *func_, JSON_Array *params) {
	void (*func)(struct im_connection*, gboolean) = func_;
	func(ic, json_array_get_boolean(params, 0));
}

static void rpc_imcb_add_buddy(struct im_connection *ic, void *func_, JSON_Array *params) {
	void (*func)(struct im_connection*, const char*, const char*) = func_;
	func(ic, json_array_get_string(params, 0), json_array_get_string(params, 1));
}

static void rpc_imcb_buddy_status(struct im_connection *ic, void *func_, JSON_Array *params) {
	void (*func)(struct im_connection*, const char*, int, const char*, const char*) = func_;
	func(ic, json_array_get_string(params, 0), json_array_get_number(params, 1),
	         json_array_get_string(params, 2), json_array_get_string(params, 3));
}

static void rpc_imcb_buddy_times(struct im_connection *ic, void *func_, JSON_Array *params) {
	void (*func)(struct im_connection*, const char*, int, int) = func_;
	func(ic, json_array_get_string(params, 0), json_array_get_number(params, 1),
	         json_array_get_number(params, 2));
}

static void rpc_imcb_buddy_msg(struct im_connection *ic, void *func_, JSON_Array *params) {
	void (*func)(struct im_connection*, const char*, const char*, int, int) = func_;
	func(ic, json_array_get_string(params, 0), json_array_get_string(params, 1),
	         json_array_get_number(params, 2), json_array_get_number(params, 3));
}

static void rpc_imcb_buddy_typing(struct im_connection *ic, void *func_, JSON_Array *params) {
	void (*func)(struct im_connection*, char*, int) = func_;
	func(ic, (char*) json_array_get_string(params, 0), json_array_get_number(params, 1));
}

struct rpc_in_method {
	char *name;
	void *func;
	void (* wfunc) (struct im_connection *ic, void *cmd, JSON_Array *params);
	char args[8];
};

static const struct rpc_in_method methods[] = {
	{ "imcb_log", imcb_log, rpc_imcb_log, "s" },
	{ "imcb_error", imcb_error, rpc_imcb_log, "s" },
	{ "imcb_connected", imcb_connected, rpc_imcb_connected, "" },
	{ "imc_logout", imc_logout, rpc_imc_logout, "b" },
	{ "imcb_add_buddy", imcb_add_buddy, rpc_imcb_add_buddy, "ss" },
	{ "imcb_remove_buddy", imcb_remove_buddy, rpc_imcb_add_buddy, "ss" },
	{ "imcb_rename_buddy", imcb_rename_buddy, rpc_imcb_add_buddy, "ss" },
	{ "imcb_buddy_nick_hint", imcb_buddy_nick_hint, rpc_imcb_add_buddy, "ss" },
	{ "imcb_buddy_status", imcb_buddy_status, rpc_imcb_buddy_status, "snss" },
	{ "imcb_buddy_status_msg", imcb_buddy_status_msg, rpc_imcb_add_buddy, "ss" },
	{ "imcb_buddy_times", imcb_buddy_times, rpc_imcb_buddy_times, "snn" },
	{ "imcb_buddy_msg", imcb_buddy_msg, rpc_imcb_buddy_msg, "ssnn" },
	{ "imcb_buddy_typing", imcb_buddy_typing, rpc_imcb_buddy_typing, "sn" },
	{ NULL },
};

static gboolean rpc_cmd_in(struct im_connection *ic, const char *cmd, JSON_Array *params) {
	int i;

	for (i = 0; methods[i].name; i++) {
		if (strcmp(cmd, methods[i].name) == 0) {
			if (json_array_get_count(params) != strlen(methods[i].args)) {
				imcb_error(ic, "Invalid argument count to method %s: %d, wanted %zd", cmd, (int) json_array_get_count(params), strlen(methods[i].args));
				return FALSE;
			}
			int j;
			for (j = 0; methods[i].args[j]; j++) {
				JSON_Value_Type type = json_value_get_type(json_array_get_value(params, j));
				gboolean ok = FALSE;
				switch (methods[i].args[j]) {
				case 's':
					ok = type == JSONString;
					break;
				case 'n':
					ok = type == JSONNumber;
					break;
				case 'o':
					ok = type == JSONObject;
					break;
				case 'a':
					ok = type == JSONArray;
					break;
				case 'b':
					ok = type == JSONBoolean;
					break;
				}
				if (!ok) {
					// This error sucks, but just get your types right!
					imcb_error(ic, "Invalid argument type, %s parameter %d: %d not %c", cmd, j, type, methods[i].args[j]);
					return FALSE;
				}
			}
			methods[i].wfunc(ic, methods[i].func, params);
			return TRUE;
		}
	}
	return FALSE;
}

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
	int len = strlen(s);
	s = g_realloc(s, len + 3);
	strcpy(s + len, "\r\n");
	len += 2;

	if ((st = write(fd, s, len)) != len) {
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
			closesocket(fd);
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
			closesocket(fd);
			return;
		}
		resplen += st;
		resp[resplen] = '\0';
	}
	while (!(parsed = json_parse_string(resp)));
	closesocket(fd);

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

#define PDIR "/tmp/rpcplugins"

/* YA RLY :-/ */
#ifndef UNIX_PATH_MAX
struct sockaddr_un sizecheck;
#define UNIX_PATH_MAX sizeof(sizecheck.sun_path)
#endif

void rpc_initmodule() {
	DIR *pdir = opendir(PDIR);
	struct dirent *de;

	if (!pdir)
		return;

	while ((de = readdir(pdir))) {
		char *fn = g_build_filename(PDIR, de->d_name, NULL);
		struct sockaddr_un su;

		strncpy(su.sun_path, fn, UNIX_PATH_MAX);
		su.sun_path[UNIX_PATH_MAX-1] = '\0';
		su.sun_family = AF_UNIX;
		rpc_initmodule_sock((struct sockaddr*) &su, sizeof(su));
		g_free(fn);
	}
}

