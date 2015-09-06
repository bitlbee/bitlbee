/********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2005 Wilmer van der Gaast and others                *
  \********************************************************************/

/* Configuration reading code						*/

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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "conf.h"
#include "ini.h"
#include "url.h"
#include "ipc.h"

#include "proxy.h"

static int conf_loadini(conf_t *conf, char *file);
static void conf_free(conf_t *conf);

conf_t *conf_load(int argc, char *argv[])
{
	conf_t *conf;
	int opt, i, config_missing = 0;

	conf = g_new0(conf_t, 1);

	conf->iface_in = NULL;
	conf->iface_out = NULL;
	conf->port = g_strdup("6667");
	conf->nofork = 0;
	conf->verbose = 0;
	conf->primary_storage = g_strdup("xml");
	conf->migrate_storage = g_strsplit("text", ",", -1);
	conf->runmode = RUNMODE_INETD;
	conf->authmode = AUTHMODE_OPEN;
	conf->auth_pass = NULL;
	conf->oper_pass = NULL;
	conf->configdir = g_strdup(CONFIG);
	conf->plugindir = g_strdup(PLUGINDIR);
	conf->pidfile = g_strdup(PIDFILE);
	conf->motdfile = g_strdup(ETCDIR "/motd.txt");
	conf->ping_interval = 180;
	conf->ping_timeout = 300;
	conf->user = NULL;
	conf->ft_max_size = SIZE_MAX;
	conf->ft_max_kbps = G_MAXUINT;
	conf->ft_listen = NULL;
	conf->protocols = NULL;
	conf->cafile = NULL;
	proxytype = 0;

	i = conf_loadini(conf, global.conf_file);
	if (i == 0) {
		fprintf(stderr, "Error: Syntax error in configuration file `%s'.\n", global.conf_file);
		return NULL;
	} else if (i == -1) {
		config_missing++;
		/* Whine after parsing the options if there was no -c pointing
		   at a *valid* configuration file. */
	}

	while (argc > 0 && (opt = getopt(argc, argv, "i:p:P:nvIDFc:d:hu:V")) >= 0) {
		/*     ^^^^ Just to make sure we skip this step from the REHASH handler. */
		if (opt == 'i') {
			conf->iface_in = g_strdup(optarg);
		} else if (opt == 'p') {
			g_free(conf->port);
			conf->port = g_strdup(optarg);
		} else if (opt == 'P') {
			g_free(conf->pidfile);
			conf->pidfile = g_strdup(optarg);
		} else if (opt == 'n') {
			conf->nofork = 1;
		} else if (opt == 'v') {
			conf->verbose = 1;
		} else if (opt == 'I') {
			conf->runmode = RUNMODE_INETD;
		} else if (opt == 'D') {
			conf->runmode = RUNMODE_DAEMON;
		} else if (opt == 'F') {
			conf->runmode = RUNMODE_FORKDAEMON;
		} else if (opt == 'c') {
			if (strcmp(global.conf_file, optarg) != 0) {
				g_free(global.conf_file);
				global.conf_file = g_strdup(optarg);
				conf_free(conf);
				/* Re-evaluate arguments. Don't use this option twice,
				   you'll end up in an infinite loop! Hope this trick
				   works with all libcs BTW.. */
				optind = 1;
				return conf_load(argc, argv);
			}
		} else if (opt == 'd') {
			g_free(conf->configdir);
			conf->configdir = g_strdup(optarg);
		} else if (opt == 'h') {
			printf("Usage: bitlbee [-D/-F [-i <interface>] [-p <port>] [-n] [-v]] [-I]\n"
			       "               [-c <file>] [-d <dir>] [-x] [-h]\n"
			       "\n"
			       "An IRC-to-other-chat-networks gateway\n"
			       "\n"
			       "  -I  Classic/InetD mode. (Default)\n"
			       "  -D  Daemon mode. (one process serves all)\n"
			       "  -F  Forking daemon. (one process per client)\n"
			       "  -u  Run daemon as specified user.\n"
			       "  -P  Specify PID-file (not for inetd mode)\n"
			       "  -i  Specify the interface (by IP address) to listen on.\n"
			       "      (Default: 0.0.0.0 (any interface))\n"
			       "  -p  Port number to listen on. (Default: 6667)\n"
			       "  -n  Don't fork.\n"
			       "  -v  Be verbose (only works in combination with -n)\n"
			       "  -c  Load alternative configuration file\n"
			       "  -d  Specify alternative user configuration directory\n"
			       "  -x  Command-line interface to password encryption/hashing\n"
			       "  -h  Show this help page.\n"
			       "  -V  Show version info.\n");
			return NULL;
		} else if (opt == 'V') {
			printf("BitlBee %s\nAPI version %06x\n",
			       BITLBEE_VERSION, BITLBEE_VERSION_CODE);
			return NULL;
		} else if (opt == 'u') {
			g_free(conf->user);
			conf->user = g_strdup(optarg);
		}
	}

	if (conf->configdir[strlen(conf->configdir) - 1] != '/') {
		char *s = g_new(char, strlen(conf->configdir) + 2);

		sprintf(s, "%s/", conf->configdir);
		g_free(conf->configdir);
		conf->configdir = s;
	}

	if (config_missing) {
		fprintf(stderr, "Warning: Unable to read configuration file `%s'.\n", global.conf_file);
	}

	if (conf->cafile && access(conf->cafile, R_OK) != 0) {
		/* Let's treat this as a serious problem so people won't think
		   they're secure when in fact they're not. */
		fprintf(stderr, "Error: Could not read CA file %s: %s\n", conf->cafile, strerror(errno));
		return NULL;
	}

	return conf;
}

static void conf_free(conf_t *conf)
{
	/* Free software means users have the four essential freedoms:
	   0. to run the program,
	   2. to study and change the program in source code form,
	   2. to redistribute exact copies, and
	   3. to distribute modified versions
	*/
	g_free(conf->auth_pass);
	g_free(conf->cafile);
	g_free(conf->configdir);
	g_free(conf->ft_listen);
	g_free(conf->hostname);
	g_free(conf->iface_in);
	g_free(conf->iface_out);
	g_free(conf->motdfile);
	g_free(conf->oper_pass);
	g_free(conf->pidfile);
	g_free(conf->plugindir);
	g_free(conf->port);
	g_free(conf->primary_storage);
	g_free(conf->user);
	g_strfreev(conf->migrate_storage);
	g_strfreev(conf->protocols);
	g_free(conf);

}

static int conf_loadini(conf_t *conf, char *file)
{
	ini_t *ini;
	int i;

	ini = ini_open(file);
	if (ini == NULL) {
		return -1;
	}
	while (ini_read(ini)) {
		if (g_strcasecmp(ini->section, "settings") == 0) {
			if (g_strcasecmp(ini->key, "runmode") == 0) {
				if (g_strcasecmp(ini->value, "daemon") == 0) {
					conf->runmode = RUNMODE_DAEMON;
				} else if (g_strcasecmp(ini->value, "forkdaemon") == 0) {
					conf->runmode = RUNMODE_FORKDAEMON;
				} else {
					conf->runmode = RUNMODE_INETD;
				}
			} else if (g_strcasecmp(ini->key, "pidfile") == 0) {
				g_free(conf->pidfile);
				conf->pidfile = g_strdup(ini->value);
			} else if (g_strcasecmp(ini->key, "daemoninterface") == 0) {
				g_free(conf->iface_in);
				conf->iface_in = g_strdup(ini->value);
			} else if (g_strcasecmp(ini->key, "daemonport") == 0) {
				g_free(conf->port);
				conf->port = g_strdup(ini->value);
			} else if (g_strcasecmp(ini->key, "clientinterface") == 0) {
				g_free(conf->iface_out);
				conf->iface_out = g_strdup(ini->value);
			} else if (g_strcasecmp(ini->key, "authmode") == 0) {
				if (g_strcasecmp(ini->value, "registered") == 0) {
					conf->authmode = AUTHMODE_REGISTERED;
				} else if (g_strcasecmp(ini->value, "closed") == 0) {
					conf->authmode = AUTHMODE_CLOSED;
				} else {
					conf->authmode = AUTHMODE_OPEN;
				}
			} else if (g_strcasecmp(ini->key, "authpassword") == 0) {
				g_free(conf->auth_pass);
				conf->auth_pass = g_strdup(ini->value);
			} else if (g_strcasecmp(ini->key, "operpassword") == 0) {
				g_free(conf->oper_pass);
				conf->oper_pass = g_strdup(ini->value);
			} else if (g_strcasecmp(ini->key, "hostname") == 0) {
				g_free(conf->hostname);
				conf->hostname = g_strdup(ini->value);
			} else if (g_strcasecmp(ini->key, "configdir") == 0) {
				g_free(conf->configdir);
				conf->configdir = g_strdup(ini->value);
			} else if (g_strcasecmp(ini->key, "plugindir") == 0) {
				g_free(conf->plugindir);
				conf->plugindir = g_strdup(ini->value);
			} else if (g_strcasecmp(ini->key, "motdfile") == 0) {
				g_free(conf->motdfile);
				conf->motdfile = g_strdup(ini->value);
			} else if (g_strcasecmp(ini->key, "account_storage") == 0) {
				g_free(conf->primary_storage);
				conf->primary_storage = g_strdup(ini->value);
			} else if (g_strcasecmp(ini->key, "account_storage_migrate") == 0) {
				g_strfreev(conf->migrate_storage);
				conf->migrate_storage = g_strsplit_set(ini->value, " \t,;", -1);
			} else if (g_strcasecmp(ini->key, "pinginterval") == 0) {
				if (sscanf(ini->value, "%d", &i) != 1) {
					fprintf(stderr, "Invalid %s value: %s\n", ini->key, ini->value);
					return 0;
				}
				conf->ping_interval = i;
			} else if (g_strcasecmp(ini->key, "pingtimeout") == 0) {
				if (sscanf(ini->value, "%d", &i) != 1) {
					fprintf(stderr, "Invalid %s value: %s\n", ini->key, ini->value);
					return 0;
				}
				conf->ping_timeout = i;
			} else if (g_strcasecmp(ini->key, "proxy") == 0) {
				url_t *url = g_new0(url_t, 1);

				if (!url_set(url, ini->value)) {
					fprintf(stderr, "Invalid %s value: %s\n", ini->key, ini->value);
					g_free(url);
					return 0;
				}

				strncpy(proxyhost, url->host, sizeof(proxyhost));
				strncpy(proxyuser, url->user, sizeof(proxyuser));
				strncpy(proxypass, url->pass, sizeof(proxypass));
				proxyport = url->port;
				if (url->proto == PROTO_HTTP) {
					proxytype = PROXY_HTTP;
				} else if (url->proto == PROTO_SOCKS4) {
					proxytype = PROXY_SOCKS4;
				} else if (url->proto == PROTO_SOCKS5) {
					proxytype = PROXY_SOCKS5;
				}

				g_free(url);
			} else if (g_strcasecmp(ini->key, "user") == 0) {
				g_free(conf->user);
				conf->user = g_strdup(ini->value);
			} else if (g_strcasecmp(ini->key, "ft_max_size") == 0) {
				size_t ft_max_size;
				if (sscanf(ini->value, "%zu", &ft_max_size) != 1) {
					fprintf(stderr, "Invalid %s value: %s\n", ini->key, ini->value);
					return 0;
				}
				conf->ft_max_size = ft_max_size;
			} else if (g_strcasecmp(ini->key, "ft_max_kbps") == 0) {
				if (sscanf(ini->value, "%d", &i) != 1) {
					fprintf(stderr, "Invalid %s value: %s\n", ini->key, ini->value);
					return 0;
				}
				conf->ft_max_kbps = i;
			} else if (g_strcasecmp(ini->key, "ft_listen") == 0) {
				g_free(conf->ft_listen);
				conf->ft_listen = g_strdup(ini->value);
			} else if (g_strcasecmp(ini->key, "protocols") == 0) {
				g_strfreev(conf->protocols);
				conf->protocols = g_strsplit_set(ini->value, " \t,;", -1);
			} else if (g_strcasecmp(ini->key, "cafile") == 0) {
				g_free(conf->cafile);
				conf->cafile = g_strdup(ini->value);
			} else {
				fprintf(stderr, "Error: Unknown setting `%s` in configuration file (line %d).\n",
				        ini->key, ini->line);
				return 0;
				/* For now just ignore unknown keys... */
			}
		} else if (g_strcasecmp(ini->section, "defaults") != 0) {
			fprintf(stderr, "Error: Unknown section [%s] in configuration file (line %d). "
			        "BitlBee configuration must be put in a [settings] section!\n", ini->section,
			        ini->line);
			return 0;
		}
	}
	ini_close(ini);

	return 1;
}

void conf_loaddefaults(irc_t *irc)
{
	ini_t *ini;

	ini = ini_open(global.conf_file);
	if (ini == NULL) {
		return;
	}
	while (ini_read(ini)) {
		if (g_strcasecmp(ini->section, "defaults") == 0) {
			set_t *s = set_find(&irc->b->set, ini->key);

			if (s) {
				if (s->def) {
					g_free(s->def);
				}
				s->def = g_strdup(ini->value);
			}
		}
	}
	ini_close(ini);
}
