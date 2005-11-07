// PropMain.cpp : implementation file
//

#include <afxdlgs.h>
#include "PropMain.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CPropMain property page

IMPLEMENT_DYNCREATE(CPropMain, CPropertyPage)

CPropMain::CPropMain() : CPropertyPage(CPropMain::IDD)
{
	//{{AFX_DATA_INIT(CPropMain)
		// NOTE: the ClassWizard will add member initialization here
	//}}AFX_DATA_INIT
}

CPropMain::~CPropMain()
{
}

void CPropMain::DoDataExchange(CDataExchange* pDX)
{
	CPropertyPage::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CPropMain)
	DDX_Control(pDX, IDC_STOPSERVICE, m_stopservice);
	DDX_Control(pDX, IDC_STARTSERVICE, m_startservice);
	DDX_Control(pDX, IDC_PING_INTERVAL, m_ping_interval);
	DDX_Control(pDX, IDC_VERBOSE, m_Verbose);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CPropMain, CPropertyPage)
	//{{AFX_MSG_MAP(CPropMain)
	ON_BN_CLICKED(IDC_STARTSERVICE, OnStartService)
	ON_BN_CLICKED(IDC_STOPSERVICE, OnStopService)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CPropMain message handlers

BOOL CPropMain::OnInitDialog() 
{
	CPropertyPage::OnInitDialog();
	
	global.conf->verbose = GetProfileInt("verbose", 0);
	global.conf->ping_interval = GetProfileInt("ping_interval_timeout", 0);
	
	return TRUE;  // return TRUE unless you set the focus to a control
	              // EXCEPTION: OCX Property Pages should return FALSE
}

void CPropMain::OnOK() 
{	
	CPropertyPage::OnOK();

	WriteProfileInt("verbose", global.conf->verbose);
	
	WriteProfileInt("ping_interval_timeout", global.conf->ping_interval);
}

void CPropMain::OnStartService() 
{
	SC_HANDLE   schService;
	SC_HANDLE   schSCManager;
	SERVICE_STATUS status;

    schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS );
	schService = OpenService(schSCManager, "bitlbee", SERVICE_ALL_ACCESS);
	
	ControlService( schService, SERVICE_CONTROL_CONTINUE, &status );

    CloseServiceHandle(schService);
    CloseServiceHandle(schSCManager);
}

void CPropMain::OnStopService() 
{
		SC_HANDLE   schService;
	SC_HANDLE   schSCManager;
	SERVICE_STATUS status;

    schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS );
	schService = OpenService(schSCManager, "bitlbee", SERVICE_ALL_ACCESS);
	
	ControlService( schService, SERVICE_CONTROL_PAUSE, &status );

    CloseServiceHandle(schService);
    CloseServiceHandle(schSCManager);
}
