#include <stdlib.h>
#include <glib.h>
#include <gmodule.h>
#include <check.h>
#include "bitlbee.h"

global_t global;	/* Against global namespace pollution */

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

int main (void)
{
	int nf;
	SRunner *sr = srunner_create(util_suite());
	srunner_add_suite(sr, nick_suite());
	srunner_run_all (sr, CK_NORMAL);
	nf = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
