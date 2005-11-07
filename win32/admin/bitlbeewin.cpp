// bitlbee.cpp : Defines the class behaviors for the application.
//

#include "maindlg.h"
#include "bitlbeewin.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CBitlbeeApp

BEGIN_MESSAGE_MAP(CBitlbeeApp, CWinApp)
	//{{AFX_MSG_MAP(CBitlbeeApp)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()


/////////////////////////////////////////////////////////////////////////////
// The one and only CBitlbeeApp object

CBitlbeeApp theApp;

/////////////////////////////////////////////////////////////////////////////
// CBitlbeeApp initialization

BOOL CBitlbeeApp::InitInstance()
{

	// Standard initialization
	// If you are not using these features and wish to reduce the size
	//  of your final executable, you should remove from the following
	//  the specific initialization routines you do not need.

#ifdef _AFXDLL
	Enable3dControls();			// Call this when using MFC in a shared DLL
#else
	Enable3dControlsStatic();	// Call this when linking to MFC statically
#endif

	new CMainDlg();

	return TRUE;
}
