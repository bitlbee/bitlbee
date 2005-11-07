#if !defined(AFX_PROPPATHS_H__693C8F02_C150_45DD_99F1_F824795E98C9__INCLUDED_)
#define AFX_PROPPATHS_H__693C8F02_C150_45DD_99F1_F824795E98C9__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000
// PropPaths.h : header file
//
#include <afxdlgs.h>
#include "Resource.h"

/////////////////////////////////////////////////////////////////////////////
// CPropPaths dialog

class CPropPaths : public CPropertyPage
{
// Construction
public:
	CPropPaths();   // standard constructor

// Dialog Data
	//{{AFX_DATA(CPropPaths)
	enum { IDD = IDD_PROPPAGE_PATHS };
	CEdit	m_motdfile;
	CButton	m_edit_motd;
	CEdit	m_configdir;
	CButton	m_browse_motd;
	CButton	m_browse_config;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CPropPaths)
	public:
	virtual void OnOK();
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CPropPaths)
	afx_msg void OnBrowseConfig();
	afx_msg void OnBrowseMotd();
	afx_msg void OnEditMotd();
	virtual BOOL OnInitDialog();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Developer Studio will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_PROPPATHS_H__693C8F02_C150_45DD_99F1_F824795E98C9__INCLUDED_)
