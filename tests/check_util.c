#include <stdlib.h>
#include <glib.h>
#include <gmodule.h>
#include <check.h>
#include <string.h>
#include "irc.h"
#include "set.h"
#include "util.h"

START_TEST(test_strip_linefeed)
{
	int i;
	const char *get[] = { "Test", "Test\r", "Test\rX\r", NULL };
	const char *expected[] = { "Test", "Test", "TestX", NULL };

	for (i = 0; get[i]; i++) {
		char copy[20];
		strcpy(copy, get[i]);
		strip_linefeed(copy);
		fail_unless (strcmp(copy, expected[i]) == 0, 
					 "(%d) strip_linefeed broken: %s -> %s (expected: %s)", 
					 i, get[i], copy, expected[i]);
	}
}
END_TEST

Suite *util_suite (void)
{
	Suite *s = suite_create("Util");
	TCase *tc_core = tcase_create("Core");
	suite_add_tcase (s, tc_core);
	tcase_add_test (tc_core, test_strip_linefeed);
	return s;
}
