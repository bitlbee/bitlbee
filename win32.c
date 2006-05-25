  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2004 Wilmer van der Gaast and others                *
  \********************************************************************/

/* Main file (Windows specific part)                                   */

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
#include "crypting.h"
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
	
	global.loop = g_main_new( FALSE );
	
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
	if( help_init( &(global.help) ) == NULL )
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
	
	g_main_run( global.loop );
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
 			g_main_run( global.loop );
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
	global.loop = g_main_new(FALSE);

	conf = g_new0( conf_t,1 );
	global.conf = conf;
	conf_get_string(key_main, "interface", "0.0.0.0", &global.conf->iface);
	conf_get_int(key_main, "port", 6667, &global.conf->port);
	conf_get_int(key_main, "verbose", 0, &global.conf->verbose);
	conf_get_string(key_main, "auth_pass", "", &global.conf->auth_pass);
	conf_get_string(key_main, "oper_pass", "", &global.conf->oper_pass);
	conf_get_int(key_main, "ping_interval_timeout", 60, &global.conf->ping_interval);
	conf_get_string(key_main, "hostname", "localhost", &global.conf->hostname);
	conf_get_string(key_main, "configdir", NULL, &global.conf->configdir);
	conf_get_string(key_main, "motdfile", NULL, &global.conf->motdfile);
	conf_get_string(key_main, "helpfile", NULL, &global.helpfile);
	global.conf->runmode = RUNMODE_DAEMON;
	conf_get_int(key_main, "AuthMode", AUTHMODE_OPEN, &global.conf->authmode);
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

#ifndef NS_INADDRSZ
#define NS_INADDRSZ	4
#endif
#ifndef NS_IN6ADDRSZ
#define NS_IN6ADDRSZ	16
#endif
#ifndef NS_INT16SZ
#define NS_INT16SZ	2
#endif

static const char *inet_ntop4(const guchar *src, char *dst, size_t size);
static const char *inet_ntop6(const guchar *src, char *dst, size_t size);

/* char *
 * inet_ntop(af, src, dst, size)
 *	convert a network format address to presentation format.
 * return:
 *	pointer to presentation format address (`dst'), or NULL (see errno).
 * author:
 *	Paul Vixie, 1996.
 */
const char *
inet_ntop(af, src, dst, size)
	int af;
	const void *src;
	char *dst;
	size_t size;
{
	switch (af) {
	case AF_INET:
		return (inet_ntop4(src, dst, size));
	case AF_INET6:
		return (inet_ntop6(src, dst, size));
	default:
		errno = WSAEAFNOSUPPORT;
		return (NULL);
	}
	/* NOTREACHED */
}

/* const char *
 * inet_ntop4(src, dst, size)
 *	format an IPv4 address
 * return:
 *	`dst' (as a const)
 * notes:
 *	(1) uses no statics
 *	(2) takes a u_char* not an in_addr as input
 * author:
 *	Paul Vixie, 1996.
 */
static const char *
inet_ntop4(src, dst, size)
	const u_char *src;
	char *dst;
	size_t size;
{
	static const char fmt[] = "%u.%u.%u.%u";
	char tmp[sizeof "255.255.255.255"];
	int nprinted;

	nprinted = g_snprintf(tmp, sizeof(tmp), fmt, src[0], src[1], src[2], src[3]);
	if (nprinted < 0)
		return (NULL);	/* we assume "errno" was set by "g_snprintf()" */
	if ((size_t)nprinted > size) {
		errno = ENOSPC;
		return (NULL);
	}
	strcpy(dst, tmp);
	return (dst);
}

/* const char *
 * inet_ntop6(src, dst, size)
 *	convert IPv6 binary address into presentation (printable) format
 * author:
 *	Paul Vixie, 1996.
 */
static const char *
inet_ntop6(src, dst, size)
	const u_char *src;
	char *dst;
	size_t size;
{
	/*
	 * Note that int32_t and int16_t need only be "at least" large enough
	 * to contain a value of the specified size.  On some systems, like
	 * Crays, there is no such thing as an integer variable with 16 bits.
	 * Keep this in mind if you think this function should have been coded
	 * to use pointer overlays.  All the world's not a VAX.
	 */
	char tmp[sizeof "ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255"], *tp;
	struct { int base, len; } best, cur;
	guint words[NS_IN6ADDRSZ / NS_INT16SZ];
	int i;

	/*
	 * Preprocess:
	 *	Copy the input (bytewise) array into a wordwise array.
	 *	Find the longest run of 0x00's in src[] for :: shorthanding.
	 */
	memset(words, '\0', sizeof words);
	for (i = 0; i < NS_IN6ADDRSZ; i++)
		words[i / 2] |= (src[i] << ((1 - (i % 2)) << 3));
	best.base = -1;
	cur.base = -1;
	for (i = 0; i < (NS_IN6ADDRSZ / NS_INT16SZ); i++) {
		if (words[i] == 0) {
			if (cur.base == -1)
				cur.base = i, cur.len = 1;
			else
				cur.len++;
		} else {
			if (cur.base != -1) {
				if (best.base == -1 || cur.len > best.len)
					best = cur;
				cur.base = -1;
			}
		}
	}
	if (cur.base != -1) {
		if (best.base == -1 || cur.len > best.len)
			best = cur;
	}
	if (best.base != -1 && best.len < 2)
		best.base = -1;

	/*
	 * Format the result.
	 */
	tp = tmp;
	for (i = 0; i < (NS_IN6ADDRSZ / NS_INT16SZ); i++) {
		/* Are we inside the best run of 0x00's? */
		if (best.base != -1 && i >= best.base &&
		    i < (best.base + best.len)) {
			if (i == best.base)
				*tp++ = ':';
			continue;
		}
		/* Are we following an initial run of 0x00s or any real hex? */
		if (i != 0)
			*tp++ = ':';
		/* Is this address an encapsulated IPv4? */
		if (i == 6 && best.base == 0 &&
		    (best.len == 6 || (best.len == 5 && words[5] == 0xffff))) {
			if (!inet_ntop4(src+12, tp, sizeof tmp - (tp - tmp)))
				return (NULL);
			tp += strlen(tp);
			break;
		}
		tp += g_snprintf(tp, sizeof tmp - (tp - tmp), "%x", words[i]);
	}
	/* Was it a trailing run of 0x00's? */
	if (best.base != -1 && (best.base + best.len) ==
	    (NS_IN6ADDRSZ / NS_INT16SZ))
		*tp++ = ':';
	*tp++ = '\0';

	/*
	 * Check for overflow, copy, and we're done.
	 */
	if ((size_t)(tp - tmp) > size) {
		errno = ENOSPC;
		return (NULL);
	}
	strcpy(dst, tmp);
	return (dst);
}

#ifdef AF_INET
static int inet_pton4(const char *src, u_char *dst);
#endif
#ifdef AF_INET6
static int inet_pton6(const char *src, u_char *dst);
#endif

/* int
 * inet_pton(af, src, dst)
 *	convert from presentation format (which usually means ASCII printable)
 *	to network format (which is usually some kind of binary format).
 * return:
 *	1 if the address was valid for the specified address family
 *	0 if the address wasn't valid (`dst' is untouched in this case)
 *	-1 if some other error occurred (`dst' is untouched in this case, too)
 * author:
 *	Paul Vixie, 1996.
 */
int
inet_pton(af, src, dst)
	int af;
	const char *src;
	void *dst;
{
	switch (af) {
#ifdef AF_INET
	case AF_INET:
		return (inet_pton4(src, dst));
#endif
#ifdef AF_INET6
	case AF_INET6:
		return (inet_pton6(src, dst));
#endif
	default:
		errno = WSAEAFNOSUPPORT;
		return (-1);
	}
	/* NOTREACHED */
}

#ifdef AF_INET
/* int
 * inet_pton4(src, dst)
 *	like inet_aton() but without all the hexadecimal and shorthand.
 * return:
 *	1 if `src' is a valid dotted quad, else 0.
 * notice:
 *	does not touch `dst' unless it's returning 1.
 * author:
 *	Paul Vixie, 1996.
 */
static int
inet_pton4(src, dst)
	const char *src;
	u_char *dst;
{
	static const char digits[] = "0123456789";
	int saw_digit, octets, ch;
	u_char tmp[NS_INADDRSZ], *tp;

	saw_digit = 0;
	octets = 0;
	*(tp = tmp) = 0;
	while ((ch = *src++) != '\0') {
		const char *pch;

		if ((pch = strchr(digits, ch)) != NULL) {
			u_int new = *tp * 10 + (pch - digits);

			if (new > 255)
				return (0);
			*tp = new;
			if (! saw_digit) {
				if (++octets > 4)
					return (0);
				saw_digit = 1;
			}
		} else if (ch == '.' && saw_digit) {
			if (octets == 4)
				return (0);
			*++tp = 0;
			saw_digit = 0;
		} else
			return (0);
	}
	if (octets < 4)
		return (0);
	memcpy(dst, tmp, NS_INADDRSZ);
	return (1);
}
#endif

#ifdef AF_INET6
/* int
 * inet_pton6(src, dst)
 *	convert presentation level address to network order binary form.
 * return:
 *	1 if `src' is a valid [RFC1884 2.2] address, else 0.
 * notice:
 *	(1) does not touch `dst' unless it's returning 1.
 *	(2) :: in a full address is silently ignored.
 * credit:
 *	inspired by Mark Andrews.
 * author:
 *	Paul Vixie, 1996.
 */
static int
inet_pton6(src, dst)
	const char *src;
	u_char *dst;
{
	static const char xdigits_l[] = "0123456789abcdef",
			  xdigits_u[] = "0123456789ABCDEF";
	u_char tmp[NS_IN6ADDRSZ], *tp, *endp, *colonp;
	const char *xdigits, *curtok;
	int ch, saw_xdigit;
	u_int val;

	memset((tp = tmp), '\0', NS_IN6ADDRSZ);
	endp = tp + NS_IN6ADDRSZ;
	colonp = NULL;
	/* Leading :: requires some special handling. */
	if (*src == ':')
		if (*++src != ':')
			return (0);
	curtok = src;
	saw_xdigit = 0;
	val = 0;
	while ((ch = *src++) != '\0') {
		const char *pch;

		if ((pch = strchr((xdigits = xdigits_l), ch)) == NULL)
			pch = strchr((xdigits = xdigits_u), ch);
		if (pch != NULL) {
			val <<= 4;
			val |= (pch - xdigits);
			if (val > 0xffff)
				return (0);
			saw_xdigit = 1;
			continue;
		}
		if (ch == ':') {
			curtok = src;
			if (!saw_xdigit) {
				if (colonp)
					return (0);
				colonp = tp;
				continue;
			} else if (*src == '\0') {
				return (0);
			}
			if (tp + NS_INT16SZ > endp)
				return (0);
			*tp++ = (u_char) (val >> 8) & 0xff;
			*tp++ = (u_char) val & 0xff;
			saw_xdigit = 0;
			val = 0;
			continue;
		}
		if (ch == '.' && ((tp + NS_INADDRSZ) <= endp) &&
		    inet_pton4(curtok, tp) > 0) {
			tp += NS_INADDRSZ;
			saw_xdigit = 0;
			break;	/* '\0' was seen by inet_pton4(). */
		}
		return (0);
	}
	if (saw_xdigit) {
		if (tp + NS_INT16SZ > endp)
			return (0);
		*tp++ = (u_char) (val >> 8) & 0xff;
		*tp++ = (u_char) val & 0xff;
	}
	if (colonp != NULL) {
		/*
		 * Since some memmove()'s erroneously fail to handle
		 * overlapping regions, we'll do the shift by hand.
		 */
		const int n = tp - colonp;
		int i;

		if (tp == endp)
			return (0);
		for (i = 1; i <= n; i++) {
			endp[- i] = colonp[n - i];
			colonp[n - i] = 0;
		}
		tp = endp;
	}
	if (tp != endp)
		return (0);
	memcpy(dst, tmp, NS_IN6ADDRSZ);
	return (1);
}
#endif
