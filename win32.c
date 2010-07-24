  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway					 *
  *																	*
  * Copyright 2002-2004 Wilmer van der Gaast and others				*
  \********************************************************************/

/* Main file (Windows specific part)								   */

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
  if not, write to the Free Software Foundation, Inc., 59 Temple Place,
  Suite 330, Boston, MA  02111-1307  USA
*/

#define BITLBEE_CORE
#include "bitlbee.h"
#include "commands.h"
#include "protocols/nogaim.h"
#include "help.h"
#include <signal.h>
#include <windows.h>

global_t global;	/* Against global namespace pollution */

static void WINAPI service_ctrl (DWORD dwControl)
{
	switch (dwControl)
	{
		case SERVICE_CONTROL_STOP:
			/* FIXME */
			break;

		case SERVICE_CONTROL_INTERROGATE:
			break;

		default:
			break;

	}
}

static void bitlbee_init(int argc, char **argv)
{
	int i = -1;
	memset( &global, 0, sizeof( global_t ) );

	b_main_init();
	
	global.conf = conf_load( argc, argv );
	if( global.conf == NULL )
		return;
	
	if( global.conf->runmode == RUNMODE_INETD )
	{
		i = bitlbee_inetd_init();
		log_message( LOGLVL_INFO, "Bitlbee %s starting in inetd mode.", BITLBEE_VERSION );

	}
	else if( global.conf->runmode == RUNMODE_DAEMON )
	{
		i = bitlbee_daemon_init();
		log_message( LOGLVL_INFO, "Bitlbee %s starting in daemon mode.", BITLBEE_VERSION );
	} 
	else 
	{
		log_message( LOGLVL_INFO, "No bitlbee mode specified...");
	}
	
	if( i != 0 )
		return;
 	
	if( access( global.conf->configdir, F_OK ) != 0 )
		log_message( LOGLVL_WARNING, "The configuration directory %s does not exist. Configuration won't be saved.", global.conf->configdir );
	else if( access( global.conf->configdir, 06 ) != 0 )
		log_message( LOGLVL_WARNING, "Permission problem: Can't read/write from/to %s.", global.conf->configdir );
	if( help_init( &(global.help), HELP_FILE ) == NULL )
		log_message( LOGLVL_WARNING, "Error opening helpfile %s.", global.helpfile );
}

void service_main (DWORD argc, LPTSTR *argv)
{
	SERVICE_STATUS_HANDLE handle;
	SERVICE_STATUS status;

	handle = RegisterServiceCtrlHandler("bitlbee", service_ctrl);

	if (!handle)
		return;

	status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	status.dwServiceSpecificExitCode = 0;

	bitlbee_init(argc, argv);

	SetServiceStatus(handle, &status);
	
	b_main_run( );
}

SERVICE_TABLE_ENTRY dispatch_table[] =
{
   { TEXT("bitlbee"), (LPSERVICE_MAIN_FUNCTION)service_main },
   { NULL, NULL }
};

static int debug = 0;

static void usage()
{
	printf("Options:\n");
	printf("-h   Show this help message\n");
	printf("-d   Debug mode (simple console program)\n");
}

int main( int argc, char **argv)
{	
	int i;
	WSADATA WSAData;

	nogaim_init( );

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-d")) debug = 1;
		if (!strcmp(argv[i], "-h")) {
			usage();
			return 0;
		}
	}

	WSAStartup(MAKEWORD(1,1), &WSAData);

	if (!debug) {
		if (!StartServiceCtrlDispatcher(dispatch_table))
			log_message( LOGLVL_ERROR, "StartServiceCtrlDispatcher failed.");
	} else {
			bitlbee_init(argc, argv);
 			b_main_run();
	}
	
	return 0;
}

double gettime()
{
	return (GetTickCount() / 1000);
}

void conf_get_string(HKEY section, const char *name, const char *def, char **dest)
{
	char buf[4096];
	long x;
	if (RegQueryValue(section, name, buf, &x) == ERROR_SUCCESS) {
		*dest = g_strdup(buf);
	} else if (!def) {
		*dest = NULL;
	} else {
		*dest = g_strdup(def);
	}
}


void conf_get_int(HKEY section, const char *name, int def, int *dest)
{
	char buf[20];
	long x;
	DWORD y;
	if (RegQueryValue(section, name, buf, &x) == ERROR_SUCCESS) {
		memcpy(&y, buf, sizeof(DWORD));
		*dest = y;
	} else {
		*dest = def;
	}
}

conf_t *conf_load( int argc, char *argv[] ) 
{
	conf_t *conf;
	HKEY key, key_main, key_proxy;
	char *tmp;

	RegOpenKey(HKEY_CURRENT_USER, "SOFTWARE\\Bitlbee", &key);
	RegOpenKey(key, "main", &key_main);
	RegOpenKey(key, "proxy", &key_proxy);
	
	memset( &global, 0, sizeof( global_t ) );
	b_main_init();

	conf = g_new0( conf_t,1 );
	global.conf = conf;
	conf_get_string(key_main, "interface_in", "0.0.0.0", &global.conf->iface_in);
	conf_get_string(key_main, "interface_out", "0.0.0.0", &global.conf->iface_out);
	conf_get_string(key_main, "port", "6667", &global.conf->port);
	conf_get_int(key_main, "verbose", 0, &global.conf->verbose);
	conf_get_string(key_main, "auth_pass", "", &global.conf->auth_pass);
	conf_get_string(key_main, "oper_pass", "", &global.conf->oper_pass);
	conf_get_int(key_main, "ping_interval_timeout", 60, &global.conf->ping_interval);
	conf_get_string(key_main, "hostname", "localhost", &global.conf->hostname);
	conf_get_string(key_main, "configdir", NULL, &global.conf->configdir);
	conf_get_string(key_main, "motdfile", NULL, &global.conf->motdfile);
	conf_get_string(key_main, "helpfile", NULL, &global.helpfile);
	global.conf->runmode = RUNMODE_DAEMON;
	conf_get_int(key_main, "AuthMode", AUTHMODE_OPEN, (int *)&global.conf->authmode);
	conf_get_string(key_proxy, "host", "", &tmp); strcpy(proxyhost, tmp);
	conf_get_string(key_proxy, "user", "", &tmp); strcpy(proxyuser, tmp);
	conf_get_string(key_proxy, "password", "", &tmp); strcpy(proxypass, tmp);
	conf_get_int(key_proxy, "type", PROXY_NONE, &proxytype);
	conf_get_int(key_proxy, "port", 3128, &proxyport);

	RegCloseKey(key);
	RegCloseKey(key_main);
	RegCloseKey(key_proxy);

	return conf;
}

void conf_loaddefaults( irc_t *irc )
{
	HKEY key_defaults;
	int i;
	char name[4096], data[4096];
	DWORD namelen = sizeof(name), datalen = sizeof(data);
	DWORD type;
	if (RegOpenKey(HKEY_LOCAL_MACHINE, "SOFTWARE\\Bitlbee\\defaults", &key_defaults) != ERROR_SUCCESS) {
		return;
	}

	for (i = 0; RegEnumValue(key_defaults, i, name, &namelen, NULL, &type, data, &datalen) == ERROR_SUCCESS; i++) {
		set_t *s = set_find( &irc->set, name );
			
		if( s )
		{
			if( s->def ) g_free( s->def );
			s->def = g_strdup( data );
		}

		namelen = sizeof(name);
		datalen = sizeof(data);
	}

	RegCloseKey(key_defaults);
}

#ifndef INADDR_NONE
#define INADDR_NONE 0xffffffff
#endif

int
inet_aton(const char *cp, struct in_addr *addr)
{
  addr->s_addr = inet_addr(cp);
  return (addr->s_addr == INADDR_NONE) ? 0 : 1;
}

void log_error(char *msg)
{
	log_message(LOGLVL_ERROR, "%s", msg);
}

void log_message(int level, char *message, ...)
{
	HANDLE  hEventSource;
	LPTSTR  lpszStrings[2];
	WORD elevel;
	va_list ap;

	va_start(ap, message);

	if (debug) {
		vprintf(message, ap);
		putchar('\n');
		va_end(ap);
		return;
	}

	hEventSource = RegisterEventSource(NULL, TEXT("bitlbee"));

	lpszStrings[0] = TEXT("bitlbee");
	lpszStrings[1] = g_strdup_vprintf(message, ap);
	va_end(ap);

	switch (level) {
	case LOGLVL_ERROR: elevel = EVENTLOG_ERROR_TYPE; break;
	case LOGLVL_WARNING: elevel = EVENTLOG_WARNING_TYPE; break;
	case LOGLVL_INFO: elevel = EVENTLOG_INFORMATION_TYPE; break;
#ifdef DEBUG
	case LOGLVL_DEBUG: elevel = EVENTLOG_AUDIT_SUCCESS; break;
#endif
	}

	if (hEventSource != NULL) {
		ReportEvent(hEventSource, 
		elevel,
		0,					
		0,					
		NULL,				 
		2,					
		0,					
		lpszStrings,		  
		NULL);				

		DeregisterEventSource(hEventSource);
	}

	g_free(lpszStrings[1]);
}

void log_link(int level, int output) { /* FIXME */ }

struct tm *
gmtime_r (const time_t *timer, struct tm *result)
{
	struct tm *local_result;
	local_result = gmtime (timer);

	if (local_result == NULL || result == NULL)
		return NULL;

	memcpy (result, local_result, sizeof (result));
	return result;
} 
