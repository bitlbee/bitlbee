#include <stdlib.h>
#include <glib.h>
#include <gmodule.h>
#include <check.h>
#include <string.h>
#include "bitlbee.h"
#include "testsuite.h"

#if 0
START_TEST(test_user_add)
irc_t * irc = torture_irc();
user_t *user;
user = user_add(irc, "foo");
fail_if(user == NULL);
fail_if(strcmp(user->nick, "foo") != 0);
fail_unless(user_find(irc, "foo") == user);
END_TEST

START_TEST(test_user_add_exists)
irc_t * irc = torture_irc();
user_t *user;
user = user_add(irc, "foo");
fail_if(user == NULL);
user = user_add(irc, "foo");
fail_unless(user == NULL);
END_TEST

START_TEST(test_user_add_invalid)
irc_t * irc = torture_irc();
user_t *user;
user = user_add(irc, ":foo");
fail_unless(user == NULL);
END_TEST

START_TEST(test_user_del_invalid)
irc_t * irc = torture_irc();
fail_unless(user_del(irc, ":foo") == 0);
END_TEST

START_TEST(test_user_del)
irc_t * irc = torture_irc();
user_t *user;
user = user_add(irc, "foo");
fail_unless(user_del(irc, "foo") == 1);
fail_unless(user_find(irc, "foo") == NULL);
END_TEST

START_TEST(test_user_del_nonexistent)
irc_t * irc = torture_irc();
fail_unless(user_del(irc, "foo") == 0);
END_TEST

START_TEST(test_user_rename)
irc_t * irc = torture_irc();
user_t *user;
user = user_add(irc, "foo");
user_rename(irc, "foo", "bar");
fail_unless(user_find(irc, "foo") == NULL);
fail_if(user_find(irc, "bar") == NULL);
END_TEST
#endif
Suite *user_suite(void)
{
	Suite *s = suite_create("User");
	TCase *tc_core = tcase_create("Core");

	suite_add_tcase(s, tc_core);
#if 0
	tcase_add_test(tc_core, test_user_add);
	tcase_add_test(tc_core, test_user_add_invalid);
	tcase_add_test(tc_core, test_user_add_exists);
	tcase_add_test(tc_core, test_user_del_invalid);
	tcase_add_test(tc_core, test_user_del_nonexistent);
	tcase_add_test(tc_core, test_user_del);
	tcase_add_test(tc_core, test_user_rename);
#endif
	return s;
}
