#include <stdlib.h>
#include <glib.h>
#include <gmodule.h>
#include <check.h>
#include <string.h>
#include "irc.h"
#include "set.h"
#include "misc.h"
#include "url.h"

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

START_TEST(test_strip_newlines)
{
	int i;
	const char *get[] = { "Test", "Test\r\n", "Test\nX\n", NULL };
	const char *expected[] = { "Test", "Test  ", "Test X ", NULL };

	for (i = 0; get[i]; i++) {
		char copy[20], *ret;
		strcpy(copy, get[i]);
		ret = strip_newlines(copy);
		fail_unless (strcmp(copy, expected[i]) == 0, 
					 "(%d) strip_newlines broken: %s -> %s (expected: %s)", 
					 i, get[i], copy, expected[i]);
		fail_unless (copy == ret, "Original string not returned"); 
	}
}
END_TEST

START_TEST(test_set_url_http)
	url_t url;
	
	fail_if (0 == url_set(&url, "http://host/"));
	fail_unless (!strcmp(url.host, "host"));
	fail_unless (!strcmp(url.file, "/"));
	fail_unless (!strcmp(url.user, ""));
	fail_unless (!strcmp(url.pass, ""));
	fail_unless (url.proto == PROTO_HTTP);
	fail_unless (url.port == 80);
END_TEST

START_TEST(test_set_url_https)
	url_t url;
	
	fail_if (0 == url_set(&url, "https://ahost/AimeeMann"));
	fail_unless (!strcmp(url.host, "ahost"));
	fail_unless (!strcmp(url.file, "/AimeeMann"));
	fail_unless (!strcmp(url.user, ""));
	fail_unless (!strcmp(url.pass, ""));
	fail_unless (url.proto == PROTO_HTTPS);
	fail_unless (url.port == 443);
END_TEST

START_TEST(test_set_url_port)
	url_t url;
	
	fail_if (0 == url_set(&url, "https://ahost:200/Lost/In/Space"));
	fail_unless (!strcmp(url.host, "ahost"));
	fail_unless (!strcmp(url.file, "/Lost/In/Space"));
	fail_unless (!strcmp(url.user, ""));
	fail_unless (!strcmp(url.pass, ""));
	fail_unless (url.proto == PROTO_HTTPS);
	fail_unless (url.port == 200);
END_TEST

START_TEST(test_set_url_username)
	url_t url;
	
	fail_if (0 == url_set(&url, "socks4://user@ahost/Space"));
	fail_unless (!strcmp(url.host, "ahost"));
	fail_unless (!strcmp(url.file, "/Space"));
	fail_unless (!strcmp(url.user, "user"));
	fail_unless (!strcmp(url.pass, ""));
	fail_unless (url.proto == PROTO_SOCKS4);
	fail_unless (url.port == 1080);
END_TEST

START_TEST(test_set_url_username_pwd)
	url_t url;
	
	fail_if (0 == url_set(&url, "socks5://user:pass@ahost/"));
	fail_unless (!strcmp(url.host, "ahost"));
	fail_unless (!strcmp(url.file, "/"));
	fail_unless (!strcmp(url.user, "user"));
	fail_unless (!strcmp(url.pass, "pass"));
	fail_unless (url.proto == PROTO_SOCKS5);
	fail_unless (url.port == 1080);
END_TEST

Suite *util_suite (void)
{
	Suite *s = suite_create("Util");
	TCase *tc_core = tcase_create("Core");
	suite_add_tcase (s, tc_core);
	tcase_add_test (tc_core, test_strip_linefeed);
	tcase_add_test (tc_core, test_strip_newlines);
	tcase_add_test (tc_core, test_set_url_http);
	tcase_add_test (tc_core, test_set_url_https);
	tcase_add_test (tc_core, test_set_url_port);
	tcase_add_test (tc_core, test_set_url_username);
	tcase_add_test (tc_core, test_set_url_username_pwd);
	return s;
}
