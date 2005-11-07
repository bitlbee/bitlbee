// MainDlg.cpp : implementation file
//

#include "PropPaths.h"
#include "PropAccess.h"
#include "PropConn.h"
#include "MainDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CMainDlg

IMPLEMENT_DYNAMIC(CMainDlg, CPropertySheet)

CMainDlg::CMainDlg() : CPropertySheet("Bitlbee for Windows") 
{
	AddPage(new CPropPaths());
	AddPage(new CPropAccess());
	AddPage(new CPropertyPage(IDD_PROPPAGE_ABOUT));
	AddPage(new CPropConn());
	Create();
	ShowWindow(SW_HIDE);
}

CMainDlg::~CMainDlg()
{
}


BEGIN_MESSAGE_MAP(CMainDlg, CPropertySheet)
	//{{AFX_MSG_MAP(CMainDlg)
	ON_WM_CLOSE()
	//}}AFX_MSG_MAP
	ON_BN_CLICKED(IDOK, OnOK)
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CMainDlg message handlers

BOOL CMainDlg::OnInitDialog() 
{
	m_bModeless = FALSE;
	m_nFlags |= WF_CONTINUEMODAL;
	
	CPropertySheet::OnInitDialog();
	GetDlgItem(IDHELP)->ShowWindow(SW_HIDE);
	GetDlgItem(IDCANCEL)->ShowWindow(SW_HIDE);

	m_bModeless = TRUE;
	m_nFlags &= WF_CONTINUEMODAL;
	
	return TRUE;  // return TRUE unless you set the focus to a control
	              // EXCEPTION: OCX Property Pages should return FALSE
}

void CMainDlg::OnOK()
{
	PressButton(PSBTN_APPLYNOW);
	ShowWindow(SW_HIDE);
}


void CMainDlg::OnClose() 
{
	ShowWindow(SW_HIDE);
}
