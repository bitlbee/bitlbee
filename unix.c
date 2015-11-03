/********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2012 Wilmer van der Gaast and others                *
  \********************************************************************/

/* Main file (Unix specific part)                                       */

/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License with
  the Debian GNU/Linux distribution in /usr/share/common-licenses/GPL;
  if not, write to the Free Software Foundation, Inc., 51 Franklin St.,
  Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "bitlbee.h"

#include "arc.h"
#include "base64.h"
#include "commands.h"
#include "protocols/nogaim.h"
#include "help.h"
#include "ipc.h"
#include "md5.h"
#include "misc.h"
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <pwd.h>
#include <locale.h>
#include <grp.h>

#if defined(OTR_BI) || defined(OTR_PI)
#include "otr.h"
#endif

global_t global;        /* Against global namespace pollution */

static struct {
	int fd[2];
	int tag;
} shutdown_pipe = {{-1 , -1}, 0};

static void sighandler_shutdown(int signal);
static void sighandler_crash(int signal);

static int crypt_main(int argc, char *argv[]);

int main(int argc, char *argv[])
{
	int i = 0;
	char *old_cwd = NULL;
	struct sigaction sig, old;

	/* Required to make iconv to ASCII//TRANSLIT work. This makes BitlBee
	   system-locale-sensitive. :-( */
	setlocale(LC_CTYPE, "");

	if (argc > 1 && strcmp(argv[1], "-x") == 0) {
		return crypt_main(argc, argv);
	}

	log_init();

	global.conf_file = g_strdup(CONF_FILE_DEF);
	global.conf = conf_load(argc, argv);
	if (global.conf == NULL) {
		return(1);
	}

	b_main_init();

	/* libpurple doesn't like fork()s after initializing itself, so if
	   we use it, do this init a little later (in case we're running in
	   ForkDaemon mode). */
#ifndef WITH_PURPLE
	nogaim_init();
#endif

#ifdef OTR_BI
	otr_init();
#endif

	global.helpfile = g_strdup(HELP_FILE);
	if (help_init(&global.help, global.helpfile) == NULL) {
		log_message(LOGLVL_WARNING, "Error opening helpfile %s.", HELP_FILE);
	}

	global.storage = storage_init(global.conf->primary_storage, global.conf->migrate_storage);
	if (global.storage == NULL) {
		log_message(LOGLVL_ERROR, "Unable to load storage backend '%s'", global.conf->primary_storage);
		return(1);
	}

	if (global.conf->runmode == RUNMODE_INETD) {
		log_link(LOGLVL_ERROR, LOGOUTPUT_IRC);
		log_link(LOGLVL_WARNING, LOGOUTPUT_IRC);

		i = bitlbee_inetd_init();
		log_message(LOGLVL_INFO, "%s %s starting in inetd mode.", PACKAGE, BITLBEE_VERSION);

	} else if (global.conf->runmode == RUNMODE_DAEMON) {
		log_link(LOGLVL_ERROR, LOGOUTPUT_CONSOLE);
		log_link(LOGLVL_WARNING, LOGOUTPUT_CONSOLE);

		i = bitlbee_daemon_init();
		log_message(LOGLVL_INFO, "%s %s starting in daemon mode.", PACKAGE, BITLBEE_VERSION);
	} else if (global.conf->runmode == RUNMODE_FORKDAEMON) {
		log_link(LOGLVL_ERROR, LOGOUTPUT_CONSOLE);
		log_link(LOGLVL_WARNING, LOGOUTPUT_CONSOLE);

		/* In case the operator requests a restart, we need this. */
		old_cwd = g_malloc(256);
		if (getcwd(old_cwd, 255) == NULL) {
			log_message(LOGLVL_WARNING, "Could not save current directory: %s", strerror(errno));
			g_free(old_cwd);
			old_cwd = NULL;
		}

		i = bitlbee_daemon_init();
		log_message(LOGLVL_INFO, "%s %s starting in forking daemon mode.", PACKAGE, BITLBEE_VERSION);
	}
	if (i != 0) {
		return(i);
	}

	if ((global.conf->user && *global.conf->user) &&
	    (global.conf->runmode == RUNMODE_DAEMON ||
	     global.conf->runmode == RUNMODE_FORKDAEMON) &&
	    (!getuid() || !geteuid())) {
		struct passwd *pw = NULL;
		pw = getpwnam(global.conf->user);
		if (pw) {
			initgroups(global.conf->user, pw->pw_gid);
			setgid(pw->pw_gid);
			setuid(pw->pw_uid);
		} else {
			log_message(LOGLVL_WARNING, "Failed to look up user %s.", global.conf->user);
		}
	}

	/* Catch some signals to tell the user what's happening before quitting */
	memset(&sig, 0, sizeof(sig));
	sig.sa_handler = SIG_IGN;
	sigaction(SIGCHLD, &sig, &old);
	sigaction(SIGPIPE, &sig, &old);
	sig.sa_flags = SA_RESETHAND;
	sig.sa_handler = sighandler_crash;
	sigaction(SIGSEGV, &sig, &old);

	sighandler_shutdown_setup();

	sig.sa_handler = sighandler_shutdown;
	sigaction(SIGINT, &sig, &old);
	sigaction(SIGTERM, &sig, &old);

	if (!getuid() || !geteuid()) {
		log_message(LOGLVL_WARNING, "BitlBee is running with root privileges. Why?");
	}

	b_main_run();

	/* Mainly good for restarting, to make sure we close the help.txt fd. */
	help_free(&global.help);

	if (global.restart) {
		char *fn = ipc_master_save_state();
		char *env;

		env = g_strdup_printf("_BITLBEE_RESTART_STATE=%s", fn);
		putenv(env);
		g_free(fn);
		/* Looks like env should *not* be freed here as putenv
		   doesn't make a copy. Odd. */

		i = chdir(old_cwd);
		close(global.listen_socket);

		if (execv(argv[0], argv) == -1) {
			/* Apparently the execve() failed, so let's just
			   jump back into our own/current main(). */
			/* Need more cleanup code to make this work. */
			return 1; /* main( argc, argv ); */
		}
	}

	return(0);
}

static int crypt_main(int argc, char *argv[])
{
	int pass_len;
	unsigned char *pass_cr, *pass_cl;

	if (argc < 4 || (strcmp(argv[2], "hash") != 0 &&
	                 strcmp(argv[2], "unhash") != 0 && argc < 5)) {
		printf("Supported:\n"
		       "  %s -x enc <key> <cleartext password>\n"
		       "  %s -x dec <key> <encrypted password>\n"
		       "  %s -x hash <cleartext password>\n"
		       "  %s -x unhash <hashed password>\n"
		       "  %s -x chkhash <hashed password> <cleartext password>\n",
		       argv[0], argv[0], argv[0], argv[0], argv[0]);
	} else if (strcmp(argv[2], "enc") == 0) {
		char *encoded;

		pass_len = arc_encode(argv[4], strlen(argv[4]), &pass_cr, argv[3], 12);

		encoded = base64_encode(pass_cr, pass_len);
		printf("%s\n", encoded);
		g_free(encoded);
		g_free(pass_cr);
	} else if (strcmp(argv[2], "dec") == 0) {
		pass_len = base64_decode(argv[4], &pass_cr);
		arc_decode(pass_cr, pass_len, (char **) &pass_cl, argv[3]);
		printf("%s\n", pass_cl);

		g_free(pass_cr);
		g_free(pass_cl);
	} else if (strcmp(argv[2], "hash") == 0) {
		md5_byte_t pass_md5[21];
		md5_state_t md5_state;
		char *encoded;

		random_bytes(pass_md5 + 16, 5);
		md5_init(&md5_state);
		md5_append(&md5_state, (md5_byte_t *) argv[3], strlen(argv[3]));
		md5_append(&md5_state, pass_md5 + 16, 5);   /* Add the salt. */
		md5_finish(&md5_state, pass_md5);

		encoded = base64_encode(pass_md5, 21);
		printf("%s\n", encoded);
		g_free(encoded);
	} else if (strcmp(argv[2], "unhash") == 0) {
		printf("Hash %s submitted to a massive Beowulf cluster of\n"
		       "overclocked 486s. Expect your answer next year somewhere around this time. :-)\n", argv[3]);
	} else if (strcmp(argv[2], "chkhash") == 0) {
		char *hash = strncmp(argv[3], "md5:", 4) == 0 ? argv[3] + 4 : argv[3];
		int st = md5_verify_password(argv[4], hash);

		printf("Hash %s given password.\n", st == 0 ? "matches" : "does not match");

		return st;
	}

	return 0;
}

/* Set up a pipe for SIGTERM/SIGINT so the actual signal handler doesn't do anything unsafe */
void sighandler_shutdown_setup()
{
	if (shutdown_pipe.fd[0] != -1) {
		/* called again from a forked process, clean up to avoid propagating the signal */
		b_event_remove(shutdown_pipe.tag);
		close(shutdown_pipe.fd[0]);
		close(shutdown_pipe.fd[1]);
	}

	if (pipe(shutdown_pipe.fd) == 0) {
		shutdown_pipe.tag = b_input_add(shutdown_pipe.fd[0], B_EV_IO_READ, bitlbee_shutdown, NULL);
	}
}

/* Signal handler for SIGTERM and SIGINT */
static void sighandler_shutdown(int signal)
{
	/* Write a single null byte to the pipe, just to send a message to the main loop.
	 * This gets handled by bitlbee_shutdown (the b_input_add callback for this pipe) */
	write(shutdown_pipe.fd[1], "", 1);
}

/* Signal handler for SIGSEGV
 * A desperate attempt to tell the user that everything is wrong in the world.
 * Avoids using irc_abort() because it has several unsafe calls to malloc */
static void sighandler_crash(int signal)
{
	GSList *l;
	const char *message = "ERROR :BitlBee crashed! (SIGSEGV received)\r\n";
	int len = strlen(message);

	for (l = irc_connection_list; l; l = l->next) {
		irc_t *irc = l->data;
		sock_make_blocking(irc->fd);
		write(irc->fd, message, len);
	}

	raise(signal);
}

double gettime()
{
	struct timeval time[1];

	gettimeofday(time, 0);
	return((double) time->tv_sec + (double) time->tv_usec / 1000000);
}
