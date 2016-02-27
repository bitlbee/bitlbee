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

#include "commands.h"
#include "protocols/nogaim.h"
#include "help.h"
#include "ipc.h"
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

#ifdef HAVE_BACKTRACE
#include <execinfo.h>
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
#ifdef HAVE_BACKTRACE
	void *unused[1];
#endif

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

	if (global.conf->runmode == RUNMODE_INETD) {
		log_link(LOGLVL_ERROR, LOGOUTPUT_IRC);
		log_link(LOGLVL_WARNING, LOGOUTPUT_IRC);
	} else {
		log_link(LOGLVL_ERROR, LOGOUTPUT_CONSOLE);
		log_link(LOGLVL_WARNING, LOGOUTPUT_CONSOLE);
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

	global.auth = auth_init(global.conf->auth_backend);
	if (global.conf->auth_backend && global.auth == NULL) {
		log_message(LOGLVL_ERROR, "Unable to load authentication backend '%s'", global.conf->auth_backend);
		return(1);
	}

	if (global.conf->runmode == RUNMODE_INETD) {
		i = bitlbee_inetd_init();
		log_message(LOGLVL_INFO, "%s %s starting in inetd mode.", PACKAGE, BITLBEE_VERSION);

	} else if (global.conf->runmode == RUNMODE_DAEMON) {
		i = bitlbee_daemon_init();
		log_message(LOGLVL_INFO, "%s %s starting in daemon mode.", PACKAGE, BITLBEE_VERSION);
	} else if (global.conf->runmode == RUNMODE_FORKDAEMON) {
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
		if (!pw) {
			log_message(LOGLVL_ERROR, "Failed to look up user %s.", global.conf->user);

		} else if (initgroups(global.conf->user, pw->pw_gid) != 0) {
			log_message(LOGLVL_ERROR, "initgroups: %s.", strerror(errno));

		} else if (setgid(pw->pw_gid) != 0) {
			log_message(LOGLVL_ERROR, "setgid(%d): %s.", pw->pw_gid, strerror(errno));

		} else if (setuid(pw->pw_uid) != 0) {
			log_message(LOGLVL_ERROR, "setuid(%d): %s.", pw->pw_uid, strerror(errno));
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

#ifdef HAVE_BACKTRACE
	/* As per the backtrace(3) man page, call this outside of the signal
	 * handler once to ensure any dynamic libraries are loaded in an
	 * async-signal-safe environment to prevent deadlocks */
	backtrace(unused, 1);
#endif

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
		password_encrypt(argv[4], argv[3], &encoded);
		printf("%s\n", encoded);
		g_free(encoded);
	} else if (strcmp(argv[2], "dec") == 0) {
		char *decoded;
		password_decrypt(argv[4], argv[3], &decoded);
		printf("%s\n", decoded);
		g_free(decoded);
	} else if (strcmp(argv[2], "hash") == 0) {
		char *hash;
		password_hash(argv[3], &hash);
		printf("%s\n", hash);
		g_free(hash);
	} else if (strcmp(argv[2], "unhash") == 0) {
		printf("Hash %s submitted to a massive Beowulf cluster of\n"
		       "overclocked 486s. Expect your answer next year somewhere around this time. :-)\n", argv[3]);
	} else if (strcmp(argv[2], "chkhash") == 0) {
		char *hash = strncmp(argv[3], "md5:", 4) == 0 ? argv[3] + 4 : argv[3];
		int st = password_verify(argv[4], hash);

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
	int unused G_GNUC_UNUSED;
	/* Write a single null byte to the pipe, just to send a message to the main loop.
	 * This gets handled by bitlbee_shutdown (the b_input_add callback for this pipe) */
	unused = write(shutdown_pipe.fd[1], "", 1);
}

#ifdef HAVE_BACKTRACE
/* Writes a backtrace to (usually) /var/lib/bitlbee/crash.log
 * No malloc allowed means not a lot can be written to that file */
static void sighandler_crash_backtrace()
{
	int fd, mapsfd;
	int size;
	void *trace[128];
	const char message[] = "## " PACKAGE " crashed\n"
		"## Version: " BITLBEE_VERSION "\n"
		"## Configure args: " BITLBEE_CONFIGURE_ARGS "\n"
		"##\n"
		"## Backtrace:\n\n";
	const char message2[] = "\n"
		"## Hint: To get details on addresses use\n"
		"##   addr2line -e <binary> <address>\n"
		"## or\n"
		"##   gdb <binary> -ex 'l *<address>' -ex q\n"
		"## where <binary> is a filename from above and <address> is the part between (...)\n"
		"##\n\n";
	const char message3[] = "\n## End of memory maps. See above for the backtrace\n\n";

	fd = open(CRASHFILE, O_WRONLY | O_APPEND | O_CREAT, 0600);

	if (fd == -1 || write(fd, message, sizeof(message) - 1) == -1) {
		return;
	}

	size = backtrace(trace, 128);
	backtrace_symbols_fd(trace, size, fd);

	(void) write(fd, message2, sizeof(message2) - 1);

	/* a bit too linux-specific, so fail gracefully */
	mapsfd = open("/proc/self/maps", O_RDONLY, 0);

	if (mapsfd != -1) {
		char buf[4096] = {0};
		ssize_t bytes;

		while ((bytes = read(mapsfd, buf, sizeof(buf))) > 0) {
			(void) write(fd, buf, bytes);
		}
		(void) close(mapsfd);
		(void) write(fd, message3, sizeof(message3) - 1);
	}

	(void) close(fd);
}
#endif

/* Signal handler for SIGSEGV
 * A desperate attempt to tell the user that everything is wrong in the world.
 * Avoids using irc_abort() because it has several unsafe calls to malloc */
static void sighandler_crash(int signal)
{
	GSList *l;
	const char message[] = "ERROR :BitlBee crashed! (SIGSEGV received)\r\n"
#ifdef HAVE_BACKTRACE
		"ERROR :Writing backtrace to " CRASHFILE "\r\n"
#endif
		"ERROR :This is a bug either in BitlBee or a plugin, ask us on IRC if unsure\r\n";

	for (l = irc_connection_list; l; l = l->next) {
		irc_t *irc = l->data;
		sock_make_blocking(irc->fd);
		if (irc->sendbuffer) {
			(void) write(irc->fd, irc->sendbuffer->str, irc->sendbuffer->len);
		}
		(void) write(irc->fd, message, sizeof(message) - 1);
	}

#ifdef HAVE_BACKTRACE
	sighandler_crash_backtrace();
#endif

	raise(signal);
}

double gettime()
{
	struct timeval time[1];

	gettimeofday(time, 0);
	return((double) time->tv_sec + (double) time->tv_usec / 1000000);
}
