#include <stdlib.h>
#include <glib.h>
#include <gmodule.h>
#include <check.h>
#include <string.h>
#include "bitlbee.h"
#include "crypting.h"
#include "testsuite.h"

START_TEST(test_check_pass_valid)
	fail_unless (checkpass ("foo", "acbd18db4cc2f85cedef654fccc4a4d8") == 0);
	fail_unless (checkpass ("invalidpass", "acbd18db4cc2f85cedef654fccc4a4d8") == -1);

END_TEST

START_TEST(test_hashpass)
	fail_unless (strcmp(hashpass("foo"), "acbd18db4cc2f85cedef654fccc4a4d8") == 0);
END_TEST

START_TEST(test_obfucrypt)
	char *raw = obfucrypt("some line", "bla");
	fail_unless(strcmp(raw, "\xd5\xdb\xce\xc7\x8c\xcd\xcb\xda\xc6") == 0);
END_TEST

START_TEST(test_deobfucrypt)
	char *raw = deobfucrypt("\xd5\xdb\xce\xc7\x8c\xcd\xcb\xda\xc6", "bla");
	fail_unless(strcmp(raw, "some line") == 0);
END_TEST

START_TEST(test_obfucrypt_bidirectional)
	char *plain = g_strdup("this is a line");
	char *raw = obfucrypt(plain, "foo");
	fail_unless(strcmp(plain, deobfucrypt(raw, "foo")) == 0);
END_TEST

Suite *crypting_suite (void)
{
	Suite *s = suite_create("Crypting");
	TCase *tc_core = tcase_create("Core");
	suite_add_tcase (s, tc_core);
	tcase_add_test (tc_core, test_check_pass_valid);
	tcase_add_test (tc_core, test_hashpass);
	tcase_add_test (tc_core, test_obfucrypt);
	tcase_add_test (tc_core, test_deobfucrypt);
	tcase_add_test (tc_core, test_obfucrypt_bidirectional);
	return s;
}
