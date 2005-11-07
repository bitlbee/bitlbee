#if !defined(AFX_PROPUSERS_H__BA8F6624_F693_403B_B3A8_B140955A894B__INCLUDED_)
#define AFX_PROPUSERS_H__BA8F6624_F693_403B_B3A8_B140955A894B__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000
// PropUsers.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CPropUsers dialog

class CPropUsers : public CPropertyPage
{
// Construction
public:
	CPropUsers();   // standard constructor

// Dialog Data
	//{{AFX_DATA(CPropUsers)
	enum { IDD = IDD_PROPPAGE_USERS };
	CListBox	m_known_users;
	CButton	m_kick;
	CButton	m_del_known_users;
	CListBox	m_current_users;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CPropUsers)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CPropUsers)
	afx_msg void OnKick();
	afx_msg void OnDelKnownUser();
	afx_msg void OnSelchangeCurrentUsers();
	afx_msg void OnSelchangeKnownUsers();
	afx_msg void OnRefreshKnownUsers();
	afx_msg void OnRefreshCurrentUsers();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Developer Studio will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_PROPUSERS_H__BA8F6624_F693_403B_B3A8_B140955A894B__INCLUDED_)
