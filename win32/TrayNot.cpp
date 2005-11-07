// TrayNot.cpp : implementation file
//

#define BITLBEE_CORE
#include "bitlbeewin.h"
#include "TrayNot.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CTrayNot dialog


CTrayNot::CTrayNot(CPropertySheet *s)
	: CDialog(CTrayNot::IDD, NULL)
{
	Create(CTrayNot::IDD);
	EnableWindow(FALSE);

	dlg = s;

	/* Traybar icon */
	NOTIFYICONDATA dat;
	dat.cbSize = sizeof(NOTIFYICONDATA);
	dat.hWnd = m_hWnd;
	dat.uID = 1;
	dat.uFlags = NIF_ICON + NIF_TIP + NIF_MESSAGE;
	dat.hIcon = AfxGetApp()->LoadIcon(IDI_BEE);
	dat.uCallbackMessage = BITLBEE_TRAY_ICON;
	strcpy(dat.szTip, "Bitlbee manager");
	Shell_NotifyIcon(NIM_ADD, &dat);

	//{{AFX_DATA_INIT(CTrayNot)
		// NOTE: the ClassWizard will add member initialization here
	//}}AFX_DATA_INIT
}


void CTrayNot::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CTrayNot)
		// NOTE: the ClassWizard will add DDX and DDV calls here
	//}}AFX_DATA_MAP
}

/////////////////////////////////////////////////////////////////////////////
// CTrayNot message handlers

CTrayNot::~CTrayNot()
{
	NOTIFYICONDATA dat;
	dat.cbSize = sizeof(NOTIFYICONDATA);
	dat.hWnd = m_hWnd;
	dat.uID = 1;
	Shell_NotifyIcon(NIM_DELETE, &dat);
}


BEGIN_MESSAGE_MAP(CTrayNot, CDialog)
	//{{AFX_MSG_MAP(CTrayNot)
		// NOTE - the ClassWizard will add and remove mapping macros here.
		ON_MESSAGE (BITLBEE_TRAY_ICON, OnSysTrayIconClick)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()


/////////////////////////////////////////////////////////////////////////////
// CTrayNot message handlers

afx_msg LONG CTrayNot::OnSysTrayIconClick (WPARAM wParam, LPARAM lParam)
{
 switch (lParam)
    {
              case WM_LBUTTONDOWN:
					dlg->ShowWindow(SW_SHOW);
					break;
              case WM_RBUTTONDOWN:
                   ShowQuickMenu ();
                   break ;
    }
 	return 0;
}

void CTrayNot::ShowQuickMenu()
{
   POINT CurPos;

   CMenu qmenu;
   qmenu.LoadMenu(IDR_POPUP);
   
   GetCursorPos (&CurPos);

   CMenu *submenu = qmenu.GetSubMenu(0);

   SetForegroundWindow();
   // Display the menu. This menu is a popup loaded elsewhere.

	submenu->TrackPopupMenu (TPM_RIGHTBUTTON | TPM_RIGHTALIGN,
                   CurPos.x,
                   CurPos.y,this);
}
