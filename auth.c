#define BITLBEE_CORE
#include "bitlbee.h"

GList *auth_init(const char *backend)
{
	GList *gl = NULL;
	int ok = backend ? 0 : 1;

	return ok ? gl : NULL;
}

storage_status_t auth_check_pass(const char *backend, const char *nick, const char *password)
{
	GList *gl;
	for (gl = global.auth; gl; gl = gl->next) {
		auth_backend_t *be = gl->data;
		if (!strcmp(be->name, backend))
			return be->check_pass(nick, password);
	}
	return STORAGE_OTHER_ERROR;
}
