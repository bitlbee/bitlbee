#if !defined(AFX_MAINDLG_H__D88FFF4C_047D_4562_A041_CCDCFA52F87C__INCLUDED_)
#define AFX_MAINDLG_H__D88FFF4C_047D_4562_A041_CCDCFA52F87C__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000
// MainDlg.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CMainDlg

class CMainDlg : public CPropertySheet
{
	DECLARE_DYNAMIC(CMainDlg)

// Construction
public:
	CMainDlg();

// Attributes
public:

// Operations
public:

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CMainDlg)
	//}}AFX_VIRTUAL

// Implementation
public:
	virtual ~CMainDlg();

	// Generated message map functions
protected:
	//{{AFX_MSG(CMainDlg)
	virtual BOOL OnInitDialog();
	afx_msg void OnClose();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
private:
	void OnOK();
};

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Developer Studio will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_MAINDLG_H__D88FFF4C_047D_4562_A041_CCDCFA52F87C__INCLUDED_)
