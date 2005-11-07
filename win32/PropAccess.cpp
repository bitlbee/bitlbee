// PropAccess.cpp : implementation file
//

#define BITLBEE_CORE
#include "bitlbeewin.h"
#include "PropAccess.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CPropAccess dialog


CPropAccess::CPropAccess()
	: CPropertyPage(CPropAccess::IDD)
{

	//{{AFX_DATA_INIT(CPropAccess)
	//}}AFX_DATA_INIT
}


void CPropAccess::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CPropAccess)
	DDX_Control(pDX, IDC_AUTH_REGISTERED, m_auth_registered);
	DDX_Control(pDX, IDC_AUTH_OPEN, m_auth_open);
	DDX_Control(pDX, IDC_AUTH_CLOSED, m_auth_closed);
	DDX_Control(pDX, IDC_PASSWORD, m_password);
	DDX_Control(pDX, IDC_PORT, m_port);
	DDX_Control(pDX, IDC_INTERFACE, m_interface);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CPropAccess, CPropertyPage)
	//{{AFX_MSG_MAP(CPropAccess)
	ON_BN_CLICKED(IDC_AUTH_REGISTERED, OnAuthRegistered)
	ON_BN_CLICKED(IDC_AUTH_OPEN, OnAuthOpen)
	ON_BN_CLICKED(IDC_AUTH_CLOSED, OnAuthClosed)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CPropAccess message handlers

void CPropAccess::OnOK() 
{
	CString iface; m_interface.GetWindowText(iface);
	CString port; m_port.GetWindowText(port);

	CString password; m_password.GetWindowText(password);
	g_free((void *)global.conf->password);
	global.conf->password = g_strdup(password);

	if(m_auth_closed.GetCheck() == 1) global.conf->authmode = AUTHMODE_CLOSED;
	if(m_auth_open.GetCheck() == 1) global.conf->authmode = AUTHMODE_OPEN;
	if(m_auth_registered.GetCheck() == 1) global.conf->authmode = AUTHMODE_REGISTERED;

	if(strcmp(iface, global.conf->iface) || atol(port) != global.conf->port) {
		global.conf->port = atoi(port);
		g_free((void *)global.conf->iface);
		global.conf->iface = g_strdup(iface);
		closesocket(global.listen_socket);
		bitlbee_daemon_init();
	}

	
	CPropertyPage::OnOK();
}

void CPropAccess::OnAuthRegistered() 
{
	m_password.EnableWindow(FALSE);
	m_auth_open.SetCheck(0);
	m_auth_registered.SetCheck(1);
	m_auth_closed.SetCheck(0);
	
}

void CPropAccess::OnAuthOpen() 
{
	m_password.EnableWindow(FALSE);
	m_auth_open.SetCheck(1);
	m_auth_registered.SetCheck(0);
	m_auth_closed.SetCheck(0);
}

void CPropAccess::OnAuthClosed() 
{
	m_password.EnableWindow(TRUE);
	m_auth_open.SetCheck(0);
	m_auth_registered.SetCheck(0);
	m_auth_closed.SetCheck(1);
	
}

BOOL CPropAccess::OnInitDialog() 
{
	CPropertyPage::OnInitDialog();

	m_interface.SetWindowText(global.conf->iface);
	m_password.SetWindowText(global.conf->password);
	char tmp[20];
	g_snprintf(tmp, sizeof(tmp), "%d", global.conf->port);
	m_port.SetWindowText(tmp);
	m_auth_open.SetCheck(0);
	m_auth_closed.SetCheck(0);
	m_auth_registered.SetCheck(0);

	switch(global.conf->authmode) {
	case AUTHMODE_OPEN: m_auth_open.SetCheck(1); m_password.EnableWindow(FALSE);break;
	case AUTHMODE_CLOSED: m_auth_closed.SetCheck(1); m_password.EnableWindow(TRUE);break;
	case AUTHMODE_REGISTERED: m_auth_registered.SetCheck(1);m_password.EnableWindow(FALSE);break;
	}
	
	return TRUE;
}
