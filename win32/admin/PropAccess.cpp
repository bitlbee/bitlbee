// PropAccess.cpp : implementation file
//

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
	WriteProfileString("interface", iface);

	CString port; m_port.GetWindowText(port);
	WriteProfileInt("port", port);

	CString password; m_password.GetWindowText(password);
	WriteProfileString("password", password);
	
	if(m_auth_closed.GetCheck() == 1) WriteProfileInt("auth_mode", 1);
	if(m_auth_open.GetCheck() == 1) WriteProfileInt("auth_mode", 0);
	if(m_auth_registered.GetCheck() == 1) WriteProfileInt("auth_mode", 2);
	
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

	HKEY key;
	RegOpenKey(HKEY_LOCAL_MACHINE, BITLBEE_KEY, &key);

	m_interface.SetWindowText(GetProfileString("interface", "0.0.0.0"));
	m_password.SetWindowText(GetProfileString("password", ""));
	char tmp[20];
	sprintf(tmp, "%d", GetProfileInt("port", 6667));
	m_port.SetWindowText(tmp);

	switch(GetProfileInt("auth_mode", 1)) {
	case 0: OnAuthOpen();break;
	case 1: OnAuthClosed();break;
	case 2: OnAuthRegistered();break;
	}
	
	return TRUE;
}
