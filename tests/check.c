#include <stdlib.h>
#include <glib.h>
#include <gmodule.h>
#include <check.h>

/* From check_util.c */
Suite *util_suite(void);

int main (void)
{
	int nf;
	SRunner *sr = srunner_create(util_suite());
	srunner_run_all (sr, CK_NORMAL);
	nf = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
