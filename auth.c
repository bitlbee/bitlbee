#define BITLBEE_CORE
#include "bitlbee.h"

#ifdef WITH_PAM
extern auth_backend_t auth_pam;
#endif
#ifdef WITH_LDAP
extern auth_backend_t auth_ldap;
#endif

GList *auth_init(const char *backend)
{
	GList *gl = NULL;
	int ok = backend ? 0 : 1;
#ifdef WITH_PAM
	gl = g_list_append(gl, &auth_pam);
	if (backend && !strcmp(backend, "pam"))
		ok = 1;
#endif
#ifdef WITH_LDAP
	gl = g_list_append(gl, &auth_ldap);
	if (backend && !strcmp(backend, "ldap"))
		ok = 1;
#endif

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
