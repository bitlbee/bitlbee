  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2004 Wilmer van der Gaast and others                *
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
  if not, write to the Free Software Foundation, Inc., 59 Temple Place,
  Suite 330, Boston, MA  02111-1307  USA
*/

#include "bitlbee.h"
#include "commands.h"
#include "crypting.h"
#include "protocols/nogaim.h"
#include "help.h"
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>
#include <winreg.h>
#include <winbase.h>

global_t global;	/* Against global namespace pollution */

int main( int argc, char *argv[] )
{
	int i = -1;
	memset( &global, 0, sizeof( global_t ) );
	
	global.loop = g_main_new( FALSE );
	
	log_init( );
	nogaim_init( );
	
	global.conf = conf_load( argc, argv );
	if( global.conf == NULL )
		return( 1 );
	
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
		return( i );
 	
	if( access( global.conf->configdir, F_OK ) != 0 )
		log_message( LOGLVL_WARNING, "The configuration directory %s does not exist. Configuration won't be saved.", global.conf->configdir );
	else if( access( global.conf->configdir, R_OK ) != 0 || access( global.conf->configdir, W_OK ) != 0 )
		log_message( LOGLVL_WARNING, "Permission problem: Can't read/write from/to %s.", global.conf->configdir );
	if( help_init( &(global.help) ) == NULL )
		log_message( LOGLVL_WARNING, "Error opening helpfile %s.", global.helpfile );
	
	g_main_run( global.loop );
	
	return( 0 );
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
	RegOpenKey(HKEY_LOCAL_MACHINE, "SOFTWARE\\Bitlbee", &key);
	RegOpenKey(HKEY_LOCAL_MACHINE, "SOFTWARE\\Bitlbee\\main", &key_main);
	RegOpenKey(HKEY_LOCAL_MACHINE, "SOFTWARE\\Bitlbee\\proxy", &key_proxy);
	
	memset( &global, 0, sizeof( global_t ) );
	global.loop = g_main_new(FALSE);
	nogaim_init();

	conf = g_new0( conf_t,1 );
	global.conf = conf;
	conf_get_string(key_main, "interface", "0.0.0.0", &global.conf->iface);
	conf_get_int(key_main, "port", 6667, &global.conf->port);
	conf_get_int(key_main, "verbose", 0, &global.conf->verbose);
	conf_get_string(key_main, "password", "", &global.conf->password);
	conf_get_int(key_main, "ping_interval_timeout", 60, &global.conf->ping_interval);
	conf_get_string(key_main, "hostname", "localhost", &global.conf->hostname);
	conf_get_string(key_main, "configdir", NULL, &global.conf->configdir);
	conf_get_string(key_main, "motdfile", NULL, &global.conf->motdfile);
	conf_get_string(key_main, "helpfile", NULL, &global.helpfile);
	global.conf->runmode = RUNMODE_INETD;
	conf_get_int(key_main, "AuthMode", AUTHMODE_CLOSED, &global.conf->authmode);
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
		set_t *s = set_find( irc, name );
			
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
