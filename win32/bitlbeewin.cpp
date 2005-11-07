// bitlbee.cpp : Defines the class behaviors for the application.
//

#define BITLBEE_CORE
#include "bitlbeewin.h"
#include "traynot.h"
#include "maindlg.h"
#include <afxsock.h>
extern "C" {
#include "config.h"
#include "bitlbee.h"
#include <stdarg.h>
#include <gmodule.h>
int
inet_aton(const char *cp, struct in_addr *addr)
{
  addr->s_addr = inet_addr(cp);
  return (addr->s_addr == INADDR_NONE) ? 0 : 1;
}

void glib_logger (const gchar *log_domain, GLogLevelFlags log_level, const gchar *msg, gpointer user_data);

}

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CBitlbeeApp

BEGIN_MESSAGE_MAP(CBitlbeeApp, CWinApp)
	//{{AFX_MSG_MAP(CBitlbeeApp)
	ON_COMMAND(IDM_EXIT, OnExit)
	ON_COMMAND(IDM_SHOW, OnShow)
	//}}AFX_MSG_MAP
	ON_COMMAND(ID_HELP, CWinApp::OnHelp)
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CBitlbeeApp construction

CBitlbeeApp::CBitlbeeApp()
{
	// TODO: add construction code here,
	// Place all significant initialization in InitInstance
}

/////////////////////////////////////////////////////////////////////////////
// The one and only CBitlbeeApp object

CBitlbeeApp theApp;

/////////////////////////////////////////////////////////////////////////////
// CBitlbeeApp initialization

static UINT bb_loop(LPVOID data)
{
	g_main_run(global.loop);
	return 0;
}


gboolean bitlbee_new_client(GIOChannel *src, GIOCondition cond, gpointer data);
global_t global; // Against global namespace pollution

BOOL CBitlbeeApp::InitInstance()
{
	if (!AfxSocketInit())
	{
		AfxMessageBox(IDP_SOCKETS_INIT_FAILED);
		return FALSE;
	}

	// Standard initialization
	// If you are not using these features and wish to reduce the size
	//  of your final executable, you should remove from the following
	//  the specific initialization routines you do not need.

#ifdef _AFXDLL
	Enable3dControls();			// Call this when using MFC in a shared DLL
#else
	Enable3dControlsStatic();	// Call this when linking to MFC statically
#endif
	
	HKEY key;
	unsigned char databuf[256];
	DWORD len;
	RegOpenKey(HKEY_LOCAL_MACHINE, "SOFTWARE\\Bitlbee", &key);
	
	memset( &global, 0, sizeof( global_t ) );
	g_log_set_handler("GLib", static_cast<GLogLevelFlags>(G_LOG_LEVEL_WARNING | G_LOG_FLAG_FATAL | G_LOG_FLAG_RECURSION), glib_logger, NULL);
	global.loop = g_main_new(FALSE);
	nogaim_init();

	SetRegistryKey("Bitlbee");
	conf_t *conf = (conf_t *)g_new0( conf_t, 1 );
	global.conf = conf;
	global.conf->iface = g_strdup(GetProfileString("main", "interface", "0.0.0.0"));
	global.conf->port = GetProfileInt("main", "port", 6667);
	global.conf->verbose = GetProfileInt("main", "verbose", 0);
	global.conf->password = g_strdup(GetProfileString("main", "password", ""));
	global.conf->ping_interval = GetProfileInt("main", "ping_interval_timeout", 60);
	global.conf->hostname = g_strdup(GetProfileString("main", "hostname", "localhost"));
	if(RegQueryValueEx(key, "configdir", NULL, NULL, databuf, &len) != ERROR_SUCCESS) strcpy((char *)databuf, "");
	global.conf->configdir = g_strdup(GetProfileString("main", "configdir", (char *)databuf));
	if(RegQueryValueEx(key, "motdfile", NULL, NULL, databuf, &len) != ERROR_SUCCESS) strcpy((char *)databuf, "");
	global.conf->motdfile = g_strdup(GetProfileString("main", "motdfile", (char *)databuf));
	if(RegQueryValueEx(key, "helpfile", NULL, NULL, databuf, &len) != ERROR_SUCCESS) strcpy((char *)databuf, "");
	global.helpfile = g_strdup(GetProfileString("main", "helpfile", (char *)databuf));
	global.conf->runmode = RUNMODE_DAEMON;
	global.conf->authmode = (enum authmode) GetProfileInt("main", "AuthMode", AUTHMODE_CLOSED);
	strcpy(proxyhost, GetProfileString("proxy", "host", ""));
	strcpy(proxyuser, GetProfileString("proxy", "user", ""));
	strcpy(proxypass, GetProfileString("proxy", "password", ""));
	proxytype = GetProfileInt("proxy", "type", PROXY_NONE);
	proxyport = GetProfileInt("proxy", "port", 3128);

	dlg = new CMainDlg();
	not = new CTrayNot(dlg);
	dlg->ShowWindow(SW_HIDE);
	m_pMainWnd = not;	

	if(help_init(&(global.help)) == NULL) {
		log_message(LOGLVL_WARNING, "Unable to initialize help");
	}
	
	if(bitlbee_daemon_init() != 0) {
		return FALSE;
	}

	AfxBeginThread(bb_loop, NULL);


	return TRUE;
}

void log_error(char *a) {
	::MessageBox(NULL, a, "Bitlbee error", MB_OK | MB_ICONEXCLAMATION);
}

/* Dummy function. log output always goes to screen anyway */
void log_link(int level, int out) {}

void conf_loaddefaults(irc_t *irc) {}
double gettime() { 
	return CTime::GetCurrentTime().GetTime();
}

void load_protocol(char *name, char *init_function_name, struct prpl *p) {
	void (*init_function) (struct prpl *);

	char *path = g_module_build_path(NULL, name);
	if(!path) {
		log_message(LOGLVL_WARNING, "Can't build path for %s\n", name);
		return;
	}

	GModule *mod = g_module_open(path, G_MODULE_BIND_LAZY);
	if(!mod) {
		log_message(LOGLVL_INFO, "Can't find %s, not loading", name);
		return;
	}

	if(!g_module_symbol(mod,init_function_name,(void **) &init_function)) {
		log_message(LOGLVL_WARNING, "Can't find function %s in %s\n", init_function_name, path);
		return;
	}
	g_free(path);

	init_function(p);
}

void jabber_init(struct prpl *p) { load_protocol("jabber", "jabber_init", p); }
void msn_init(struct prpl *p) { load_protocol("msn", "msn_init", p); }
void byahoo_init(struct prpl *p) { load_protocol("yahoo", "byahoo_init", p); }
void oscar_init(struct prpl *p) { load_protocol("oscar", "oscar_init", p); }

void CBitlbeeApp::OnExit() 
{
	AfxGetApp()->ExitInstance();
	exit(0);
}

void CBitlbeeApp::OnShow() 
{
	dlg->ShowWindow(SW_SHOW);
}

int CBitlbeeApp::ExitInstance() 
{
	WriteProfileString("main", "interface", global.conf->iface);
	WriteProfileInt("main", "port", global.conf->port);
	WriteProfileInt("main", "verbose", global.conf->verbose);
	WriteProfileString("main", "password", global.conf->password);
	WriteProfileString("main", "configdir", global.conf->configdir);
	WriteProfileString("main", "hostname", global.conf->hostname);
	WriteProfileString("main", "motdfile", global.conf->motdfile);
	WriteProfileInt("main", "authmode", global.conf->authmode);
	WriteProfileInt("proxy", "type", proxytype);
	WriteProfileString("proxy", "host", proxyhost);
	WriteProfileString("proxy", "user", proxyuser);
	WriteProfileString("proxy", "password", proxypass);
	WriteProfileInt("proxy", "port", proxyport);
	WriteProfileInt("main", "ping_interval_timeout", global.conf->ping_interval);
	delete not;
	return CWinApp::ExitInstance();
}
