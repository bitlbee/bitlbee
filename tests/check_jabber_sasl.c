#include <stdlib.h>
#include <glib.h>
#include <gmodule.h>
#include <check.h>
#include <string.h>
#include <stdio.h>

char *sasl_get_part(char *data, char *field);

#define challenge1 "nonce=\"1669585310\",qop=\"auth\",charset=utf-8,algorithm=md5-sess," \
	"something=\"Not \\\"standardized\\\"\""
#define challenge2 "realm=\"quadpoint.org\", nonce=\"NPotlQpQf9RNYodOwierkQ==\", " \
	"qop=\"auth, auth-int\", charset=utf-8, algorithm=md5-sess"
#define challenge3 ", realm=\"localhost\", nonce=\"LlBV2txnO8RbB5hgs3KgiQ==\", " \
	"qop=\"auth, auth-int, \", ,\n, charset=utf-8, algorithm=md5-sess,"

struct {
	char *challenge;
	char *key;
	char *value;
} get_part_tests[] = {
	{
		challenge1,
		"nonce",
		"1669585310"
	},
	{
		challenge1,
		"charset",
		"utf-8"
	},
	{
		challenge1,
		"harset",
		NULL
	},
	{
		challenge1,
		"something",
		"Not \"standardized\""
	},
	{
		challenge1,
		"something_else",
		NULL
	},
	{
		challenge2,
		"realm",
		"quadpoint.org",
	},
	{
		challenge2,
		"real",
		NULL
	},
	{
		challenge2,
		"qop",
		"auth, auth-int"
	},
	{
		challenge3,
		"realm",
		"localhost"
	},
	{
		challenge3,
		"qop",
		"auth, auth-int, "
	},
	{
		challenge3,
		"charset",
		"utf-8"
	},
	{ NULL, NULL, NULL }
};

START_TEST(check_get_part)
{
	int i;

	for (i = 0; get_part_tests[i].key; i++) {
		tcase_fn_start(get_part_tests[i].key, __FILE__, i);
		char *res;

		res = sasl_get_part(get_part_tests[i].challenge,
		                    get_part_tests[i].key);

		if (get_part_tests[i].value == NULL) {
			fail_if(res != NULL, "Found key %s in %s while it shouldn't be there!",
			        get_part_tests[i].key, get_part_tests[i].challenge);
		} else if (res) {
			fail_unless(strcmp(res, get_part_tests[i].value) == 0,
			            "Incorrect value for key %s in %s: %s",
			            get_part_tests[i].key, get_part_tests[i].challenge, res);
		} else {
			fail("Could not find key %s in %s",
			     get_part_tests[i].key, get_part_tests[i].challenge);
		}

		g_free(res);
	}
}
END_TEST

Suite *jabber_sasl_suite(void)
{
	Suite *s = suite_create("jabber/sasl");
	TCase *tc_core = tcase_create("Core");

	suite_add_tcase(s, tc_core);
	tcase_add_test(tc_core, check_get_part);
	return s;
}
