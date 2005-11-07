// bitlbee.h : main header file for the BITLBEE application
//

#if !defined(AFX_BITLBEE_H__E76A1E72_2177_4477_A796_76D6AB817F80__INCLUDED_)
#define AFX_BITLBEE_H__E76A1E72_2177_4477_A796_76D6AB817F80__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000

#include <afxwin.h>
#include <afxdlgs.h>
#include <windows.h>
#include "resource.h"		// main symbols

class CTrayNot;
extern "C" {
#define BITLBEE_CORE
#include "bitlbee.h"
}

/////////////////////////////////////////////////////////////////////////////
// CBitlbeeApp:
// See bitlbee.cpp for the implementation of this class
//

class CBitlbeeApp : public CWinApp
{
public:
	CBitlbeeApp();

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CBitlbeeApp)
	public:
	virtual BOOL InitInstance();
	virtual int ExitInstance();
	//}}AFX_VIRTUAL

// Implementation

	//{{AFX_MSG(CBitlbeeApp)
	afx_msg void OnExit();
	afx_msg void OnShow();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
	protected:
		CTrayNot *not;
		CPropertySheet *dlg;
		GIOChannel *listen;
};

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Developer Studio will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_BITLBEE_H__E76A1E72_2177_4477_A796_76D6AB817F80__INCLUDED_)

