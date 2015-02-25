
static int next_rpc_id = 1;

struct rpc_protocol {
	struct sockaddr *addr;
	socklen_t addrlen;
}

struct rpc_connection {
	int fd;
	char *buf;
}

struct rpc_groupchat {
	struct groupchat *bee_gc;
	char *rpc_handle;
}

static JSON_Value *rpc_out_new(const char *method, JSON_Array **params_) {
	JSON_Value *rpc = json_value_init_object();
	json_set_string(rpc, "method", "chat_msg");
	json_set_number(rpc, "id", next_rpc_id++);

	JSON_Array *params = json_value_init_array();
	json_object_set_value(rpc, "params", params);

	if (params_)
		*params_ = json_array(params);
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

static JSON_Object *rpc_ser_account(const account_t *acc) {
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

static void rpc_login(account_t *acc) {
	struct im_connection *ic = imcb_new(acc);
	RPC_OUT_INIT("login");
	json_array_append_value(params, rpc_ser_account(acc));
	rpc_send(gc->ic, rpc);
	// Create im
}

static void rpc_chat_msg(struct groupchat *gc, char *msg, int flags) {
	RPC_OUT_INIT("chat_msg");	
	struct rpc_groupchat *data = gc->proto_data;
	json_array_append_string(params, data->rpc_handle);
	json_array_append_string(params, msg);
	json_array_append_number(params, flags);
	rpc_send(gc->ic, rpc);
}

static void rpc_in(JSON_Object *rpc) {
	JSON_Object *res = json_object_get_object(rpc, "result");
	const char *cmd = json_object_get_string(rpc, "method");
	JSON_Value *Value *id = json_object_get_value(rpc, "id");
	JSON_Array *params *params = json_object_get_array(rpc, "params");

	if ((!cmd && !res) || !id || (cmd && !params)) {
		imcb_logout(ic, "Received invalid JSON-RPC object.");
		return;
	}

	if (res) {
		// handle response. I think I mostly won't.
	} else {
		rpc_cmd_in(cmd, params);
	}
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
}

static void rpc_cmd_in(const char *cmd, JSON_Array *params) {

}

#define RPC_ADD_FUNC(func) \
	if (g_hash_table_contains(methods, #func)) \
		ret->func = rpc_ # func

void rpc_initmodule_sock(struct sockaddr *address, socklen addrlen) {
	int st, fd, i;

	fd = socket(address->ss_family, SOCK_STREAM);
	if (fd == -1 || connect(fd, address, addrlen) == -1)
		return;

	RPC_OUT_INIT("init");
	JSON_Value *d = json_value_init_object();
	json_object_set_string(json_object(d), "version_str", BITLBEE_VERSION);
	json_object_set_number(json_object(d), "version", BITLBEE_VERSION_CODE);
	json_array_append_value(d);
	char *s = json_serialize_to_string(rpc);

	if ((st = write(fd, s, strlen(s))) != strlen(s)) {
		// LOG ERROR
		return;
	}
	g_free(s);

	char *resp = NULL;
	int bufsize = 4096, resplen = 0;
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
		
		if (resplen >= bufsize)
			bufsize *= 2;
		resp = g_realloc(resp, bufsize + 1);
		st = read(fd, resp + resplen, bufsize - resplen);
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
	GHashTable *methods = g_hash_table_new();
	int i;
	for (i = 0; i < json_array_get_count(methods_a); i++)
		g_hash_table_add(methods, json_array_get_string(methods_a, i));

	RPC_ADD_FUNC(login);
	RPC_ADD_FUNC(chat_msg);
	
	g_hash_table_free(methods);

	// TODO: Property for a few standard nickcmp implementations.
	
	JSON_Array *settings = json_object_get_array(isup, "settings");
	for (i = 0; i < json_array_get_count(settings); i++) {
		JSON_Object *set = json_array_get_object(settings, i);
		// set..name, set..type, set..default, set..flags ?
	}

	ret->name = g_strdup(json_object_get_string(isup, "name"));

	struct rpc_protocol *proto_data = g_new0(struct rpc_protocol, 1);
	proto_data->addr = g_memdup(address, addrlen);
	proto_data->addrlen = addrlen;
	ret->data = proto_data;

	register_protocol(ret);
}

void rpc_initmodule() {
}

