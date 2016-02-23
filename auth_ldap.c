#define BITLBEE_CORE
#define LDAP_DEPRECATED 1
#include "bitlbee.h"
#include <ldap.h>

static storage_status_t ldap_check_pass(const char *nick, const char *password)
{
	LDAP *ldap;
	LDAPMessage *msg, *entry;
	char *dn = NULL;
	char *filter;
	char *attrs[1] = { NULL };
	int ret, count;

	if((ret = ldap_initialize(&ldap, NULL)) != LDAP_SUCCESS) {
		log_message(LOGLVL_WARNING, "ldap_initialize failed: %s", ldap_err2string(ret));
		return STORAGE_OTHER_ERROR;
	}

	/* First we do an anonymous bind to map uid=$nick to a DN*/
	if((ret = ldap_simple_bind_s(ldap, NULL, NULL)) != LDAP_SUCCESS) {
		ldap_unbind_s(ldap);
		log_message(LOGLVL_WARNING, "Anonymous bind failed: %s", ldap_err2string(ret));
		return STORAGE_OTHER_ERROR;
	}


	/* We search and process the result */
	filter = g_strdup_printf("(uid=%s)", nick);
	ret = ldap_search_ext_s(ldap, NULL, LDAP_SCOPE_SUBTREE, filter, attrs, 0, NULL, NULL, NULL, 1, &msg);
	g_free(filter);

	if(ret != LDAP_SUCCESS) {
		ldap_unbind_s(ldap);
		log_message(LOGLVL_WARNING, "uid search failed: %s", ldap_err2string(ret));
		return STORAGE_OTHER_ERROR;
	}

	count = ldap_count_entries(ldap, msg);
	if (count == -1) {
		ldap_get_option(ldap, LDAP_OPT_ERROR_NUMBER, &ret);
		ldap_msgfree(msg);
		ldap_unbind_s(ldap);
		log_message(LOGLVL_WARNING, "uid search failed: %s", ldap_err2string(ret));
		return STORAGE_OTHER_ERROR;
	}

	if (!count) {
		ldap_msgfree(msg);
		ldap_unbind_s(ldap);
		return STORAGE_NO_SUCH_USER;
	}

	entry = ldap_first_entry(ldap, msg);
	dn = ldap_get_dn(ldap, entry);
	ldap_msgfree(msg);

	/* And now we bind as the user to authenticate */
	ret = ldap_simple_bind_s(ldap, dn, password);
	g_free(dn);
	ldap_unbind_s(ldap);

	switch (ret) {
		case LDAP_SUCCESS:
			return STORAGE_OK;
		case LDAP_INVALID_CREDENTIALS:
			return STORAGE_INVALID_PASSWORD;
		default:
			log_message(LOGLVL_WARNING, "Authenticated bind failed: %s", ldap_err2string(ret));
			return STORAGE_OTHER_ERROR;
	}
}

auth_backend_t auth_ldap = {
	.name = "ldap",
	.check_pass = ldap_check_pass,
};
