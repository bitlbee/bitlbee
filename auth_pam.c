#define BITLBEE_CORE
#include "bitlbee.h"
#include <security/pam_appl.h>

#define PAM_CHECK(x) do { \
	ret = (x); \
	if(ret != PAM_SUCCESS) { \
		pam_func = #x; \
		goto pam_error; \
	} \
} while(0)

/* This function fills in the password when PAM asks for it */
int pamconv(int num_msg, const struct pam_message **msg, struct pam_response **resp, void *appdata_ptr) {
	int i;
	struct pam_response *rsp = g_new0(struct pam_response, num_msg);

	for (i = 0; i < num_msg; i++) {
		rsp[i].resp = NULL;
		rsp[i].resp_retcode = 0;
		if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF) {
			rsp[i].resp = g_strdup((char *)appdata_ptr);
		}
	}
	*resp = rsp;
	return PAM_SUCCESS;
}

static storage_status_t pam_check_pass(const char *nick, const char *password)
{
	int ret;
	const struct pam_conv pamc = { pamconv, (void*) password };
	pam_handle_t *pamh = NULL;
	char *pam_func;

	PAM_CHECK(pam_start("bitlbee", nick, &pamc, &pamh));
	PAM_CHECK(pam_authenticate(pamh, PAM_DISALLOW_NULL_AUTHTOK));
	PAM_CHECK(pam_acct_mgmt(pamh, 0));

	pam_end(pamh, ret);
	return STORAGE_OK;

pam_error:
	switch (ret) {
		case PAM_AUTH_ERR:
			pam_end(pamh, ret);
			return STORAGE_INVALID_PASSWORD;
		case PAM_USER_UNKNOWN:
		case PAM_PERM_DENIED:
			pam_end(pamh, ret);
			return STORAGE_NO_SUCH_USER;
		default:
			log_message(LOGLVL_WARNING, "%s failed: %s", pam_func, pam_strerror(pamh, ret));
			pam_end(pamh, ret);
			return STORAGE_OTHER_ERROR;
	}
}

auth_backend_t auth_pam = {
	.name = "pam",
	.check_pass = pam_check_pass,
};
