#if !defined(AFX_PROPACCESS_H__0AC45777_B43C_4467_91FF_391DFD582057__INCLUDED_)
#define AFX_PROPACCESS_H__0AC45777_B43C_4467_91FF_391DFD582057__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000
// PropAccess.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CPropAccess dialog

class CPropAccess : public CPropertyPage
{
// Construction
public:
	CPropAccess();   // standard constructor

// Dialog Data
	//{{AFX_DATA(CPropAccess)
	enum { IDD = IDD_PROPPAGE_ACCESS };
	CButton	m_auth_registered;
	CButton	m_auth_open;
	CButton	m_auth_closed;
	CEdit	m_password;
	CEdit	m_port;
	CEdit	m_interface;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CPropAccess)
	public:
	virtual void OnOK();
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CPropAccess)
	afx_msg void OnAuthRegistered();
	afx_msg void OnAuthOpen();
	afx_msg void OnAuthClosed();
	virtual BOOL OnInitDialog();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Developer Studio will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_PROPACCESS_H__0AC45777_B43C_4467_91FF_391DFD582057__INCLUDED_)
