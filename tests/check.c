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

irc_t *torture_irc(void)
{
	irc_t *irc;
	GIOChannel *ch1, *ch2;
	if (!g_io_channel_pair(&ch1, &ch2))
		return NULL;
	irc = irc_new(g_io_channel_unix_get_fd(ch1));
	return irc;
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

/* From check_arc.c */
Suite *arc_suite(void);

/* From check_irc.c */
Suite *irc_suite(void);

/* From check_help.c */
Suite *help_suite(void);

/* From check_user.c */
Suite *user_suite(void);

/* From check_set.c */
Suite *set_suite(void);

/* From check_jabber_sasl.c */
Suite *jabber_sasl_suite(void);

/* From check_jabber_sasl.c */
Suite *jabber_util_suite(void);

int main (int argc, char **argv)
{
	int nf;
	SRunner *sr;
	GOptionContext *pc;
	gboolean no_fork = FALSE;
	gboolean verbose = FALSE;
	GOptionEntry options[] = {
		{"no-fork", 'n', 0, G_OPTION_ARG_NONE, &no_fork, "Don't fork" },
		{"verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose, "Be verbose", NULL },
		{ NULL }
	};
	int i;

	pc = g_option_context_new("");
	g_option_context_add_main_entries(pc, options, NULL);

	if(!g_option_context_parse(pc, &argc, &argv, NULL))
		return 1;

	g_option_context_free(pc);

	log_init();

	if (verbose) {
		log_link( LOGLVL_ERROR, LOGOUTPUT_CONSOLE );
#ifdef DEBUG
		log_link( LOGLVL_DEBUG, LOGOUTPUT_CONSOLE );
#endif
		log_link( LOGLVL_INFO, LOGOUTPUT_CONSOLE );
		log_link( LOGLVL_WARNING, LOGOUTPUT_CONSOLE );
	}

	global.conf = conf_load( 0, NULL);
	global.conf->runmode = RUNMODE_DAEMON;

	sr = srunner_create(util_suite());
	srunner_add_suite(sr, nick_suite());
	srunner_add_suite(sr, md5_suite());
	srunner_add_suite(sr, arc_suite());
	srunner_add_suite(sr, irc_suite());
	srunner_add_suite(sr, help_suite());
	srunner_add_suite(sr, user_suite());
	srunner_add_suite(sr, set_suite());
	srunner_add_suite(sr, jabber_sasl_suite());
	srunner_add_suite(sr, jabber_util_suite());
	if (no_fork)
		srunner_set_fork_status(sr, CK_NOFORK);
	srunner_run_all (sr, verbose?CK_VERBOSE:CK_NORMAL);
	nf = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
