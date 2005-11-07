#if !defined(AFX_PROPCONN_H__8969671F_8D9F_45E9_929F_B36CFEFCBAA2__INCLUDED_)
#define AFX_PROPCONN_H__8969671F_8D9F_45E9_929F_B36CFEFCBAA2__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000
// PropConn.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CPropConn dialog

class CPropConn : public CPropertyPage
{
// Construction
public:
	CPropConn(CWnd* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(CPropConn)
	enum { IDD = IDD_PROPPAGE_CONNECTION };
	CEdit	m_proxyport;
	CListBox	m_proxytype;
	CButton	m_proxy_enabled;
	CButton	m_proxy_auth_enabled;
	CEdit	m_proxypass;
	CEdit	m_proxyhost;
	CEdit	m_proxyuser;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CPropConn)
	public:
	virtual void OnOK();
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CPropConn)
	afx_msg void OnProxyAuthEnabled();
	afx_msg void OnProxyEnabled();
	virtual BOOL OnInitDialog();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Developer Studio will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_PROPCONN_H__8969671F_8D9F_45E9_929F_B36CFEFCBAA2__INCLUDED_)
