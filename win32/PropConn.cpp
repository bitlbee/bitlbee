// PropConn.cpp : implementation file
//

#define BITLBEE_CORE
#include "bitlbeewin.h"
#include "PropConn.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CPropConn dialog


CPropConn::CPropConn(CWnd* pParent /*=NULL*/)
	: CPropertyPage(CPropConn::IDD)
{
	//{{AFX_DATA_INIT(CPropConn)
	//}}AFX_DATA_INIT
}


void CPropConn::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CPropConn)
	DDX_Control(pDX, IDC_PROXYPORT, m_proxyport);
	DDX_Control(pDX, IDC_PROXYTYPE, m_proxytype);
	DDX_Control(pDX, IDC_PROXY_ENABLED, m_proxy_enabled);
	DDX_Control(pDX, IDC_PROXY_AUTH_ENABLED, m_proxy_auth_enabled);
	DDX_Control(pDX, IDC_PROXYPASS, m_proxypass);
	DDX_Control(pDX, IDC_PROXYHOST, m_proxyhost);
	DDX_Control(pDX, IDC_PROXYUSER, m_proxyuser);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CPropConn, CPropertyPage)
	//{{AFX_MSG_MAP(CPropConn)
	ON_BN_CLICKED(IDC_PROXY_AUTH_ENABLED, OnProxyAuthEnabled)
	ON_BN_CLICKED(IDC_PROXY_ENABLED, OnProxyEnabled)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CPropConn message handlers

void CPropConn::OnProxyAuthEnabled() 
{
	m_proxyuser.EnableWindow(m_proxy_enabled.GetCheck() && m_proxy_auth_enabled.GetCheck());
	m_proxypass.EnableWindow(m_proxy_enabled.GetCheck() && m_proxy_auth_enabled.GetCheck());
}

void CPropConn::OnProxyEnabled() 
{
	// TODO: Add your control notification handler code here
	m_proxyhost.EnableWindow(m_proxy_enabled.GetCheck());
	m_proxytype.EnableWindow(m_proxy_enabled.GetCheck());
	m_proxyport.EnableWindow(m_proxy_enabled.GetCheck());
	m_proxy_auth_enabled.EnableWindow(m_proxy_enabled.GetCheck());

	if(m_proxy_enabled.GetCheck() && (m_proxytype.GetCurSel() < 0 || m_proxytype.GetCurSel() > 2)) 
	{
		m_proxytype.SetCurSel(0);
	}

	OnProxyAuthEnabled();
}

void CPropConn::OnOK() 
{
	if(!m_proxy_enabled.GetCheck()) {
		proxytype = PROXY_NONE;
		return;
	}

	CString tmp;
	m_proxyhost.GetWindowText(tmp);
	strcpy(proxyhost, tmp);

	m_proxyport.GetWindowText(tmp);
	proxyport = atoi(tmp);

	proxytype = m_proxytype.GetCurSel()+1;

	if(!m_proxy_auth_enabled.GetCheck()) {
		strcpy(proxyuser, "");
		strcpy(proxypass, "");
		return;
	}

	m_proxyuser.GetWindowText(tmp);
	strcpy(proxyuser, tmp);
	m_proxypass.GetWindowText(tmp);
	strcpy(proxypass, tmp);
	
	CPropertyPage::OnOK();
}

BOOL CPropConn::OnInitDialog() 
{
	char pp[20];
	CPropertyPage::OnInitDialog();

	m_proxyhost.SetWindowText(proxyhost);
	m_proxyuser.SetWindowText(proxyuser);
	m_proxypass.SetWindowText(proxypass);
	g_snprintf(pp, 20, "%d", proxyport);
	m_proxyport.SetWindowText(pp);

	m_proxytype.AddString("SOCKS 4");
	m_proxytype.AddString("SOCKS 5");
	m_proxytype.AddString("HTTP");
	m_proxytype.SetCurSel(proxytype-1);

	m_proxy_enabled.SetCheck(proxytype == PROXY_NONE?0:1); 
	m_proxy_auth_enabled.SetCheck(strcmp(proxyuser, "")?1:0);

	OnProxyEnabled();
	
	return TRUE;  
}
