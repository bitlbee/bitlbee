#if !defined(AFX_PROPMAIN_H__F3EF57A7_15AA_4F36_B6C1_2EAD91127449__INCLUDED_)
#define AFX_PROPMAIN_H__F3EF57A7_15AA_4F36_B6C1_2EAD91127449__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000
// PropMain.h : header file

#include "Resource.h"
//

/////////////////////////////////////////////////////////////////////////////
// CPropMain dialog

class CPropMain : public CPropertyPage
{
	DECLARE_DYNCREATE(CPropMain)

// Construction
public:
	CPropMain();
	~CPropMain();

// Dialog Data
	//{{AFX_DATA(CPropMain)
	enum { IDD = IDD_PROPPAGE_MAIN };
	CButton	m_stopservice;
	CButton	m_startservice;
	CEdit	m_ping_interval;
	CButton	m_Verbose;
	//}}AFX_DATA


// Overrides
	// ClassWizard generate virtual function overrides
	//{{AFX_VIRTUAL(CPropMain)
	public:
	virtual void OnOK();
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:
	// Generated message map functions
	//{{AFX_MSG(CPropMain)
	virtual BOOL OnInitDialog();
	afx_msg void OnStartService();
	afx_msg void OnStopService();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()

};

//{{AFX_INSERT_LOCATION}}
// Microsoft Developer Studio will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_PROPMAIN_H__F3EF57A7_15AA_4F36_B6C1_2EAD91127449__INCLUDED_)
