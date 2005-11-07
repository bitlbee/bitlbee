// PropLog.cpp : implementation file
//

#define BITLBEE_CORE
#include "bitlbeewin.h"
#include "PropLog.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CPropLog dialog


CPropLog::CPropLog()
	: CPropertyPage(CPropLog::IDD)
{
	//{{AFX_DATA_INIT(CPropLog)
		// NOTE: the ClassWizard will add member initialization here
	//}}AFX_DATA_INIT
}


void CPropLog::DoDataExchange(CDataExchange* pDX)
{
	CPropertyPage::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CPropLog)
	DDX_Control(pDX, IDC_LOG, m_log);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CPropLog, CPropertyPage)
	//{{AFX_MSG_MAP(CPropLog)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CPropLog message handlers

static GList *log = NULL;

extern "C" {
void glib_logger (const gchar *log_domain, GLogLevelFlags log_level, const gchar *msg, gpointer user_data)
{
	log = g_list_append(log, g_strdup_printf("%s: %s", log_domain, msg));
}
}

void log_message(int level, char *message, ... ) { 
#define LOG_MAXLEN 300
	va_list ap;
	va_start(ap, message);
	char *msg = (char *)g_malloc(LOG_MAXLEN);
	g_vsnprintf(msg, LOG_MAXLEN, message, ap);
	va_end(ap);
	log = g_list_append(log, msg);
	if(level == LOGLVL_ERROR) ::MessageBox(NULL, msg, "Bitlbee", MB_OK | MB_ICONINFORMATION);
	TRACE("%d: %s\n", level, msg);
}


BOOL CPropLog::OnInitDialog() 
{
	CPropertyPage::OnInitDialog();
	
	m_log.ResetContent();
	GList *gl = log;
	while(gl) {
		char *d = (char *)gl->data;
		m_log.AddString(d);
		gl = gl->next;
	}
	
	return TRUE;  // return TRUE unless you set the focus to a control
	              // EXCEPTION: OCX Property Pages should return FALSE
}
