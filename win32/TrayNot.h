#if !defined(AFX_TRAYNOT_H__C8B7E607_671B_4C97_8251_AF5AA83DF401__INCLUDED_)
#define AFX_TRAYNOT_H__C8B7E607_671B_4C97_8251_AF5AA83DF401__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000
// TrayNot.h : header file
//

#include <afxwin.h>

#define BITLBEE_TRAY_ICON WM_USER+1

/////////////////////////////////////////////////////////////////////////////
// CTrayNot dialog

class CTrayNot : public CDialog
{
// Construction
public:
	virtual  ~CTrayNot();
	CTrayNot(CPropertySheet *);   // standard constructor

// Dialog Data
	//{{AFX_DATA(CTrayNot)
	enum { IDD = IDD_PHONY };
		// NOTE: the ClassWizard will add data members here
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CTrayNot)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CTrayNot)
		// NOTE: the ClassWizard will add member functions here
	afx_msg LONG OnSysTrayIconClick (WPARAM wParam, LPARAM lParam);
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
	CPropertySheet *dlg;
	void ShowQuickMenu();
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Developer Studio will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_TRAYNOT_H__C8B7E607_671B_4C97_8251_AF5AA83DF401__INCLUDED_)
