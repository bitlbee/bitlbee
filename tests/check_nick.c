#include <stdlib.h>
#include <glib.h>
#include <gmodule.h>
#include <check.h>
#include <string.h>
#include "irc.h"
#include "set.h"
#include "misc.h"
#include "bitlbee.h"

START_TEST(test_nick_strip){
	int i;
	const char *get[] = { "test:", "test", "test\n",
		              "thisisaveryveryveryverylongnick",
		              "thisisave:ryveryveryverylongnick",
		              "t::::est",
		              "test123",
		              "123test",
		              "123",
		              NULL };
	const char *expected[] = { "test", "test", "test",
		                   "thisisaveryveryveryveryl",
		                   "thisisaveryveryveryveryl",
		                   "test",
		                   "test123",
		                   "_123test",
		                   "_123",
		                   NULL };

	for (i = 0; get[i]; i++) {
		char copy[60];
		strcpy(copy, get[i]);
		nick_strip(NULL, copy);
		fail_unless(strcmp(copy, expected[i]) == 0,
		            "(%d) nick_strip broken: %s -> %s (expected: %s)",
		            i, get[i], copy, expected[i]);
	}
}
END_TEST

START_TEST(test_nick_ok_ok)
{
	const char *nicks[] = { "foo", "bar123", "bla[", "blie]", "BreEZaH",
		                "\\od^~", "_123", "_123test", NULL };
	int i;

	for (i = 0; nicks[i]; i++) {
		fail_unless(nick_ok(NULL, nicks[i]) == 1,
		            "nick_ok() failed: %s", nicks[i]);
	}
}
END_TEST

START_TEST(test_nick_ok_notok)
{
	const char *nicks[] = { "thisisaveryveryveryveryveryveryverylongnick",
		                "\nillegalchar", "", "nick%", "123test", NULL };
	int i;

	for (i = 0; nicks[i]; i++) {
		fail_unless(nick_ok(NULL, nicks[i]) == 0,
		            "nick_ok() succeeded for invalid: %s", nicks[i]);
	}
}
END_TEST

Suite *nick_suite(void)
{
	Suite *s = suite_create("Nick");
	TCase *tc_core = tcase_create("Core");

	suite_add_tcase(s, tc_core);
	tcase_add_test(tc_core, test_nick_ok_ok);
	tcase_add_test(tc_core, test_nick_ok_notok);
	tcase_add_test(tc_core, test_nick_strip);
	return s;
}
