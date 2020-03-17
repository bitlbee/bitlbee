#include <stdlib.h>
#include <glib.h>
#include <gmodule.h>
#include <check.h>
#include <string.h>
#include <stdio.h>
#include "irc.h"
#include "events.h"
#include "testsuite.h"

START_TEST(test_connect)
{
    GIOChannel * ch1, *ch2;
    irc_t *irc;
    char *raw;
    fail_unless(g_io_channel_pair(&ch1, &ch2));

    irc = irc_new(g_io_channel_unix_get_fd(ch1));

    irc_free(irc);

    fail_unless(g_io_channel_read_to_end(ch2, &raw, NULL, NULL) == G_IO_STATUS_NORMAL);

    fail_if(strcmp(raw, "") != 0);

    g_free(raw);
}
END_TEST

START_TEST(test_login)
{
    GIOChannel * ch1, *ch2;
    irc_t *irc;
    char *raw;
    fail_unless(g_io_channel_pair(&ch1, &ch2));

    g_io_channel_set_flags(ch1, G_IO_FLAG_NONBLOCK, NULL);
    g_io_channel_set_flags(ch2, G_IO_FLAG_NONBLOCK, NULL);

    irc = irc_new(g_io_channel_unix_get_fd(ch1));

    fail_unless(g_io_channel_write_chars(ch2, "NICK bla\r\r\n"
                "USER a a a a\n", -1, NULL, NULL) == G_IO_STATUS_NORMAL);
    fail_unless(g_io_channel_flush(ch2, NULL) == G_IO_STATUS_NORMAL);

    b_main_iteration();
    irc_free(irc);

    fail_unless(g_io_channel_read_to_end(ch2, &raw, NULL, NULL) == G_IO_STATUS_NORMAL);

    fail_unless(strstr(raw, "001") != NULL);
    fail_unless(strstr(raw, "002") != NULL);
    fail_unless(strstr(raw, "003") != NULL);
    fail_unless(strstr(raw, "004") != NULL);
    fail_unless(strstr(raw, "005") != NULL);

    g_free(raw);
}
END_TEST

Suite *irc_suite(void)
{
	Suite *s = suite_create("IRC");
	TCase *tc_core = tcase_create("Core");

	suite_add_tcase(s, tc_core);
	tcase_add_test(tc_core, test_connect);
	tcase_add_test(tc_core, test_login);
	return s;
}
