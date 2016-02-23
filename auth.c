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
	if (backend && !strcmp(backend, "pam")) {
		ok = 1;
	}
#endif
#ifdef WITH_LDAP
	gl = g_list_append(gl, &auth_ldap);
	if (backend && !strcmp(backend, "ldap")) {
		ok = 1;
	}
#endif

	return ok ? gl : NULL;
}

storage_status_t auth_check_pass(irc_t *irc, const char *nick, const char *password)
{
	GList *gl;
	storage_status_t status = storage_check_pass(irc, nick, password);

	if (status == STORAGE_CHECK_BACKEND) {
		for (gl = global.auth; gl; gl = gl->next) {
			auth_backend_t *be = gl->data;
			if (!strcmp(be->name, irc->auth_backend)) {
				status = be->check_pass(nick, password);
				break;
			}
		}
	} else if (status == STORAGE_NO_SUCH_USER && global.conf->auth_backend) {
		for (gl = global.auth; gl; gl = gl->next) {
			auth_backend_t *be = gl->data;
			if (!strcmp(be->name, global.conf->auth_backend)) {
				status = be->check_pass(nick, password);
				/* Save the user so storage_load will pick them up, similar to
				 * what the register command would do */
				if (status == STORAGE_OK) {
					irc->auth_backend = g_strdup(global.conf->auth_backend);
					storage_save(irc, (char *)password, 0);
				}
				break;
			}
		}
	}

	if (status == STORAGE_OK) {
		irc_setpass(irc, password);
	}

	return status;
}
