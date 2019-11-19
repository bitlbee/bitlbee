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
		fail_unless(strcmp(copy, expected[i]) == 0,
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
		fail_unless(strcmp(copy, expected[i]) == 0,
		            "(%d) strip_newlines broken: %s -> %s (expected: %s)",
		            i, get[i], copy, expected[i]);
		fail_unless(copy == ret, "Original string not returned");
	}
}
END_TEST

START_TEST(test_set_url_http)
{
    url_t url;

    fail_if(0 == url_set(&url, "http://host/"));
    fail_unless(!strcmp(url.host, "host"));
    fail_unless(!strcmp(url.file, "/"));
    fail_unless(!strcmp(url.user, ""));
    fail_unless(!strcmp(url.pass, ""));
    fail_unless(url.proto == PROTO_HTTP);
    fail_unless(url.port == 80);
}
END_TEST

START_TEST(test_set_url_https)
{
    url_t url;

    fail_if(0 == url_set(&url, "https://ahost/AimeeMann"));
    fail_unless(!strcmp(url.host, "ahost"));
    fail_unless(!strcmp(url.file, "/AimeeMann"));
    fail_unless(!strcmp(url.user, ""));
    fail_unless(!strcmp(url.pass, ""));
    fail_unless(url.proto == PROTO_HTTPS);
    fail_unless(url.port == 443);
}
END_TEST

START_TEST(test_set_url_port)
{
    url_t url;

    fail_if(0 == url_set(&url, "https://ahost:200/Lost/In/Space"));
    fail_unless(!strcmp(url.host, "ahost"));
    fail_unless(!strcmp(url.file, "/Lost/In/Space"));
    fail_unless(!strcmp(url.user, ""));
    fail_unless(!strcmp(url.pass, ""));
    fail_unless(url.proto == PROTO_HTTPS);
    fail_unless(url.port == 200);
}
END_TEST

START_TEST(test_set_url_username)
{
    url_t url;

    fail_if(0 == url_set(&url, "socks4://user@ahost/Space"));
    fail_unless(!strcmp(url.host, "ahost"));
    fail_unless(!strcmp(url.file, "/Space"));
    fail_unless(!strcmp(url.user, "user"));
    fail_unless(!strcmp(url.pass, ""));
    fail_unless(url.proto == PROTO_SOCKS4);
    fail_unless(url.port == 1080);
}
END_TEST

START_TEST(test_set_url_username_pwd)
{
    url_t url;

    fail_if(0 == url_set(&url, "socks5://user:pass@ahost/"));
    fail_unless(!strcmp(url.host, "ahost"));
    fail_unless(!strcmp(url.file, "/"));
    fail_unless(!strcmp(url.user, "user"));
    fail_unless(!strcmp(url.pass, "pass"));
    fail_unless(url.proto == PROTO_SOCKS5);
    fail_unless(url.port == 1080);
}
END_TEST

struct {
	char *orig;
	int line_len;
	char *wrapped;
} word_wrap_tests[] = {
	{
		"Line-wrapping is not as easy as it seems?",
		16,
		"Line-wrapping is\nnot as easy as\nit seems?"
	},
	{
		"Line-wrapping is not as easy as it seems?",
		8,
		"Line-\nwrapping\nis not\nas easy\nas it\nseems?"
	},
	{
		"Line-wrapping is\nnot as easy as it seems?",
		8,
		"Line-\nwrapping\nis\nnot as\neasy as\nit\nseems?"
	},
	{
		"a aa aaa aaaa aaaaa aaaaaa aaaaaaa aaaaaaaa",
		5,
		"a aa\naaa\naaaa\naaaaa\naaaaa\na\naaaaa\naa\naaaaa\naaa",
	},
	{
		"aaaaaaaa aaaaaaa aaaaaa aaaaa aaaa aaa aa a",
		5,
		"aaaaa\naaa\naaaaa\naa\naaaaa\na\naaaaa\naaaa\naaa\naa a",
	},
	{
		"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
		5,
		"aaaaa\naaaaa\naaaaa\naaaaa\naaaaa\naaaaa\naaaaa\na",
	},
	{
		"áááááááááá",
		11,
		"ááááá\nááááá",
	},
	{
		NULL
	}
};

START_TEST(test_word_wrap)
{
    int i;

    for (i = 0; word_wrap_tests[i].orig && *word_wrap_tests[i].orig; i++) {
        char *wrapped = word_wrap(word_wrap_tests[i].orig, word_wrap_tests[i].line_len);

        fail_unless(strcmp(word_wrap_tests[i].wrapped, wrapped) == 0,
                "%s (line_len = %d) should wrap to `%s', not to `%s'",
                word_wrap_tests[i].orig, word_wrap_tests[i].line_len,
                word_wrap_tests[i].wrapped, wrapped);

        g_free(wrapped);
    }
}
END_TEST

START_TEST(test_http_encode)
{
    char s[80];

    strcpy(s, "ee\xc3" "\xab" "ee!!...");
    http_encode(s);
    fail_unless(strcmp(s, "ee%C3%ABee%21%21...") == 0);
}
END_TEST

struct {
	int limit;
	char *command;
	char *expected[IRC_MAX_ARGS + 1];
} split_tests[] = {
	{
		0, "account add etc \"user name with spaces\" 'pass\\ word'",
		{ "account", "add", "etc", "user name with spaces", "pass\\ word", NULL },
	},
	{
		0, "channel set group Close\\ friends",
		{ "channel", "set", "group", "Close friends", NULL },
	},
	{
		2, "reply wilmer \"testing in C is a PITA\", you said.",
		{ "reply", "wilmer", "\"testing in C is a PITA\", you said.", NULL },
	},
	{
		4, "one space  two  spaces  limit  limit",
		{ "one", "space", "two", "spaces", "limit  limit", NULL },
	},
	{
		0, NULL,
		{ NULL }
	},
};

START_TEST(test_split_command_parts)
{
    int i;
    for (i = 0; split_tests[i].command; i++) {
        char *cmd = g_strdup(split_tests[i].command);
        char **split = split_command_parts(cmd, split_tests[i].limit);
        char **expected = split_tests[i].expected;

        int j;
        for (j = 0; split[j] && expected[j]; j++) {
            fail_unless(strcmp(split[j], expected[j]) == 0,
                    "(%d) split_command_parts broken: split(\"%s\")[%d] -> %s (expected: %s)",
                    i, split_tests[i].command, j, split[j], expected[j]);
        }
        g_free(cmd);
    }
}
END_TEST

Suite *util_suite(void)
{
	Suite *s = suite_create("Util");
	TCase *tc_core = tcase_create("Core");

	suite_add_tcase(s, tc_core);
	tcase_add_test(tc_core, test_strip_linefeed);
	tcase_add_test(tc_core, test_strip_newlines);
	tcase_add_test(tc_core, test_set_url_http);
	tcase_add_test(tc_core, test_set_url_https);
	tcase_add_test(tc_core, test_set_url_port);
	tcase_add_test(tc_core, test_set_url_username);
	tcase_add_test(tc_core, test_set_url_username_pwd);
	tcase_add_test(tc_core, test_word_wrap);
	tcase_add_test(tc_core, test_http_encode);
	tcase_add_test(tc_core, test_split_command_parts);
	return s;
}
