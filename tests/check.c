#include <stdlib.h>
#include <glib.h>
#include <gmodule.h>
#include <check.h>
#include "bitlbee.h"
#include "testsuite.h"

global_t global;	/* Against global namespace pollution */

gboolean g_io_channel_pair(GIOChannel **ch1, GIOChannel **ch2)
{
	int sock[2];
	if (socketpair(AF_UNIX, SOCK_STREAM, PF_UNIX, sock) < 0) {
		perror("socketpair");
		return FALSE;
	}

	*ch1 = g_io_channel_unix_new(sock[0]);
	*ch2 = g_io_channel_unix_new(sock[1]);
	return TRUE;
}

double gettime()
{
	struct timeval time[1];

	gettimeofday( time, 0 );
	return( (double) time->tv_sec + (double) time->tv_usec / 1000000 );
}

/* From check_util.c */
Suite *util_suite(void);

/* From check_nick.c */
Suite *nick_suite(void);

/* From check_md5.c */
Suite *md5_suite(void);

int main (void)
{
	int nf;
	SRunner *sr = srunner_create(util_suite());
	srunner_add_suite(sr, nick_suite());
	srunner_add_suite(sr, md5_suite());
	srunner_run_all (sr, CK_NORMAL);
	nf = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
